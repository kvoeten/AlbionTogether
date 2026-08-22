#include "RemoteHeroWeaponTransitionController.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace
{
    using fable::game::creature::equipment::CreatureWeaponFamily;

    bool IsSemanticTransition(
        const std::string& actionType,
        CreatureWeaponFamily finalFamily) noexcept
    {
        const bool draw = actionType.find("UnsheatheItemFromInventory") !=
            std::string::npos;
        const bool stow = actionType.find("SheatheItemToInventory") !=
            std::string::npos;
        return (draw && finalFamily != CreatureWeaponFamily::None) ||
            (stow && finalFamily == CreatureWeaponFamily::None);
    }

    const char* FamilyName(CreatureWeaponFamily family) noexcept
    {
        return family == CreatureWeaponFamily::Melee
            ? "melee"
            : family == CreatureWeaponFamily::Ranged
                ? "ranged"
                : "sheathed";
    }
}

namespace fable::game::hero_pawn::equipment::transitions
{
    bool RemoteHeroWeaponTransitionController::Initialize(
        game::EntityService& entities,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        diagnostics_ = diagnostics;
        initialized_ = animation_.Resolve(entities.GameModule());
        diagnostics_.Event(
            initialized_
                ? "MultiplayerRemoteWeaponTransitionReady"
                : "ClientFailed",
            initialized_
                ? "resolved native Hero draw/stow animation playback"
                : "multiplayer-remote-weapon-transition-animation");
        return initialized_;
    }

    void RemoteHeroWeaponTransitionController::Bind(
        game::Entity& hero,
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        hero_ = &hero;
        nativeHero_ = nativeHero;
        actorId_ = actorId;
    }

