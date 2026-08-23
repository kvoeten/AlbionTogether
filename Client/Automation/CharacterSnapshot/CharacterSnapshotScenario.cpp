#include "CharacterSnapshotScenario.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Core/Target/FableNativeLayout.h"

#include <cmath>
#include <cstdio>

namespace fable::automation::character_snapshot
{
using namespace fable::core::bootstrap;
using namespace fable::core::target;

    bool InitializeCharacterSnapshot()
    {
        if (CoreContext().configuration.CharacterSnapshotPath().empty())
        {
            return true;
        }
        if (!ScenarioLoadsFixture())
        {
            LogEvent("ClientFailed", "character-snapshot-requires-fixture-load-scenario");
            return false;
        }

        std::string failure;
        if (!fable::automation::character_snapshot::
                ServerCharacterSnapshotLoader::Load(
                    CoreContext().configuration.CharacterSnapshotPath().c_str(),
                    CharacterSnapshotContext().snapshot,
                    failure))
        {
            LogEvent("ClientFailed", failure.c_str());
            return false;
        }

        CharacterSnapshotContext().configured = true;
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "server_character_id=%s display_name=%s bootstrap_save=%s region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f",
            CharacterSnapshotContext().snapshot.serverCharacterId.c_str(),
            CharacterSnapshotContext().snapshot.displayName.c_str(),
            CharacterSnapshotContext().snapshot.bootstrapSave.c_str(),
            CharacterSnapshotContext().snapshot.regionIndex,
            CharacterSnapshotContext().snapshot.position[0],
            CharacterSnapshotContext().snapshot.position[1],
            CharacterSnapshotContext().snapshot.position[2],
            CharacterSnapshotContext().snapshot.facingAngle,
            CharacterSnapshotContext().snapshot.combatHealth);
        LogEvent("CharacterSnapshotReady", detail);
        LogFormat("Server character snapshot accepted: %s (%s).",
            CharacterSnapshotContext().snapshot.displayName.c_str(),
            CharacterSnapshotContext().snapshot.serverCharacterId.c_str());
        return true;
    }
    bool ReadCharacterState(
        GameScriptInterface* gameInterface,
        ScriptThing& hero,
        CharacterState& state,
        const char*& failure)
    {
        failure = "unknown";
        if (gameInterface == nullptr || CoreContext().gameModule == nullptr)
        {
            failure = "game-interface-unavailable";
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        const auto getPosition = reinterpret_cast<ScriptThingGetPositionVector>(
            base + kScriptThingGetPositionVectorRva);
        const auto getFacing = reinterpret_cast<ScriptThingGetFacingAngle>(
            base + kScriptThingGetFacingAngleRva);
        const auto selectActiveHero = reinterpret_cast<ActiveHeroSelector>(
            base + kActiveHeroSelectorRva);
        const auto resolveCreature = reinterpret_cast<ActiveHeroCreatureResolver>(
            base + kActiveHeroCreatureResolverRva);
        const auto getProgressionHealthMaximum =
            reinterpret_cast<HeroProgressionHealthGetMaximum>(
                base + kHeroProgressionHealthGetMaximumRva);

        __try
        {
            const float* const position = getPosition(&hero);
            if (position == nullptr)
            {
                failure = "position-vector-unavailable";
                return false;
            }
            state.position[0] = position[0];
            state.position[1] = position[1];
            state.position[2] = position[2];
            state.facingAngle = getFacing(&hero);
            if (!std::isfinite(state.position[0]) ||
                !std::isfinite(state.position[1]) ||
                !std::isfinite(state.position[2]) ||
                !std::isfinite(state.facingAngle))
            {
                failure = "non-finite-transform";
                return false;
            }

            void* const regionOwner = *reinterpret_cast<void* const*>(
                reinterpret_cast<const std::uint8_t*>(gameInterface) + 0x04);
            if (regionOwner == nullptr)
            {
                failure = "region-manager-owner-unavailable";
                return false;
            }
            auto* const regionOwnerVtable = *reinterpret_cast<void***>(regionOwner);
            if (regionOwnerVtable == nullptr)
            {
                failure = "region-manager-owner-vtable-unavailable";
                return false;
            }
            const auto getRegionManager = reinterpret_cast<GetRegionManager>(
                regionOwnerVtable[0x30 / sizeof(void*)]);
            void* const regionManager = getRegionManager(regionOwner);
            if (regionManager == nullptr)
            {
                failure = "region-manager-unavailable";
                return false;
            }
            auto* const regionManagerVtable = *reinterpret_cast<void***>(regionManager);
            if (regionManagerVtable == nullptr)
            {
                failure = "region-manager-vtable-unavailable";
                return false;
            }
            const auto getRegionAtPosition = reinterpret_cast<GetRegionAtPosition>(
                regionManagerVtable[0x40 / sizeof(void*)]);
            const std::uint32_t regionToken =
                getRegionAtPosition(regionManager, state.position);
            const auto resolveRegionIndex = reinterpret_cast<ResolveRegionIndex>(
                base + kRegionManagerResolveIndexRva);
            state.regionIndex = resolveRegionIndex(regionManager, regionToken);
            if (state.regionIndex <= 0 || state.regionIndex > 1'024)
            {
                failure = "active-region-index-out-of-range";
                return false;
            }

            void* const selectorOwner = *reinterpret_cast<void* const*>(
                reinterpret_cast<const std::uint8_t*>(gameInterface) + 0x14);
            if (selectorOwner == nullptr)
            {
                failure = "active-hero-selector-unavailable";
                return false;
            }
            void* const selectedHero = selectActiveHero(selectorOwner);
            void* const creature = selectedHero == nullptr
                ? nullptr
                : resolveCreature(selectedHero);
            if (creature == nullptr)
            {
                failure = "active-hero-creature-unavailable";
                return false;
            }
            state.creature = creature;
            state.creatureVtable = *reinterpret_cast<void* const*>(creature);
            if (state.creatureVtable == nullptr)
            {
                failure = "active-hero-creature-vtable-unavailable";
                return false;
            }

            const auto expectedCreatureVtable = reinterpret_cast<void*>(
                base + kThingPlayerCreatureVtableRva);
            if (state.creatureVtable != expectedCreatureVtable)
            {
                failure = "active-hero-is-not-CThingPlayerCreature";
                return false;
            }

            const auto* const creatureBytes =
                static_cast<const std::uint8_t*>(creature);
            state.combatHealthMaximum = *reinterpret_cast<const float*>(
                creatureBytes + 0xCC);
            state.combatHealth = *reinterpret_cast<const float*>(
                creatureBytes + 0xD0);
            if (!std::isfinite(state.combatHealthMaximum) ||
                !std::isfinite(state.combatHealth) ||
                state.combatHealthMaximum <= 0.0f ||
                state.combatHealth < 0.0f ||
                state.combatHealth > state.combatHealthMaximum + 0.01f)
            {
                failure = "combat-health-values-out-of-range";
                return false;
            }

            const auto* const begin = *reinterpret_cast<CreatureComponentEntry* const*>(
                creatureBytes + 0x44);
            const auto* const end = *reinterpret_cast<CreatureComponentEntry* const*>(
                creatureBytes + 0x48);
            const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
            const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
            if (begin == nullptr || end == nullptr || endAddress < beginAddress ||
                (endAddress - beginAddress) % sizeof(CreatureComponentEntry) != 0)
            {
                failure = "creature-component-collection-invalid";
                return false;
            }
            const std::size_t componentCount =
                (endAddress - beginAddress) / sizeof(CreatureComponentEntry);
            if (componentCount > 256)
            {
                failure = "creature-component-collection-too-large";
                return false;
            }

            // Component type 4 is the Hero progression path named "Health" by
            // Fable's script API.  It is distinct from the creature's combat HP.
            void* progressionHealthComponent = nullptr;
            for (std::size_t index = 0; index < componentCount; ++index)
            {
                if (begin[index].type == 4)
                {
                    progressionHealthComponent = begin[index].component;
                    break;
                }
            }
            if (progressionHealthComponent == nullptr)
            {
                failure = "hero-progression-health-component-unavailable";
                return false;
            }

            state.progressionHealthValue = *reinterpret_cast<const int*>(
                static_cast<const std::uint8_t*>(progressionHealthComponent) + 0x30);
            state.progressionHealthMaximum =
                getProgressionHealthMaximum(progressionHealthComponent);
            if (state.progressionHealthMaximum <= 0 ||
                state.progressionHealthValue < -state.progressionHealthMaximum ||
                state.progressionHealthValue > state.progressionHealthMaximum)
            {
                failure = "hero-progression-health-values-out-of-range";
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "native-character-state-read-raised-structured-exception";
            return false;
        }

        failure = nullptr;
        return true;
    }

    bool CharacterSnapshotBaselineIsStable(const CharacterState& state)
    {
        constexpr float kTransformTolerance = 0.01f;
        constexpr float kHealthTolerance = 0.01f;
        const bool sameAsPrevious =
            CharacterSnapshotContext().baselineStableSamples != 0 &&
            state.creature == CharacterSnapshotContext().baseline.creature &&
            state.regionIndex == CharacterSnapshotContext().baseline.regionIndex &&
            std::fabs(state.position[0] -
                CharacterSnapshotContext().baseline.position[0]) <= kTransformTolerance &&
            std::fabs(state.position[1] -
                CharacterSnapshotContext().baseline.position[1]) <= kTransformTolerance &&
            std::fabs(state.position[2] -
                CharacterSnapshotContext().baseline.position[2]) <= kTransformTolerance &&
            std::fabs(state.facingAngle -
                CharacterSnapshotContext().baseline.facingAngle) <= kTransformTolerance &&
            std::fabs(state.combatHealth -
                CharacterSnapshotContext().baseline.combatHealth) <= kHealthTolerance &&
            std::fabs(state.combatHealthMaximum -
                CharacterSnapshotContext().baseline.combatHealthMaximum) <= kHealthTolerance;

        CharacterSnapshotContext().baseline = state;
        CharacterSnapshotContext().baselineStableSamples = sameAsPrevious
            ? CharacterSnapshotContext().baselineStableSamples + 1
            : 1;

        char detail[320] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "stable_samples=%u region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f combat_health_maximum=%.3f creature=%p",
            CharacterSnapshotContext().baselineStableSamples,
            state.regionIndex,
            state.position[0],
            state.position[1],
            state.position[2],
            state.facingAngle,
            state.combatHealth,
            state.combatHealthMaximum,
            state.creature);
        LogEvent("CharacterSnapshotBaselineSample", detail);
        return CharacterSnapshotContext().baselineStableSamples >= 3;
    }

    bool ApplyCharacterSnapshot(
        GameScriptInterface* gameInterface,
        ScriptThing& hero,
        const CharacterState& before,
        const char*& failure)
    {
        failure = "unknown-character-snapshot-application-failure";
        if (!CharacterSnapshotContext().configured || gameInterface == nullptr ||
            before.creature == nullptr || before.creatureVtable == nullptr)
        {
            failure = "character-snapshot-application-state-is-unavailable";
            return false;
        }
        if (CharacterSnapshotContext().snapshot.combatHealth > before.combatHealthMaximum + 0.01f)
        {
            failure = "snapshot-combat-health-exceeds-loaded-maximum";
            return false;
        }
        if (CharacterSnapshotContext().snapshot.regionIndex != before.regionIndex)
        {
            failure = "snapshot-region-does-not-match-loaded-region";
            return false;
        }

        const auto teleportThing = reinterpret_cast<TeleportThing>(
            gameInterface->vtable[kTeleportThingVtableIndex]);
        const auto* const creatureVtable = static_cast<void* const*>(
            before.creatureVtable);
        const auto modifyCombatHealth = reinterpret_cast<ModifyCombatHealth>(
            creatureVtable[kThingCreatureModifyCombatHealthVtableIndex]);
        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        if (teleportThing != reinterpret_cast<TeleportThing>(
                base + kGameScriptInterfaceTeleportThingRva) ||
            modifyCombatHealth != reinterpret_cast<ModifyCombatHealth>(
                base + kThingPlayerCreatureModifyCombatHealthRva))
        {
            failure = "character-snapshot-native-seam-validation-failed";
            return false;
        }

        __try
        {
            // These final two arguments mirror the retail TeleportThing script
            // dispatcher's default call.  No map transition is requested.
            teleportThing(
                gameInterface,
                &hero,
                CharacterSnapshotContext().snapshot.position,
                CharacterSnapshotContext().snapshot.facingAngle,
                false,
                0);

            const float healthDelta =
                CharacterSnapshotContext().snapshot.combatHealth - before.combatHealth;
            if (std::fabs(healthDelta) > 0.01f)
            {
                // Use CThingPlayerCreature's real virtual health mutation path,
                // including its normal clamping and gameplay side effects.
                modifyCombatHealth(before.creature, healthDelta, false);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "character-snapshot-native-call-raised-structured-exception";
            return false;
        }

        CharacterSnapshotContext().applied.store(true, std::memory_order_release);
        CharacterSnapshotContext().stableSamples.store(0, std::memory_order_release);
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "server_character_id=%s region_index=%d before_position=(%.6f,%.6f,%.6f) target_position=(%.6f,%.6f,%.6f) before_facing=%.6f target_facing=%.6f before_combat_health=%.3f target_combat_health=%.3f",
            CharacterSnapshotContext().snapshot.serverCharacterId.c_str(),
            before.regionIndex,
            before.position[0],
            before.position[1],
            before.position[2],
            CharacterSnapshotContext().snapshot.position[0],
            CharacterSnapshotContext().snapshot.position[1],
            CharacterSnapshotContext().snapshot.position[2],
            before.facingAngle,
            CharacterSnapshotContext().snapshot.facingAngle,
            before.combatHealth,
            CharacterSnapshotContext().snapshot.combatHealth);
        LogEvent("CharacterSnapshotApplied", detail);
        failure = nullptr;
        return true;
    }

    bool CharacterSnapshotMatches(
        const CharacterState& state,
        const char*& failure)
    {
        constexpr float kPositionTolerance = 1.5f;
        constexpr float kFacingTolerance = 0.02f;
        constexpr float kHealthTolerance = 0.01f;
        if (state.regionIndex != CharacterSnapshotContext().snapshot.regionIndex)
        {
            failure = "server-character-region-does-not-match";
            return false;
        }
        if (std::fabs(state.position[0] - CharacterSnapshotContext().snapshot.position[0]) >
                kPositionTolerance ||
            std::fabs(state.position[1] - CharacterSnapshotContext().snapshot.position[1]) >
                kPositionTolerance ||
            std::fabs(state.position[2] - CharacterSnapshotContext().snapshot.position[2]) >
                kPositionTolerance)
        {
            failure = "server-character-position-has-not-converged";
            return false;
        }
        if (std::fabs(state.facingAngle - CharacterSnapshotContext().snapshot.facingAngle) >
            kFacingTolerance)
        {
            failure = "server-character-facing-has-not-converged";
            return false;
        }
        if (std::fabs(state.combatHealth - CharacterSnapshotContext().snapshot.combatHealth) >
            kHealthTolerance)
        {
            failure = "server-character-combat-health-has-not-converged";
            return false;
        }
        failure = nullptr;
        return true;
    }

    void ObserveBootstrapHeroReadiness()
    {
        const bool bootstrapScenario = ScenarioIs(L"bootstrap_fixture_probe");
        const bool loadScenario = ScenarioLoadsFixture();
        const bool appearanceScenario = ScenarioIs(L"appearance_cycle");
        const bool multiplayerSession = CoreContext().configuration.MultiplayerEnabled();
        const bool startInvoked = multiplayerSession
            ? true
            : bootstrapScenario
            ? FrontEndContext().startInvoked.load(std::memory_order_acquire)
            : loadScenario &&
                FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire);
        const bool observationComplete = multiplayerSession
            ? CharacterSnapshotContext().heroReadyLogged.load(std::memory_order_acquire)
            : appearanceScenario
            ? AutomationContext().appearanceCycle.IsComplete()
            : bootstrapScenario
            ? CharacterSnapshotContext().heroReadyLogged.load(std::memory_order_acquire)
            : loadScenario &&
                (CharacterSnapshotContext().configured
                    ? CharacterSnapshotContext().snapshotAssertionPassed.load(std::memory_order_acquire)
                    : CharacterSnapshotContext().assertionPassed.load(std::memory_order_acquire));
        if ((!bootstrapScenario && !loadScenario && !multiplayerSession) ||
            !startInvoked ||
            observationComplete)
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG invokedAt = multiplayerSession
            ? now - std::min<ULONGLONG>(now, 5'000)
            : bootstrapScenario
            ? FrontEndContext().startInvokedAt.load(std::memory_order_acquire)
            : FrontEndContext().fixtureStartInvokedAt.load(std::memory_order_acquire);
        if (invokedAt == 0 || now - invokedAt < 5'000)
        {
            return;
        }
        ULONGLONG previousProbe =
            CharacterSnapshotContext().heroLastProbeAt.load(std::memory_order_acquire);
        if (now - previousProbe < 1'000 ||
            !CharacterSnapshotContext().heroLastProbeAt.compare_exchange_strong(
                previousProbe,
                now,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }

        GameScriptInterface* gameInterface = nullptr;
        if (!ResolveGameInterface(gameInterface))
        {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + kCharStringConstructorRva);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + kCharStringDestructorRva);
        const auto getThingWithScriptName = reinterpret_cast<GetThingWithScriptName>(
            gameInterface->vtable[kGetThingWithScriptNameVtableIndex]);
        const auto isNull = reinterpret_cast<ScriptThingIsNull>(
            base + kScriptThingIsNullRva);
        auto* const expectedScriptThingVtable = reinterpret_cast<void**>(
            base + kScriptThingVtableRva);

        constexpr char kHeroScriptName[] = "SCRIPT_NAME_HERO";
        CharString heroScriptName = {};
        ScriptThing hero = {};
        bool heroScriptNameConstructed = false;
        bool heroReady = false;
        bool characterStateReady = false;
        bool snapshotAppliedThisProbe = false;
        bool snapshotApplicationFailed = false;
        CharacterState characterState = {};
        const char* characterStateFailure = nullptr;
        void* heroImplementation = nullptr;
        CountedPointerInfo* heroPointerInfo = nullptr;
        const unsigned int probe =
            CharacterSnapshotContext().heroProbeCount.fetch_add(1, std::memory_order_acq_rel) + 1;

        __try
        {
            constructString(&heroScriptName, kHeroScriptName, -1);
            heroScriptNameConstructed = true;
            ScriptThing* const returned = getThingWithScriptName(
                gameInterface,
                &hero,
                &heroScriptName);
            heroImplementation = hero.implementation;
            heroPointerInfo = hero.pointerInfo;
            heroReady = returned == &hero &&
                hero.vtable == expectedScriptThingVtable &&
                !isNull(&hero);

            if (loadScenario && heroReady)
            {
                characterStateReady = ReadCharacterState(
                    gameInterface,
                    hero,
                    characterState,
                    characterStateFailure);
                if (characterStateReady &&
                    CharacterSnapshotContext().configured &&
                    !CharacterSnapshotContext().applied.load(std::memory_order_acquire) &&
                    CharacterSnapshotBaselineIsStable(characterState))
                {
                    snapshotAppliedThisProbe = ApplyCharacterSnapshot(
                        gameInterface,
                        hero,
                        characterState,
                        characterStateFailure);
                    snapshotApplicationFailed = !snapshotAppliedThisProbe;
                    if (snapshotApplicationFailed)
                    {
                        LogEvent(
                            "ClientFailed",
                            characterStateFailure != nullptr
                                ? characterStateFailure
                                : "unknown-character-snapshot-application-failure");
                    }
                }
            }

            if (probe <= 3 || heroReady)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "probe=%u returned=%p result=%p vtable=%p implementation=%p pointer_info=%p ready=%s",
                    probe,
                    returned,
                    &hero,
                    hero.vtable,
                    heroImplementation,
                    heroPointerInfo,
                    heroReady ? "true" : "false");
                LogEvent(
                    multiplayerSession
                        ? "MultiplayerHeroProbe"
                        : bootstrapScenario
                        ? "BootstrapHeroProbe"
                        : "FixtureLoadHeroProbe",
                    detail);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogEvent("ClientFailed", "world-Hero-lookup-raised-structured-exception");
        }

        if (appearanceScenario && heroReady && characterStateReady &&
            CharacterSnapshotContext().assertionPassed.load(std::memory_order_acquire))
        {
            const AppearanceCycleCharacterSnapshot appearanceCharacter = {
                characterState.progressionHealthValue,
                characterState.combatHealth,
                characterState.combatHealthMaximum,
                characterState.regionIndex,
                characterState.creature,
                characterState.creatureVtable,
            };
            AutomationContext().appearanceCycle.Tick(appearanceCharacter);
        }

        if (hero.vtable == expectedScriptThingVtable)
        {
            RetainTransformHandle(hero, "world Hero observation");
        }
        if (heroScriptNameConstructed)
        {
            __try
            {
                destroyString(&heroScriptName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogEvent("ClientFailed", "world-Hero-script-name-cleanup-failed");
                return;
            }
        }

        if (heroReady &&
            !CharacterSnapshotContext().heroReadyLogged.exchange(true, std::memory_order_acq_rel))
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "implementation=%p pointer_info=%p script_name=SCRIPT_NAME_HERO",
                heroImplementation,
                heroPointerInfo);
            LogEvent("HeroReady", detail);
            LogEvent(
                "WorldReady",
                multiplayerSession
                    ? "Hero object resolved after player-selected save load"
                    : bootstrapScenario
                    ? "Hero object resolved after isolated New Game bootstrap"
                    : "Hero object resolved after exact isolated AutoSave load");
            GameplayContext().runtime.DispatchWorldReady();
        }

        if (loadScenario && heroReady)
        {
            if (snapshotApplicationFailed || snapshotAppliedThisProbe)
            {
                return;
            }
            if (!characterStateReady)
            {
                if (probe <= 3)
                {
                    LogEvent(
                        "CharacterStateUnavailable",
                        characterStateFailure != nullptr
                            ? characterStateFailure
                            : "unknown");
                }
                return;
            }
            if (CharacterSnapshotContext().configured &&
                !CharacterSnapshotContext().applied.load(std::memory_order_acquire))
            {
                return;
            }

            if (CharacterSnapshotContext().configured)
            {
                const unsigned int attempt =
                    CharacterSnapshotContext().verificationAttempts.fetch_add(
                        1,
                        std::memory_order_acq_rel) + 1;
                const char* snapshotFailure = nullptr;
                if (!CharacterSnapshotMatches(characterState, snapshotFailure))
                {
                    CharacterSnapshotContext().stableSamples.store(0, std::memory_order_release);
                    if (attempt <= 5)
                    {
                        char detail[384] = {};
                        std::snprintf(
                            detail,
                            std::size(detail),
                            "attempt=%u reason=%s observed_position=(%.6f,%.6f,%.6f) observed_facing=%.6f observed_combat_health=%.3f",
                            attempt,
                            snapshotFailure != nullptr ? snapshotFailure : "unknown",
                            characterState.position[0],
                            characterState.position[1],
                            characterState.position[2],
                            characterState.facingAngle,
                            characterState.combatHealth);
                        LogEvent("CharacterSnapshotVerificationPending", detail);
                    }
                    return;
                }
            }

            const unsigned int sample =
                CharacterSnapshotContext().stableSamples.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            char detail[384] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "sample=%u region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f combat_health_maximum=%.3f progression_health=%d progression_health_maximum=%d creature=%p creature_vtable=%p",
                sample,
                characterState.regionIndex,
                characterState.position[0],
                characterState.position[1],
                characterState.position[2],
                characterState.facingAngle,
                characterState.combatHealth,
                characterState.combatHealthMaximum,
                characterState.progressionHealthValue,
                characterState.progressionHealthMaximum,
                characterState.creature,
                characterState.creatureVtable);
            LogEvent("CharacterStateSample", detail);
            if (CharacterSnapshotContext().configured)
            {
                LogEvent("CharacterSnapshotSample", detail);
            }

            if (sample >= 3)
            {
                if (!CharacterSnapshotContext().assertionPassed.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    LogEvent(
                        "AssertionPassed",
                        CharacterSnapshotContext().configured
                            ? "three consecutive server-character target transform and combat-health samples"
                            : "three consecutive finite Hero transform, combat-health, progression, and active-creature samples");
                }
                if (CharacterSnapshotContext().configured &&
                    !CharacterSnapshotContext().snapshotAssertionPassed.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    char snapshotDetail[256] = {};
                    std::snprintf(
                        snapshotDetail,
                        std::size(snapshotDetail),
                        "server_character_id=%s display_name=%s samples=%u",
                        CharacterSnapshotContext().snapshot.serverCharacterId.c_str(),
                        CharacterSnapshotContext().snapshot.displayName.c_str(),
                        sample);
                    LogEvent("CharacterSnapshotAssertionPassed", snapshotDetail);
                }
            }
        }
    }

}

