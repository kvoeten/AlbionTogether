#include "CombatTargetAcceptanceDriver.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Creature/CreatureService.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Game/NPC/NpcService.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

namespace
{
    constexpr char TargetDefinition[] = "CREATURE_HOBBE_GRUNT";
    constexpr char TargetScriptName[] =
        "SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";
    constexpr char RemotePlayerScriptName[] =
        "SCRIPT_NAME_FABLE_TOGETHER_REMOTE_PLAYER";
    constexpr char ArenaMap[] = "FrescoDome";
    constexpr float Tau = 6.28318530717958647692f;
    constexpr float SpawnDistance = 1.5f;
    constexpr std::uint64_t PeerStagingDelayMilliseconds = 2'000;
    constexpr std::uint64_t RosterSettleMilliseconds = 1'000;
    constexpr std::uint64_t RetryMilliseconds = 1'000;
    constexpr std::uint64_t GuestHealthMutationDelayMilliseconds = 2'000;
    constexpr std::uint64_t TargetHealthMutationDelayMilliseconds = 2'500;
    constexpr std::uint64_t HealthMutationRetryMilliseconds = 500;
    constexpr std::uint64_t TargetMaintenanceIntervalMilliseconds = 100;
    constexpr float TargetMaintenanceThreshold = 0.75f;
    constexpr unsigned int RequiredTargetHealthMutations = 2;
    constexpr unsigned int MaximumAttempts = 8;
    constexpr unsigned int HeroMeleeAttackAbility = 1101;
    constexpr std::uint64_t WeaponStateRetryMilliseconds = 250;
    constexpr std::uint64_t AttackAfterWeaponReadyMilliseconds = 750;
    constexpr std::uint64_t TargetedAttackAfterUntargetedMilliseconds =
        2'500;
    constexpr unsigned int RequiredSustainedAttacks = 6;
    constexpr std::uint64_t SustainedAttackIntervalMilliseconds = 900;
    // Leave the final stowed presentation visible long enough for automated
    // screenshot inspection; this is acceptance-only pacing, not gameplay.
    constexpr std::uint64_t WeaponTransitionSettleMilliseconds = 3'000;
}

