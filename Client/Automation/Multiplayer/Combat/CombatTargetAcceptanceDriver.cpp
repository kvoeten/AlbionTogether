#include "CombatTargetAcceptanceDriver.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Creature/CreatureService.h"
#include "Game/Creature/Combat/Native/HeroTargetingComponent.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/NPC/NpcService.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

namespace
{
    constexpr char TargetDefinition[] = "CREATURE_HOBBE_GRUNT";
    constexpr char TargetScriptName[] =
        "SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";
    constexpr float Tau = 6.28318530717958647692f;
    constexpr float SpawnDistance = 1.25f;
    constexpr std::uint64_t RosterSettleMilliseconds = 1'000;
    constexpr std::uint64_t RetryMilliseconds = 1'000;
    constexpr std::uint64_t GuestHealthMutationDelayMilliseconds = 2'000;
    constexpr std::uint64_t TargetHealthMutationDelayMilliseconds = 2'500;
    constexpr std::uint64_t HealthMutationRetryMilliseconds = 500;
    constexpr unsigned int RequiredTargetHealthMutations = 2;
    constexpr std::uint64_t HostSpawnControlMilliseconds = 12'000;
    constexpr unsigned int MaximumAttempts = 8;
}

namespace fable::automation::multiplayer::combat
{
    void CombatTargetAcceptanceDriver::Initialize(
        bool enabled,
        bool spawnTarget,
        game::EntityService& entities,
        game::CreatureService& creatures,
        game::NpcService& npcs,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        creatures_ = &creatures;
        npcs_ = &npcs;
        diagnostics_ = diagnostics;
        enabled_ = enabled;
        spawnTarget_ = spawnTarget;
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
            if (hostSpawnControl_ != nullptr &&
                releaseHostSpawnControlAt_ != 0 &&
                now >= releaseHostSpawnControlAt_)
            {
                const bool released = hostSpawnControl_->ReleaseControl();
                hostSpawnControl_->Release();
                hostSpawnControl_ = nullptr;
                releaseHostSpawnControlAt_ = 0;
                diagnostics_.Event(
                    "MultiplayerCombatTargetSpawnControlReleased",
                    released
                        ? "host spawn restraint released after guest targeting window"
                        : "host spawn restraint handle was retired after native release failed");
            }
            return;
        }
        if (armedAt_ == 0)
        {
            armedAt_ = now;
            return;
        }
        if (now - armedAt_ < RosterSettleMilliseconds ||
            (nextAttemptAt_ != 0 && now < nextAttemptAt_))
        {
            return;
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

            void* const heroThing = entities_->ResolveNative(
                hero->NativeHandle());
            void* const targetThing = entities_->ResolveNative(
                target_->NativeHandle());
            void* const targeting = game::entity::native::ThingComponentAccess::
                Find(
                    heroThing,
                    game::entity::native::ThingComponentType::Targeting);
            const bool candidateArmed = game::creature::combat::native::
                HeroTargetingComponent::AssignCandidatePrimary(
                    entities_->GameModule(),
                    targeting,
                    targetThing);
            const bool selectedArmed = game::creature::combat::native::
                HeroTargetingComponent::AssignSelectedTarget(
                    entities_->GameModule(),
                    targeting,
                    targetThing);
            const bool armed = candidateArmed && selectedArmed;
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
                    "thing_uid=%016llX hero=%p targeting=%p target=%p assignment=retail-selected-and-candidate-weak-references",
                    static_cast<unsigned long long>(target_->GetUid()),
                    heroThing,
                    targeting,
                    targetThing);
                diagnostics_.Event("MultiplayerCombatTargetArmed", detail);
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
        if (map.empty() || !std::isfinite(heroPosition.x) ||
            !std::isfinite(heroPosition.y) ||
            !std::isfinite(heroPosition.z) || !std::isfinite(heroFacing))
        {
            nextAttemptAt_ = now + RetryMilliseconds;
            return;
        }

        const float radians = heroFacing * Tau;
        const game::Vector3 targetPosition = {
            heroPosition.x + std::sin(radians) * SpawnDistance,
            heroPosition.y + std::cos(radians) * SpawnDistance,
            heroPosition.z,
        };
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
        // CreateCreature's temporary result is reclaimed when no retail
        // script owns it. The script counter is the lightweight lifetime pin:
        // unlike StartScriptingEntity it does not install a scripted-control
        // layer, so the hostile creature's native AI brain remains authoritative.
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
        if (spawnRestrained)
        {
            releaseHostSpawnControlAt_ =
                now + HostSpawnControlMilliseconds;
        }
        target_->SetAttackable(true);
        // Keep the retail combat traits explicit so this fixture exercises
        // the same target selection, damage, and retaliation path as a normal
        // hostile creature rather than relying on a neutral guard becoming
        // hostile after an unobserved first hit.
        target_->SetDamageable(true);
        target_->SetCollidable(true);
        target_->SetDrawable(true);
        target_->Teleport(targetPosition, heroFacing + 0.5f, false);

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX definition=%s script_name=%s map=%s position=(%.3f,%.3f,%.3f) hero_facing=%.6f script_retained=%s spawn_restrained=%s script_counter=%d",
            static_cast<unsigned long long>(target_->GetUid()),
            TargetDefinition,
            TargetScriptName,
            map.c_str(),
            targetPosition.x,
            targetPosition.y,
            targetPosition.z,
            heroFacing,
            scriptRetained_ ? "true" : "false",
            spawnRestrained ? "true" : "false",
            target_->GetScriptCounter());
        diagnostics_.Event("MultiplayerCombatTargetSpawned", detail);
        completed_ = true;
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
        npcs_ = nullptr;
        target_ = nullptr;
        hostSpawnControl_ = nullptr;
        diagnostics_ = {};
        armedAt_ = 0;
        releaseHostSpawnControlAt_ = 0;
        nextHealthMutationAt_ = 0;
        nextTargetHealthMutationAt_ = 0;
        nextAttemptAt_ = 0;
        attempts_ = 0;
        targetHealthMutations_ = 0;
        scriptRetained_ = false;
        spawnTarget_ = false;
        targetArmed_ = false;
        healthMutationApplied_ = false;
        enabled_ = false;
        completed_ = false;
    }
}