namespace
{
    using namespace fable::core::bootstrap;

    bool CharacterSnapshotFeatureEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallCharacterSnapshotFeature(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return true;
        }
        const auto& configuration = CoreContext().configuration;
        const bool appearance =
            configuration.Mode() == fable::automation::runtime::ClientMode::AppearanceCycle ||
            ScenarioIs(L"appearance_cycle");
        LogEvent(
            "ClientReady",
            configuration.Mode() == fable::automation::runtime::ClientMode::TransformProbe
                ? "transform_probe"
                : appearance ? "appearance_cycle" : "observe");
        return fable::automation::character_snapshot::InitializeCharacterSnapshot();
    }

    void UninstallCharacterSnapshotFeature(FeatureContext&) noexcept {}

    FABLE_FEATURE_DEPENDENCIES(characterSnapshotDependencies, "gameplay.runtime");
    FABLE_FEATURE_DESCRIPTOR(
        fableCharacterSnapshotFeature,
        "automation.character-snapshot",
        "Character snapshot automation",
        FeaturePhase::Runtime,
        30,
        CharacterSnapshotFeatureEnabled,
        characterSnapshotDependencies,
        std::size(characterSnapshotDependencies),
        InstallCharacterSnapshotFeature,
        UninstallCharacterSnapshotFeature,
        "character-snapshot-initialization");
}
