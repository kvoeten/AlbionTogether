#include "RemoteHeroCombatController.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Combat/Native/AiTargetingComponent.h"
#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Creature/Locomotion/Native/LocomotionComponents.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Equipment/RemoteHeroEquipmentController.h"

#include <Windows.h>

#include <cstdio>

namespace fable::game::hero_pawn::combat
{
    bool RemoteHeroCombatController::Initialize(
        game::EntityService& entities,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::equipment::RemoteHeroEquipmentController& equipment,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        combat_ = &combat;
        equipment_ = &equipment;
        diagnostics_ = diagnostics;
        rangedAim_.Initialize(diagnostics);
        return true;
    }

    bool RemoteHeroCombatController::Bind(
        game::Entity& hero,
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        if (nativeHero == nullptr || combat_ == nullptr)
        {
            return false;
        }
        hero_ = &hero;
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        rangedAim_.Bind(nativeHero, actorId);
        healthReplicaProtected_ = combat_->SetReplicaHealthProtection(
            nativeHero_, true);
        return healthReplicaProtected_;
    }

    bool RemoteHeroCombatController::ApplyHealth(
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        if (combat_ == nullptr || nativeHero_ == nullptr || revision == 0)
        {
            return false;
        }
        if (healthCreature_ == nativeHero_ &&
            appliedHealthRevision_ == revision)
        {
            return true;
        }
        if (!combat_->ApplyAuthoritativeCombatHealth(
                nativeHero_, currentHealth, maximumHealth))
        {
            return false;
        }
        const bool terminal = currentHealth <= 0.01f;
        if (terminal && !terminalHealthObserved_)
        {
            // CCreatureAction_Die owns full native creature teardown. On a
            // remote AHeroPawn it unregisters the presentation and replaces
            // it with a non-creature corpse, leaving no actor for the next
            // positive health revision to revive. Keep the proxy alive and
            // let the already-replicated terminal hit reaction represent the
            // brief down state; the owner remains authoritative for phial or
            // Guild recovery and publishes the restorative health revision.
            terminalHealthObserved_ = true;
            diagnostics_.Event(
                "MultiplayerRemotePlayerTerminalHealthHeld",
                "remote Hero proxy retained for reversible owner-authoritative revival");
        }
        else if (!terminal)
        {
            terminalHealthObserved_ = false;
        }
        healthCreature_ = nativeHero_;
        appliedHealthRevision_ = revision;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor=%llu revision=%u health=%.3f maximum=%.3f death=%s",
            static_cast<unsigned long long>(actorId_),
            revision,
            currentHealth,
            maximumHealth,
            terminal ? "proxy-retained" : "alive");
        diagnostics_.Event("MultiplayerRemotePlayerVitalsApplied", detail);
        return true;
    }

    bool RemoteHeroCombatController::PerformAbility(
        creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        std::uint32_t abilityId,
        float charge,
        void* targetCreature,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId)
    {
        using creature::equipment::CreatureWeaponFamily;
        if (combat_ == nullptr || entities_ == nullptr || equipment_ == nullptr ||
            nativeHero_ == nullptr || hero_ == nullptr || !hero_->IsValid() ||
            !equipment_->PrepareWeapon(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot))
        {
            return false;
        }

        if (targetCreature != nullptr)
        {
            const HMODULE gameModule = entities_->GameModule();
            const bool targetValid =
                creature::native::CreatureFrameFunctions::ValidateCreature(
                    gameModule, targetCreature) ||
                creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(gameModule, targetCreature);
            if (!targetValid)
            {
                return false;
            }
            void* const targeting = entity::native::ThingComponentAccess::Find(
                nativeHero_, entity::native::ThingComponentType::Targeting);
            const bool selectedAssigned =
                creature::combat::native::AiTargetingComponent::
                    SetScriptTargetOverride(
                        gameModule, targeting, targetCreature);
            if (!selectedAssigned)
            {
                return false;
            }
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p targeting=%p target=%p source=canonical-entity-identity",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                targeting,
                targetCreature);
            diagnostics_.Event(
                "MultiplayerRemotePlayerTargetAssigned", detail);
        }
        bool submitted = false;
        const char* route = "retail-hero-ability";
        const bool rangedAim = weaponFamily == CreatureWeaponFamily::Ranged &&
            resolvedActionType.find("HeroLoadRangedWeapon") !=
                std::string::npos;
        const bool rangedFire =
            weaponFamily == CreatureWeaponFamily::Ranged &&
            resolvedActionType.find("FireMissileWeapon") !=
                std::string::npos;
        if (rangedAim || rangedFire)
        {
            creature::equipment::native::CreatureWeaponInspection inspection;
            const bool rangedWeaponReady =
                creature::equipment::native::CreatureWeaponFunctions::Inspect(
                    nativeHero_,
                    requiredWeapons.meleeDefinitionIndex,
                    requiredWeapons.rangedDefinitionIndex,
                    inspection) &&
                inspection.rangedPresent &&
                inspection.rangedWeapon != nullptr;
            const bool aimWasActive = rangedAim_.IsActive();
            const bool aimModeReady = !rangedAim || rangedAim_.Begin();
            submitted = rangedWeaponReady && aimModeReady && (rangedAim
                ? combat_->SubmitReplicatedRangedAim(
                    nativeHero_,
                    inspection.rangedWeapon,
                    resolvedActionType.c_str(),
                    resolvedAnimationId)
                : combat_->SubmitReplicatedRangedFire(
                    nativeHero_,
                    inspection.rangedWeapon,
                    resolvedActionType.c_str(),
                    resolvedAnimationId));
            if (rangedAim && !submitted && !aimWasActive)
            {
                (void)rangedAim_.End();
            }
            else if (rangedFire && submitted && !rangedAim_.End())
            {
                diagnostics_.Event(
                    "MultiplayerRemoteRangedAimModeExitFailed",
                    "ranged fire was accepted; cleanup will be retried on the ordered aim-end event");
            }
            route = rangedAim
                ? "native-ranged-aim-action"
                : "native-fire-missile-action";
        }
        else if (weaponFamily == CreatureWeaponFamily::Melee &&
            resolvedActionType.find("InterruptableMidAttackAutoTurn") !=
                std::string::npos)
        {
            creature::locomotion::native::LocomotionComponentSnapshot aim;
            const HMODULE gameModule = entities_->GameModule();
            const bool targetPositionResolved = targetCreature != nullptr &&
                creature::locomotion::native::LocomotionComponentDefinition::
                    Inspect(gameModule, targetCreature, aim) &&
                aim.physicsNavigatorValidated;
            if (!targetPositionResolved)
            {
                aim = {};
                if (!creature::locomotion::native::
                        LocomotionComponentDefinition::Inspect(
                            gameModule, nativeHero_, aim) ||
                    !aim.physicsNavigatorValidated)
                {
                    return false;
                }
                // Untargeted retail Hero attacks take a world aim point. A
                // missing semantic target should not block the owner-observed
                // swing; use a stable nearby point and retain the replicated
                // pawn yaw already driven by locomotion.
                aim.physicsPosition.y += 1.0f;
            }
            const float targetPosition[3] = {
                aim.physicsPosition.x,
                aim.physicsPosition.y,
                aim.physicsPosition.z,
            };
            submitted = combat_->SubmitReplicatedUntargetedAttack(
                nativeHero_,
                targetPosition,
                resolvedActionType.c_str(),
                resolvedAnimationId);
            route = "native-hero-auto-turn-action";
        }
        else
        {
            submitted = combat_->SubmitReplicatedAbility(
                nativeHero_,
                abilityId,
                charge,
                resolvedActionType.c_str(),
                resolvedAnimationId);
        }
        if (weaponFamily == CreatureWeaponFamily::Melee ||
            weaponFamily == CreatureWeaponFamily::Ranged)
        {
            char detail[448] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p target=%p ability_id=%u charge=%.3f source_action=%s source_animation_id=%u route=%s submitted=%s",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                targetCreature,
                abilityId,
                charge,
                resolvedActionType.empty()
                    ? "<unresolved>"
                    : resolvedActionType.c_str(),
                resolvedAnimationId,
                route,
                submitted ? "true" : "false");
            diagnostics_.Event(
                submitted
                    ? (weaponFamily == CreatureWeaponFamily::Ranged
                        ? "MultiplayerRemoteNativeRangedAttackSubmitted"
                        : "MultiplayerRemoteNativeAttackSubmitted")
                    : (weaponFamily == CreatureWeaponFamily::Ranged
                        ? "MultiplayerRemoteNativeRangedAttackRejected"
                        : "MultiplayerRemoteNativeAttackRejected"),
                detail);
        }
        return submitted;
    }

    bool RemoteHeroCombatController::EndRangedAim() noexcept
    {
        return rangedAim_.End();
    }

    void RemoteHeroCombatController::Unbind() noexcept
    {
        rangedAim_.Unbind();
        if (healthReplicaProtected_ && combat_ != nullptr &&
            nativeHero_ != nullptr)
        {
            combat_->SetReplicaHealthProtection(nativeHero_, false);
        }
        hero_ = nullptr;
        nativeHero_ = nullptr;
        actorId_ = 0;
        healthCreature_ = nullptr;
        appliedHealthRevision_ = 0;
        terminalHealthObserved_ = false;
        healthReplicaProtected_ = false;
    }

    void RemoteHeroCombatController::Shutdown() noexcept
    {
        Unbind();
        rangedAim_.Shutdown();
        entities_ = nullptr;
        combat_ = nullptr;
        equipment_ = nullptr;
        diagnostics_ = {};
    }
}
