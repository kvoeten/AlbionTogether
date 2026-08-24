#include "RemoteHeroCombatController.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Combat/Native/AiTargetingComponent.h"
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
        healthCreature_ = nativeHero_;
        appliedHealthRevision_ = revision;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor=%llu revision=%u health=%.3f maximum=%.3f",
            static_cast<unsigned long long>(actorId_),
            revision,
            currentHealth,
            maximumHealth);
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
        if (weaponFamily == CreatureWeaponFamily::Melee &&
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
        if (weaponFamily == CreatureWeaponFamily::Melee)
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
                    ? "MultiplayerRemoteNativeAttackSubmitted"
                    : "MultiplayerRemoteNativeAttackRejected",
                detail);
        }
        return submitted;
    }

    void RemoteHeroCombatController::Unbind() noexcept
    {
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
        healthReplicaProtected_ = false;
    }

    void RemoteHeroCombatController::Shutdown() noexcept
    {
        Unbind();
        entities_ = nullptr;
        combat_ = nullptr;
        equipment_ = nullptr;
        diagnostics_ = {};
    }
}
