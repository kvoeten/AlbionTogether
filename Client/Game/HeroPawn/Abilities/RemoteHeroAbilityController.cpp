#include "RemoteHeroAbilityController.h"

#include "Game/Creature/Combat/Native/AiTargetingComponent.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/HeroPawn/Abilities/Native/HeroWillAbilityFunctions.h"
#include "Game/HeroPawn/Abilities/Native/HeroWillComponentProvisioner.h"

#include <cstdio>

namespace fable::game::hero_pawn::abilities
{
    bool RemoteHeroAbilityController::Initialize(
        game::EntityService& entities,
        HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        return true;
    }

    bool RemoteHeroAbilityController::Bind(
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        if (nativeHero == nullptr || actorId == 0 || entities_ == nullptr ||
            abilities_ == nullptr)
        {
            return false;
        }
        native::HeroWillComponentProvisioning provisioning;
        if (!native::HeroWillComponentProvisioner::Ensure(
                entities_->GameModule(), nativeHero, provisioning))
        {
            char failure[192] = {};
            std::snprintf(
                failure,
                sizeof(failure),
                "actor_id=%llu hero=%p reason=will-controller-provisioning",
                static_cast<unsigned long long>(actorId),
                nativeHero);
            diagnostics_.Event(
                "MultiplayerRemoteHeroAbilityBindFailed", failure);
            return false;
        }
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        if (!abilities_->BindRemotePresentationHero(nativeHero_, actorId_))
        {
            nativeHero_ = nullptr;
            actorId_ = 0;
            diagnostics_.Event(
                "MultiplayerRemoteHeroAbilityBindFailed",
                "reason=assassin-rush-presentation-routing");
            return false;
        }
        nativeAbilityInventoryPresent_ =
            provisioning.abilityInventoryPresent;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p component=%p added=%s ability_inventory=%s inventory_added=%s owner=%p creature=%p",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            provisioning.component,
            provisioning.added ? "true" : "false",
            nativeAbilityInventoryPresent_ ? "true" : "false",
            provisioning.abilityInventoryAdded ? "true" : "false",
            native::HeroWillAbilityFunctions::ReadOwner(
                provisioning.component),
            native::HeroWillAbilityFunctions::ReadCreature(
                provisioning.component));
        diagnostics_.Event(
            "MultiplayerRemoteHeroAbilityBound", detail);
        return true;
    }

    bool RemoteHeroAbilityController::Perform(
        HeroAbility ability,
        HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature) noexcept
    {
        if (nativeHero_ == nullptr || entities_ == nullptr ||
            abilities_ == nullptr || !IsValid(ability) || !IsValid(command))
        {
            if (ability != lastFailureAbility_ ||
                command != lastFailureCommand_)
            {
                lastFailureAbility_ = ability;
                lastFailureCommand_ = command;
                diagnostics_.Event(
                    "MultiplayerRemoteHeroAbilityPrerequisiteFailed",
                    "reason=controller-not-bound-or-invalid-command");
            }
            return false;
        }
        if (targetCreature != nullptr)
        {
            const HMODULE module = entities_->GameModule();
            const bool validTarget = creature::native::CreatureFrameFunctions::
                    ValidateCreature(module, targetCreature) ||
                creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(module, targetCreature);
            void* const targeting = entity::native::ThingComponentAccess::Find(
                nativeHero_, entity::native::ThingComponentType::Targeting);
            const bool targetingValid = targeting != nullptr &&
                creature::combat::native::AiTargetingComponent::Validate(
                    module, targeting);
            const bool selectedAssigned = validTarget && targetingValid &&
                creature::combat::native::AiTargetingComponent::
                    SetScriptTargetOverride(
                        module, targeting, targetCreature);
            void* const observedTarget = targetingValid
                ? creature::combat::native::AiTargetingComponent::
                    GetScriptTargetOverride(module, targeting)
                : nullptr;
            const bool targetObserved = observedTarget == targetCreature;
            if (!validTarget ||
                (!selectedAssigned && !targetObserved))
            {
                if (ability != lastFailureAbility_ ||
                    command != lastFailureCommand_)
                {
                    lastFailureAbility_ = ability;
                    lastFailureCommand_ = command;
                    char failure[320] = {};
                    std::snprintf(
                        failure,
                        sizeof(failure),
                        "reason=ai-script-target-override valid_target=%s targeting_valid=%s targeting=%p assigned=%s observed=%s target=%p observed_target=%p ability=%u command=%u",
                        validTarget ? "true" : "false",
                        targetingValid ? "true" : "false",
                        targeting,
                        selectedAssigned ? "true" : "false",
                        targetObserved ? "true" : "false",
                        targetCreature,
                        observedTarget,
                        static_cast<unsigned int>(ability),
                        static_cast<unsigned int>(command));
                    diagnostics_.Event(
                        "MultiplayerRemoteHeroAbilityPrerequisiteFailed",
                        failure);
                }
                return false;
            }
        }
        if (nativeAbilityInventoryPresent_ &&
            !abilities_->ApplyProgressionState(
                nativeHero_, ability, progressionState))
        {
            char failure[192] = {};
            std::snprintf(
                failure,
                sizeof(failure),
                "reason=progression-apply-failed hero=%p ability=%u state=%d",
                nativeHero_,
                static_cast<unsigned int>(ability),
                progressionState);
            diagnostics_.Event(
                "MultiplayerRemoteHeroAbilityPrerequisiteFailed", failure);
            return false;
        }
        // CTCBerserk owns local-player render and delayed teardown state. It
        // can start on a second Hero but corrupts that proxy when its native
        // lifetime retires. The owner-authored Hero morph/bone-scale channel
        // already carries the visible transformation, so consume the remote
        // command without constructing CTCBerserk on the presentation Hero.
        const bool presentationOnly = ability == HeroAbility::Berserk;
        const bool accepted = presentationOnly || abilities_->Replay(
            nativeHero_, ability, command);
        if (presentationOnly)
        {
            char presentation[192] = {};
            std::snprintf(
                presentation,
                sizeof(presentation),
                "actor_id=%llu hero=%p command=%u route=replicated-owner-morph-state",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                static_cast<unsigned int>(command));
            diagnostics_.Event(
                "MultiplayerRemoteHeroBerserkPresentationOnly",
                presentation);
        }
        if (accepted)
        {
            lastFailureAbility_ = HeroAbility::None;
            lastFailureCommand_ = HeroAbilityCommand::None;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p target=%p ability=%u name=%s command=%u accepted=%s",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            targetCreature,
            static_cast<unsigned int>(ability),
            Name(ability),
            static_cast<unsigned int>(command),
            accepted ? "true" : "false");
        diagnostics_.Event(
            accepted
                ? "MultiplayerRemoteHeroAbilitySubmitted"
                : "MultiplayerRemoteHeroAbilityRejected",
            detail);
        return accepted;
    }

    void RemoteHeroAbilityController::Unbind() noexcept
    {
        if (abilities_ != nullptr && nativeHero_ != nullptr)
        {
            abilities_->UnbindRemotePresentationHero(nativeHero_);
        }
        nativeHero_ = nullptr;
        actorId_ = 0;
        nativeAbilityInventoryPresent_ = false;
        lastFailureAbility_ = HeroAbility::None;
        lastFailureCommand_ = HeroAbilityCommand::None;
    }

    void RemoteHeroAbilityController::Shutdown() noexcept
    {
        Unbind();
        entities_ = nullptr;
        abilities_ = nullptr;
        diagnostics_ = {};
    }
}
