#include "RemoteHeroCombatController.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Combat/Native/AiTargetingComponent.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Equipment/RemoteHeroEquipmentController.h"

#include <Windows.h>

#include <cmath>
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

        const auto submitAutoTurn = [&]()
        {
            constexpr float Tau = 6.28318530717958647692f;
            constexpr float AimDistance = 4.0f;
            const game::Vector3 position = hero_->GetPosition();
            const float radians = hero_->GetFacing() * Tau;
            const float targetPosition[3] = {
                position.x + std::sin(radians) * AimDistance,
                position.y + std::cos(radians) * AimDistance,
                position.z,
            };
            const bool submitted =
                std::isfinite(targetPosition[0]) &&
                std::isfinite(targetPosition[1]) &&
                std::isfinite(targetPosition[2]) &&
                combat_->SubmitReplicatedUntargetedAttack(
                    nativeHero_, targetPosition);
            char detail[480] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p ability_id=%u charge=%.3f aim=(%.3f,%.3f,%.3f) source_action=%s source_animation_id=%u route=retail-hero-untargeted-auto-turn submitted=%s",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                abilityId,
                charge,
                targetPosition[0],
                targetPosition[1],
                targetPosition[2],
                resolvedActionType.empty()
                    ? "<unresolved>"
                    : resolvedActionType.c_str(),
                resolvedAnimationId,
                submitted ? "true" : "false");
            diagnostics_.Event(
                submitted
                    ? "MultiplayerRemoteNativeUntargetedAttackSubmitted"
                    : "MultiplayerRemoteNativeUntargetedAttackRejected",
                detail);
            return submitted;
        };

        if (weaponFamily == CreatureWeaponFamily::Melee &&
            targetCreature == nullptr)
        {
            return submitAutoTurn();
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
            if (weaponFamily == CreatureWeaponFamily::Melee)
            {
                // The verified AI immediate-attack constructor owns its target
                // directly. Do not make melee replay depend on a Hero
                // targeting component, and do not let the auto-turn selector
                // choose a different contextual/acrobatic attack variant.
                const bool submitted =
                    combat_->SubmitReplicatedImmediateAttack(
                        nativeHero_, targetCreature);
                char attackDetail[448] = {};
                std::snprintf(
                    attackDetail,
                    sizeof(attackDetail),
                    "actor_id=%llu avatar=%p target=%p ability_id=%u charge=%.3f source_action=%s source_animation_id=%u route=retail-ai-immediate-attack submitted=%s",
                    static_cast<unsigned long long>(actorId_),
                    nativeHero_,
                    targetCreature,
                    abilityId,
                    charge,
                    resolvedActionType.empty()
                        ? "<unresolved>"
                        : resolvedActionType.c_str(),
                    resolvedAnimationId,
                    submitted ? "true" : "false");
                diagnostics_.Event(
                    submitted
                        ? "MultiplayerRemoteNativeAttackSubmitted"
                        : "MultiplayerRemoteNativeAttackRejected",
                    attackDetail);
                return submitted;
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
        const bool submitted = combat_->SubmitReplicatedAbility(
            nativeHero_, abilityId, charge);
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