namespace fable::automation::multiplayer::combat
{
    void CombatTargetAcceptanceDriver::Initialize(
        bool enabled,
        bool spawnTarget,
        bool targetOnly,
        game::EntityService& entities,
        game::CreatureService& creatures,
        game::creature::combat::CreatureCombatService& combat,
        game::NpcService& npcs,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        creatures_ = &creatures;
        combat_ = &combat;
        npcs_ = &npcs;
        diagnostics_ = diagnostics;
        enabled_ = enabled;
        spawnTarget_ = spawnTarget;
        targetOnly_ = targetOnly;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerCombatTargetAcceptanceArmed",
                spawnTarget_
                    ? "host will create one authoritative hostile creature after both player presentations are ready"
                    : "guest will arm the replicated hostile creature through Fable's native Hero targeting component");
        }
    }

    void CombatTargetAcceptanceDriver::Tick(bool remotePresentationReady)
    {
        if (!enabled_ || !remotePresentationReady ||
            entities_ == nullptr || npcs_ == nullptr)
        {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (spawnTarget_ && completed_)
        {
            if (maintainTargetHealth_)
            {
                MaintainAcceptanceTargetHealth(now);
            }
            return;
        }
        if (armedAt_ == 0)
        {
            armedAt_ = now;
            return;
        }

        // The launcher separates the otherwise overlapping fixture Heroes
        // through normal W input once both processes are fully in-world.
        // Do not snapshot the saved transform or spawn the target before that
        // single input nudge has completed.
        if (!peerStaged_ && now - armedAt_ < PeerStagingDelayMilliseconds)
        {
            return;
        }

        if (!peerStaged_)
        {
            game::Entity* const hero = entities_->GetHero();
            if (hero == nullptr || !hero->IsValid())
            {
                if (hero != nullptr)
                {
                    hero->Release();
                }
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }

            const std::string currentMap = hero->GetCurrentMapName();
            if (currentMap != ArenaMap)
            {
                hero->Release();
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }

            const game::Vector3 peerPosition = hero->GetPosition();
            const float facing = hero->GetFacing();
            const std::string map = hero->GetCurrentMapName();
            hero->Release();
            if (map != ArenaMap)
            {
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }

            peerStaged_ = true;
            armedAt_ = now;
            nextAttemptAt_ = 0;
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s map=%s position=(%.3f,%.3f,%.3f) facing=%.6f arena=chamber-of-fate source=native-save",
                spawnTarget_ ? "host" : "guest",
                map.c_str(),
                peerPosition.x,
                peerPosition.y,
                peerPosition.z,
                facing);
            diagnostics_.Event("MultiplayerCombatPeerStaged", detail);
            return;
        }

        if (now - armedAt_ < RosterSettleMilliseconds ||
            (nextAttemptAt_ != 0 && now < nextAttemptAt_))
        {
            return;
        }

        if (!arenaConverged_)
        {
            game::Entity* const remote =
                entities_->FindByScriptName(RemotePlayerScriptName);
            const bool converged = remote != nullptr && remote->IsValid() &&
                remote->GetCurrentMapName() == ArenaMap;
            const game::Vector3 remotePosition = converged
                ? remote->GetPosition()
                : game::Vector3{};
            if (remote != nullptr)
            {
                remote->Release();
            }
            if (!converged)
            {
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }

            arenaConverged_ = true;
            nextAttemptAt_ = 0;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s map=%s remote_position=(%.3f,%.3f,%.3f) same_map=true",
                spawnTarget_ ? "host" : "guest",
                ArenaMap,
                remotePosition.x,
                remotePosition.y,
                remotePosition.z);
            diagnostics_.Event("MultiplayerCombatArenaConverged", detail);
        }

        if (!spawnTarget_)
        {
            if (target_ == nullptr)
            {
                target_ = entities_->FindByScriptName(TargetScriptName);
            }
            game::Entity* const hero = entities_->GetHero();
            if (hero == nullptr || !hero->IsValid() || target_ == nullptr ||
                !target_->IsValid())
            {
                if (hero != nullptr)
                {
                    hero->Release();
                }
                if (target_ != nullptr && !target_->IsValid())
                {
                    target_->Release();
                    target_ = nullptr;
                }
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }

            // The combat target exists to receive replicated player actions,
            // not to start a town-wide faction fight. Apply this locally as
            // well as on the host because the guest materializes a fresh Thing.
            target_->SetFriendsWithEverything(true);

            void* const heroThing = entities_->ResolveNative(
                hero->NativeHandle());
            void* const targetThing = entities_->ResolveNative(
                target_->NativeHandle());
            void* const targeting = game::entity::native::ThingComponentAccess::
                Find(
                    heroThing,
                    game::entity::native::ThingComponentType::Targeting);
            const bool armed = targetArmed_ ||
                game::creature::combat::native::HeroTargetingComponent::
                    AssignSelectedTarget(
                        entities_->GameModule(),
                        targeting,
                        targetThing);
            if (!armed)
            {
                hero->Release();
                nextAttemptAt_ = now + RetryMilliseconds;
                return;
            }
            nextAttemptAt_ = 0;
            if (!targetArmed_)
            {
                targetArmed_ = true;
                nextHealthMutationAt_ =
                    now + GuestHealthMutationDelayMilliseconds;
                nextTargetHealthMutationAt_ =
                    now + TargetHealthMutationDelayMilliseconds;
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "thing_uid=%016llX hero=%p targeting=%p target=%p assignment=retail-selected-target-setter",
                    static_cast<unsigned long long>(target_->GetUid()),
                    heroThing,
                    targeting,
                    targetThing);
                diagnostics_.Event("MultiplayerCombatTargetArmed", detail);
            }
            if (targetOnly_)
            {
                hero->Release();
                return;
            }

            using game::creature::equipment::CreatureWeaponFamily;
            using game::hero_pawn::equipment::HeroEquipmentState;
            using game::hero_pawn::equipment::native::HeroWeaponComponent;
            HeroEquipmentState equipment;
            const bool equipmentRead = HeroWeaponComponent::Capture(
                heroThing, equipment);
            if (!meleeRequested_ && equipmentRead && equipment.IsSane() &&
                equipment.meleeDefinitionIndex > 0)
            {
                meleeRequested_ = HeroWeaponComponent::RequestActiveFamily(
                    entities_->Interface(),
                    hero->NativeHandle(),
                    CreatureWeaponFamily::Melee);
                if (meleeRequested_)
                {
                    meleeRequestedAt_ = now;
                    nextAttackAttemptAt_ =
                        now + WeaponStateRetryMilliseconds;
                    char detail[192] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "hero=%p melee_definition=%d source=retail-hero-unsheathe-wrapper",
                        heroThing,
                        equipment.meleeDefinitionIndex);
                    diagnostics_.Event(
                        "MultiplayerCombatNativeMeleeRequested", detail);
                }
            }
            if (meleeRequested_ && !meleeReady_ && equipmentRead &&
                equipment.activeFamily == CreatureWeaponFamily::Melee)
            {
                meleeReady_ = true;
                meleeReadyAt_ = now;
                nextAttackAttemptAt_ =
                    now + AttackAfterWeaponReadyMilliseconds;
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "hero=%p melee_definition=%d source=hero-carrying-observer",
                    heroThing,
                    equipment.meleeDefinitionIndex);
                diagnostics_.Event(
                    "MultiplayerCombatNativeMeleeReady", detail);
            }
            if (meleeReady_ && !untargetedAttackSubmitted_ &&
                combat_ != nullptr && now >= nextAttackAttemptAt_)
            {
                // First prove the Hero-style attack semantic is independent
                // of an enemy. Clear all native target weak references before
                // submission, then clear once more before capture in case the
                // local Hero's factory opportunistically reacquired the nearby
                // fixture target while constructing its action.
                const bool clearedBefore =
                    game::creature::combat::native::HeroTargetingComponent::
                        ClearTargets(entities_->GameModule(), targeting);
                const bool submitted = clearedBefore &&
                    combat_->SubmitReplicatedAbility(
                        heroThing, HeroMeleeAttackAbility, 0.0f);
                const bool clearedAfter = submitted &&
                    game::creature::combat::native::HeroTargetingComponent::
                        ClearTargets(entities_->GameModule(), targeting);
                if (submitted && clearedAfter)
                {
                    combat_->ObservePlayerAbility(
                        heroThing,
                        HeroMeleeAttackAbility,
                        0.0f,
                        true);
                    untargetedAttackSubmitted_ = true;
                    nextAttackAttemptAt_ =
                        now + TargetedAttackAfterUntargetedMilliseconds;
                    diagnostics_.Event(
                        "MultiplayerCombatNativeUntargetedAttackSubmitted",
                        "ability_id=1101 weapon=melee target=null source=native-creature-ability");
                }
                else
                {
                    nextAttackAttemptAt_ =
                        now + WeaponStateRetryMilliseconds;
                }
            }
            if (meleeReady_ && untargetedAttackSubmitted_ &&
                !nativeAttackSubmitted_ && combat_ != nullptr &&
                now >= nextAttackAttemptAt_)
            {
                const bool submitted = combat_->SubmitReplicatedAbility(
                    heroThing, HeroMeleeAttackAbility, 0.0f);
                if (submitted)
                {
                    // SubmitReplicatedAbility deliberately bypasses capture to
                    // prevent remote echo. This acceptance-only source is a
                    // genuine local Hero request, so publish the observation
                    // after the native command accepted it.
                    combat_->ObservePlayerAbility(
                        heroThing,
                        HeroMeleeAttackAbility,
                        0.0f,
                        true);
                    nativeAttackSubmitted_ = true;
                    nextAttackAttemptAt_ =
                        now + SustainedAttackIntervalMilliseconds;
                    char detail[192] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "hero=%p ability_id=%u weapon=melee target=%p source=native-creature-ability",
                        heroThing,
                        HeroMeleeAttackAbility,
                        targetThing);
                    diagnostics_.Event(
                        "MultiplayerCombatNativeAttackSubmitted", detail);
                }
                else
                {
                    nextAttackAttemptAt_ =
                        now + WeaponStateRetryMilliseconds;
                }
            }
            if (meleeReady_ && nativeAttackSubmitted_ &&
                sustainedAttackCount_ < RequiredSustainedAttacks &&
                combat_ != nullptr && now >= nextAttackAttemptAt_)
            {
                // Keep exercising the ordered action mailbox after the first
                // successful pair. These attacks are deliberately untargeted:
                // the restrained fixture cannot knock either Hero around, and
                // the test catches a stale action/equipment dependency that
                // would otherwise make replication silently stop over time.
                const bool clearedBefore =
                    game::creature::combat::native::HeroTargetingComponent::
                        ClearTargets(entities_->GameModule(), targeting);
                const bool submitted = clearedBefore &&
                    combat_->SubmitReplicatedAbility(
                        heroThing, HeroMeleeAttackAbility, 0.0f);
                const bool clearedAfter = submitted &&
                    game::creature::combat::native::HeroTargetingComponent::
                        ClearTargets(entities_->GameModule(), targeting);
                if (submitted && clearedAfter)
                {
                    combat_->ObservePlayerAbility(
                        heroThing,
                        HeroMeleeAttackAbility,
                        0.0f,
                        true);
                    ++sustainedAttackCount_;
                    nextAttackAttemptAt_ =
                        now + SustainedAttackIntervalMilliseconds;
                    char detail[192] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "ordinal=%u/%u ability_id=%u weapon=melee target=null source=native-creature-ability",
                        sustainedAttackCount_,
                        RequiredSustainedAttacks,
                        HeroMeleeAttackAbility);
                    diagnostics_.Event(
                        "MultiplayerCombatNativeSustainedAttackSubmitted",
                        detail);
                }
                else
                {
                    nextAttackAttemptAt_ =
                        now + WeaponStateRetryMilliseconds;
                }
            }
            if (sustainedAttackCount_ >= RequiredSustainedAttacks &&
                !sheatheRequested_ && equipmentRead &&
                now >= nextAttackAttemptAt_)
            {
                sheatheRequested_ = HeroWeaponComponent::RequestActiveFamily(
                    entities_->Interface(),
                    hero->NativeHandle(),
                    CreatureWeaponFamily::None);
                if (sheatheRequested_)
                {
                    nextWeaponTransitionAt_ =
                        now + WeaponStateRetryMilliseconds;
                    diagnostics_.Event(
                        "MultiplayerCombatNativeMeleeStowRequested",
                        "source=retail-hero-sheathe-wrapper");
                }
            }
            if (sheatheRequested_ && !sheatheReady_ && equipmentRead &&
                now >= nextWeaponTransitionAt_ &&
                equipment.activeFamily == CreatureWeaponFamily::None)
            {
                sheatheReady_ = true;
                nextWeaponTransitionAt_ =
                    now + WeaponTransitionSettleMilliseconds;
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "melee=%d melee_slot=%u source=owner-final-carry-state",
                    equipment.meleeDefinitionIndex,
                    equipment.meleeAttachmentSlot);
                diagnostics_.Event(
                    "MultiplayerCombatNativeMeleeStowed", detail);
            }
            if (sheatheReady_ && !redrawRequested_ && equipmentRead &&
                now >= nextWeaponTransitionAt_)
            {
                redrawRequested_ = HeroWeaponComponent::RequestActiveFamily(
                    entities_->Interface(),
                    hero->NativeHandle(),
                    CreatureWeaponFamily::Melee);
                if (redrawRequested_)
                {
                    nextWeaponTransitionAt_ =
                        now + WeaponStateRetryMilliseconds;
                    diagnostics_.Event(
                        "MultiplayerCombatNativeMeleeRedrawRequested",
                        "source=retail-hero-unsheathe-wrapper");
                }
            }
            if (redrawRequested_ && !redrawReady_ && equipmentRead &&
                now >= nextWeaponTransitionAt_ &&
                equipment.activeFamily == CreatureWeaponFamily::Melee)
            {
                redrawReady_ = true;
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "melee=%d melee_slot=%u source=owner-final-carry-state",
                    equipment.meleeDefinitionIndex,
                    equipment.meleeAttachmentSlot);
                diagnostics_.Event(
                    "MultiplayerCombatNativeMeleeRedrawReady", detail);
            }

            // Exercise player-vitals replication deterministically through
            // Fable's real CThingCreature health mutation vtable boundary.
            // Enemy retaliation remains native gameplay, but is not a stable
            // acceptance precondition because AI may select the host instead.
            if (!healthMutationApplied_ && creatures_ != nullptr &&
                nextHealthMutationAt_ != 0 && now >= nextHealthMutationAt_)
            {
                const float previous = creatures_->GetHealth(hero);
                const float maximum = creatures_->GetMaximumHealth(hero);
                float requested = previous - 1.0f;
                if (previous <= 1.0f && maximum > previous)
                {
                    requested = previous + 1.0f;
                }
                const bool changed = std::isfinite(previous) &&
                    std::isfinite(maximum) && maximum > 0.0f &&
                    requested >= 0.0f && requested <= maximum &&
                    std::fabs(requested - previous) > 0.0001f &&
                    creatures_->SetHealth(hero, requested);
                if (changed)
                {
                    healthMutationApplied_ = true;
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "source=native-creature-health-setter previous=%.3f current=%.3f maximum=%.3f",
                        previous,
                        requested,
                        maximum);
                    diagnostics_.Event(
                        "MultiplayerCombatGuestHealthMutationApplied",
                        detail);
                }
                else
                {
                    nextHealthMutationAt_ =
                        now + HealthMutationRetryMilliseconds;
                }
            }

            // Weapon collision is deliberately not an acceptance precondition:
            // the selected-target and attack-action checks cover Fable's combat
            // path, while these real native mutations prove that the guest's
            // temporary NPC authority can publish ordered vitals revisions back
            // to the host without fabricating protocol messages.
            if (creatures_ != nullptr &&
                targetHealthMutations_ < RequiredTargetHealthMutations &&
                nextTargetHealthMutationAt_ != 0 &&
                now >= nextTargetHealthMutationAt_)
            {
                const float previous = creatures_->GetHealth(target_);
                const float maximum = creatures_->GetMaximumHealth(target_);
                float requested = previous - 1.0f;
                if (previous <= 1.0f && maximum > previous)
                {
                    requested = previous + 1.0f;
                }
                const bool changed = std::isfinite(previous) &&
                    std::isfinite(maximum) && maximum > 0.0f &&
                    requested >= 0.0f && requested <= maximum &&
                    std::fabs(requested - previous) > 0.0001f &&
                    creatures_->SetHealth(target_, requested);
                if (changed)
                {
                    ++targetHealthMutations_;
                    nextTargetHealthMutationAt_ =
                        now + HealthMutationRetryMilliseconds;
                    char detail[320] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "thing_uid=%016llX ordinal=%u/%u source=native-creature-health-setter previous=%.3f current=%.3f maximum=%.3f",
                        static_cast<unsigned long long>(target_->GetUid()),
                        targetHealthMutations_,
                        RequiredTargetHealthMutations,
                        previous,
                        requested,
                        maximum);
                    diagnostics_.Event(
                        "MultiplayerCombatTargetHealthMutationApplied",
                        detail);
                }
                else
                {
                    nextTargetHealthMutationAt_ =
                        now + HealthMutationRetryMilliseconds;
                }
            }
            hero->Release();
            return;
        }

        game::Entity* const hero = entities_->GetHero();
        if (hero == nullptr || !hero->IsValid())
        {
            if (hero != nullptr)
            {
                hero->Release();
            }
            nextAttemptAt_ = now + RetryMilliseconds;
            return;
        }

        const game::Vector3 heroPosition = hero->GetPosition();
        const float heroFacing = hero->GetFacing();
        const std::string map = hero->GetCurrentMapName();
        hero->Release();
        if (map != ArenaMap || !std::isfinite(heroPosition.x) ||
            !std::isfinite(heroPosition.y) ||
            !std::isfinite(heroPosition.z) || !std::isfinite(heroFacing))
        {
            nextAttemptAt_ = now + RetryMilliseconds;
            return;
        }

        const float radians = heroFacing * Tau;
        game::Vector3 targetPosition = {
            heroPosition.x + std::sin(radians) * SpawnDistance,
            heroPosition.y + std::cos(radians) * SpawnDistance,
            heroPosition.z,
        };
        game::Entity* const remote =
            entities_->FindByScriptName(RemotePlayerScriptName);
        if (remote != nullptr && remote->IsValid() &&
            remote->GetCurrentMapName() == map)
        {
            const game::Vector3 remotePosition = remote->GetPosition();
            if (std::isfinite(remotePosition.x) &&
                std::isfinite(remotePosition.y) &&
                std::isfinite(remotePosition.z))
            {
                targetPosition = {
                    (heroPosition.x + remotePosition.x) * 0.5f,
                    (heroPosition.y + remotePosition.y) * 0.5f,
                    (heroPosition.z + remotePosition.z) * 0.5f,
                };
            }
        }
        if (remote != nullptr)
        {
            remote->Release();
        }
        ++attempts_;
        target_ = npcs_->Spawn(
            TargetDefinition,
            targetPosition,
            TargetScriptName);
        if (target_ == nullptr || !target_->IsValid())
        {
            if (target_ != nullptr)
            {
                target_->Release();
                target_ = nullptr;
            }
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "attempt=%u/%u definition=%s",
                attempts_,
                MaximumAttempts,
                TargetDefinition);
            diagnostics_.Event("MultiplayerCombatTargetSpawnDeferred", detail);
            if (attempts_ >= MaximumAttempts)
            {
                completed_ = true;
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-combat-target-create-failed");
                return;
            }
            nextAttemptAt_ = now + RetryMilliseconds;
            return;
        }

        target_->SetKillOnLevelUnload(true);
        // CreateCreature's temporary result is reclaimed when no retail script
        // owns it. The script counter pins its lifetime; the explicit control
        // layer keeps this player-action fixture from joining town faction AI.
        scriptRetained_ = target_->IncrementScriptCounter();
        hostSpawnControl_ = target_->AcquireControl(game::AiPriority::Highest);
        const bool spawnRestrained = hostSpawnControl_ != nullptr &&
            hostSpawnControl_->IsValid() &&
            hostSpawnControl_->ClearAllActions(true);
        if (!spawnRestrained && hostSpawnControl_ != nullptr)
        {
            hostSpawnControl_->ReleaseControl();
            hostSpawnControl_->Release();
            hostSpawnControl_ = nullptr;
        }
        target_->SetFriendsWithEverything(true);
        target_->SetAttackable(true);
        // Keep the retail combat traits explicit so player targeting, damage,
        // hit reactions, and authority transfer still exercise real creature
        // code while autonomous retaliation remains outside this test.
        target_->SetDamageable(true);
        target_->SetCollidable(true);
        target_->SetDrawable(true);
        target_->Teleport(targetPosition, 0.0f, false);

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX definition=%s script_name=%s map=%s position=(%.3f,%.3f,%.3f) friendly=true spawn_restrained=%s script_retained=%s script_counter=%d",
            static_cast<unsigned long long>(target_->GetUid()),
            TargetDefinition,
            TargetScriptName,
            map.c_str(),
            targetPosition.x,
            targetPosition.y,
            targetPosition.z,
            spawnRestrained ? "true" : "false",
            scriptRetained_ ? "true" : "false",
            target_->GetScriptCounter());
        diagnostics_.Event("MultiplayerCombatTargetSpawned", detail);
        completed_ = true;
    }

    void CombatTargetAcceptanceDriver::AllowTargetDeath() noexcept
    {
        if (!enabled_ || !spawnTarget_ || !maintainTargetHealth_)
        {
            return;
        }
        maintainTargetHealth_ = false;
        diagnostics_.Event(
            "MultiplayerCombatTargetDeathAllowed",
            "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET maintenance=disabled");
    }

    void CombatTargetAcceptanceDriver::MaintainAcceptanceTargetHealth(
        std::uint64_t now) noexcept
    {
        if (now < nextTargetMaintenanceAt_ || creatures_ == nullptr ||
            target_ == nullptr || !target_->IsValid())
        {
            return;
        }
        nextTargetMaintenanceAt_ =
            now + TargetMaintenanceIntervalMilliseconds;

        const float current = creatures_->GetHealth(target_);
        const float maximum = creatures_->GetMaximumHealth(target_);
        if (!std::isfinite(current) || !std::isfinite(maximum) ||
            maximum <= 0.0f || current >= maximum * TargetMaintenanceThreshold)
        {
            return;
        }

        if (!creatures_->SetHealth(target_, maximum))
        {
            return;
        }

        ++targetHealthRestorations_;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX ordinal=%u source=host-acceptance-fixture previous=%.3f current=%.3f maximum=%.3f",
            static_cast<unsigned long long>(target_->GetUid()),
            targetHealthRestorations_,
            current,
            maximum,
            maximum);
        diagnostics_.Event(
            "MultiplayerAcceptanceTargetHealthRestored", detail);
    }

    bool CombatTargetAcceptanceDriver::IsTargetReady() const noexcept
    {
        return enabled_ && arenaConverged_ &&
            (spawnTarget_ ? completed_ : targetArmed_);
    }

    void CombatTargetAcceptanceDriver::Shutdown() noexcept
    {
        if (hostSpawnControl_ != nullptr)
        {
            hostSpawnControl_->ReleaseControl();
            hostSpawnControl_->Release();
        }
        if (target_ != nullptr)
        {
            if (scriptRetained_ && target_->IsValid())
            {
                target_->DecrementScriptCounter();
            }
            target_->Release();
        }
        entities_ = nullptr;
        creatures_ = nullptr;
        combat_ = nullptr;
        npcs_ = nullptr;
        target_ = nullptr;
        hostSpawnControl_ = nullptr;
        diagnostics_ = {};
        armedAt_ = 0;
        nextHealthMutationAt_ = 0;
        nextTargetHealthMutationAt_ = 0;
        nextTargetMaintenanceAt_ = 0;
        nextAttemptAt_ = 0;
        meleeRequestedAt_ = 0;
        meleeReadyAt_ = 0;
        nextAttackAttemptAt_ = 0;
        nextWeaponTransitionAt_ = 0;
        attempts_ = 0;
        targetHealthMutations_ = 0;
        targetHealthRestorations_ = 0;
        sustainedAttackCount_ = 0;
        scriptRetained_ = false;
        peerStaged_ = false;
        arenaConverged_ = false;
        spawnTarget_ = false;
        targetOnly_ = false;
        targetArmed_ = false;
        meleeRequested_ = false;
        meleeReady_ = false;
        untargetedAttackSubmitted_ = false;
        nativeAttackSubmitted_ = false;
        sheatheRequested_ = false;
        sheatheReady_ = false;
        redrawRequested_ = false;
        redrawReady_ = false;
        healthMutationApplied_ = false;
        maintainTargetHealth_ = true;
        enabled_ = false;
        completed_ = false;
    }
}