    bool RemoteHeroWeaponTransitionController::Submit(
        const HeroEquipmentState& finalState,
        const std::string& sourceActionType,
        std::uint32_t animationId)
    {
        if (!initialized_ || hero_ == nullptr || !hero_->IsValid() ||
            nativeHero_ == nullptr || pending_.active ||
            !finalState.IsSane() || animationId == 0 ||
            !IsSemanticTransition(sourceActionType, finalState.activeFamily))
        {
            return false;
        }

        std::size_t removedTemplateWeapons = 0;
        if (!game::creature::equipment::native::CreatureWeaponFunctions::
                PruneUnexpectedWeapons(
                    nativeHero_,
                    finalState.meleeDefinitionIndex,
                    finalState.meleeAttachmentSlot,
                    finalState.rangedDefinitionIndex,
                    finalState.rangedAttachmentSlot,
                    removedTemplateWeapons))
        {
            return false;
        }
        if (removedTemplateWeapons != 0)
        {
            hero_->UpdateAttachment();
        }

        const game::creature::animation::native::AnimationPlaybackAttempt
            attempt = animation_.Play(nativeHero_, animationId, 0);
        if (attempt.result != game::creature::animation::native::
                AnimationPlaybackResult::Played)
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        pending_.finalState = finalState;
        pending_.sourceActionType = sourceActionType;
        pending_.mutationAt = now + CarryMutationDelayMilliseconds;
        pending_.expiresAt = now + TransitionLifetimeMilliseconds;
        pending_.animationId = animationId;
        pending_.mutationAttempts = 0;
        pending_.active = true;

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu avatar=%p family=%s animation_id=%u native_action=%s mutation_delay_ms=%llu",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            FamilyName(finalState.activeFamily),
            animationId,
            sourceActionType.c_str(),
            static_cast<unsigned long long>(
                CarryMutationDelayMilliseconds));
        diagnostics_.Event(
            "MultiplayerRemoteWeaponTransitionAnimationStarted", detail);
        return true;
    }

    void RemoteHeroWeaponTransitionController::Process(
        std::uint64_t now) noexcept
    {
        if (!pending_.active || now < pending_.mutationAt)
        {
            return;
        }

        using game::creature::equipment::CreatureWeaponFamily;
        using game::creature::equipment::native::CreatureWeaponFunctions;
        using game::creature::equipment::native::CreatureWeaponInspection;
        using game::creature::equipment::native::CreatureCarryingEntry;
        using game::creature::equipment::native::CreatureCarryingInspection;
        CreatureWeaponInspection current;
        const bool inspected = CreatureWeaponFunctions::Inspect(
            nativeHero_,
            pending_.finalState.meleeDefinitionIndex,
            pending_.finalState.rangedDefinitionIndex,
            current);
        const bool requireMelee =
            pending_.finalState.activeFamily == CreatureWeaponFamily::Melee;
        const bool requireRanged =
            pending_.finalState.activeFamily == CreatureWeaponFamily::Ranged;
        const std::int32_t meleeDefinition =
            pending_.finalState.meleeDefinitionIndex > 0 &&
                (requireMelee || (inspected && current.meleePresent))
            ? pending_.finalState.meleeDefinitionIndex
            : -1;
        const std::int32_t rangedDefinition =
            pending_.finalState.rangedDefinitionIndex > 0 &&
                (requireRanged || (inspected && current.rangedPresent))
            ? pending_.finalState.rangedDefinitionIndex
            : -1;
        const std::uint32_t meleeSlot = meleeDefinition > 0
            ? pending_.finalState.meleeAttachmentSlot
            : 0;
        const std::uint32_t rangedSlot = rangedDefinition > 0
            ? pending_.finalState.rangedAttachmentSlot
            : 0;

        CreatureWeaponInspection applied;
        const bool mutated = CreatureWeaponFunctions::ApplyLoadout(
                nativeHero_,
                meleeDefinition,
                rangedDefinition,
                meleeSlot,
                rangedSlot,
                pending_.finalState.activeFamily,
                &applied) &&
            hero_->UpdateAttachment();
        ++pending_.mutationAttempts;
        if (mutated)
        {
            char detail[512] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p family=%s animation_id=%u melee=%d melee_slot=%u ranged=%d ranged_slot=%u attempts=%u",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                FamilyName(pending_.finalState.activeFamily),
                pending_.animationId,
                meleeDefinition,
                applied.meleeAttachmentSlot,
                rangedDefinition,
                applied.rangedAttachmentSlot,
                pending_.mutationAttempts);
            diagnostics_.Event(
                "MultiplayerRemoteWeaponTransitionApplied", detail);
            CreatureCarryingInspection carrying;
            if (CreatureWeaponFunctions::InspectCarrying(
                    nativeHero_, carrying))
            {
                char inventory[1024] = {};
                int used = std::snprintf(
                    inventory,
                    sizeof(inventory),
                    "actor_id=%llu count=%zu truncated=%s entries=",
                    static_cast<unsigned long long>(actorId_),
                    carrying.count,
                    carrying.truncated ? "true" : "false");
                for (std::size_t index = 0;
                     index < carrying.count && used > 0 &&
                     static_cast<std::size_t>(used) < sizeof(inventory);
                     ++index)
                {
                    const CreatureCarryingEntry& entry =
                        carrying.entries[index];
                    used += std::snprintf(
                        inventory + used,
                        sizeof(inventory) -
                            static_cast<std::size_t>(used),
                        "%s%u:%d:%p:%p",
                        index == 0 ? "[" : ",",
                        entry.attachmentSlot,
                        entry.definitionIndex,
                        entry.thing,
                        entry.graphic);
                }
                if (used > 0 &&
                    static_cast<std::size_t>(used) < sizeof(inventory) - 1)
                {
                    std::snprintf(
                        inventory + used,
                        sizeof(inventory) -
                            static_cast<std::size_t>(used),
                        "]");
                }
                diagnostics_.Event(
                    "MultiplayerRemoteWeaponCarryingInspected", inventory);
            }
            appliedState_ = pending_.finalState;
            appliedStateAvailable_ = true;
            pending_ = {};
            return;
        }

        if (pending_.mutationAttempts >= MaximumMutationAttempts ||
            now >= pending_.expiresAt)
        {
            char detail[384] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p family=%s animation_id=%u attempts=%u reason=final-carry-slots-not-applied",
                static_cast<unsigned long long>(actorId_),
                nativeHero_,
                FamilyName(pending_.finalState.activeFamily),
                pending_.animationId,
                pending_.mutationAttempts);
            diagnostics_.Event(
                "MultiplayerRemoteWeaponTransitionFailed", detail);
            pending_ = {};
            return;
        }
        pending_.mutationAt = now + MutationRetryMilliseconds;
    }

    void RemoteHeroWeaponTransitionController::Unbind() noexcept
    {
        hero_ = nullptr;
        nativeHero_ = nullptr;
        actorId_ = 0;
        pending_ = {};
        appliedState_ = {};
        appliedStateAvailable_ = false;
    }

    void RemoteHeroWeaponTransitionController::Shutdown() noexcept
    {
        Unbind();
        entities_ = nullptr;
        diagnostics_ = {};
        initialized_ = false;
    }

    bool RemoteHeroWeaponTransitionController::IsPending() const noexcept
    {
        return pending_.active;
    }

    bool RemoteHeroWeaponTransitionController::ConsumeAppliedState(
        HeroEquipmentState& state) noexcept
    {
        if (!appliedStateAvailable_)
        {
            return false;
        }
        state = appliedState_;
        appliedState_ = {};
        appliedStateAvailable_ = false;
        return true;
    }
}
