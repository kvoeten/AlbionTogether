#include "RemoteHeroWeaponTransitionController.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Creature/Equipment/Native/CreatureWeaponCache.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace
{
    using fable::game::creature::equipment::CreatureWeaponFamily;
    using fable::game::creature::equipment::native::CreatureWeaponInspection;

    constexpr const char* NativeWeaponTransitionActionType =
        "CCreatureAction_UnsheatheWeapons";

    const char* FamilyName(CreatureWeaponFamily family) noexcept
    {
        return family == CreatureWeaponFamily::Melee
            ? "melee"
            : family == CreatureWeaponFamily::Ranged
                ? "ranged"
                : "sheathed";
    }

    bool MatchesTransitionState(
        const fable::game::hero_pawn::equipment::HeroEquipmentState& expected,
        const CreatureWeaponInspection& actual) noexcept
    {
        const bool meleeExpected = expected.meleeDefinitionIndex > 0 &&
            (expected.activeFamily == CreatureWeaponFamily::Melee ||
                expected.meleeAttachmentSlot != 0);
        const bool rangedExpected = expected.rangedDefinitionIndex > 0 &&
            (expected.activeFamily == CreatureWeaponFamily::Ranged ||
                expected.rangedAttachmentSlot != 0);
        const bool meleeSlotMatches = !meleeExpected ||
            expected.meleeAttachmentSlot == 0 ||
            actual.meleeAttachmentSlot == expected.meleeAttachmentSlot;
        const bool rangedSlotMatches = !rangedExpected ||
            expected.rangedAttachmentSlot == 0 ||
            actual.rangedAttachmentSlot == expected.rangedAttachmentSlot;

        if (!meleeSlotMatches || !rangedSlotMatches ||
            (meleeExpected && !actual.meleePresent) ||
            (rangedExpected && !actual.rangedPresent))
        {
            return false;
        }
        if (expected.activeFamily == CreatureWeaponFamily::Melee)
        {
            return actual.meleePresent && !actual.meleeStowed &&
                (!rangedExpected || actual.rangedStowed);
        }
        if (expected.activeFamily == CreatureWeaponFamily::Ranged)
        {
            return actual.rangedPresent && !actual.rangedStowed &&
                (!meleeExpected || actual.meleeStowed);
        }
        return (!meleeExpected || actual.meleeStowed) &&
            (!rangedExpected || actual.rangedStowed);
    }
}

namespace fable::game::hero_pawn::equipment::transitions
{
    bool RemoteHeroWeaponTransitionController::Initialize(
        game::EntityService& entities,
        game::creature::animation::CreatureAnimationService& animation,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        animation_ = &animation;
        diagnostics_ = diagnostics;
        initialized_ = animation.IsReady();
        diagnostics_.Event(
            initialized_
                ? "MultiplayerRemoteWeaponTransitionReady"
                : "ClientFailed",
            initialized_
                ? "resolved native Hero draw/stow action submission"
                : "multiplayer-remote-weapon-transition-animation");
        return initialized_;
    }

    void RemoteHeroWeaponTransitionController::Bind(
        game::Entity& hero,
        void* nativeHero,
        std::uint64_t actorId,
        game::creature::equipment::native::CreatureWeaponCache&
            weaponCache) noexcept
    {
        Unbind();
        hero_ = &hero;
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        weaponCache_ = &weaponCache;
    }

    bool RemoteHeroWeaponTransitionController::Submit(
        const HeroEquipmentState& finalState,
        std::uint32_t animationId,
        std::uint64_t actionId)
    {
        return Submit(
            finalState,
            animationId,
            actionId,
            0,
            fable::multiplayer::protocol::equipment_transition_timing::
                DefaultTransitionDurationMilliseconds,
            fable::multiplayer::protocol::equipment_transition_timing::
                DefaultAttachmentNotifyOffsetMilliseconds);
    }

    bool RemoteHeroWeaponTransitionController::Submit(
        const HeroEquipmentState& finalState,
        std::uint32_t animationId,
        std::uint64_t actionId,
        const std::uint32_t elapsedMs,
        const std::uint32_t durationMs,
        const std::uint32_t attachmentNotifyOffsetMs)
    {
        if (!initialized_ || entities_ == nullptr || animation_ == nullptr ||
            hero_ == nullptr || !hero_->IsValid() ||
            nativeHero_ == nullptr || weaponCache_ == nullptr ||
            !finalState.IsSane() || animationId == 0 || actionId == 0 ||
            durationMs == 0 || elapsedMs >= durationMs ||
            attachmentNotifyOffsetMs > durationMs ||
            actionId <= lastSubmittedActionId_)
        {
            return false;
        }
        if (pending_.active && actionId <= pending_.actionId)
        {
            return false;
        }
        if (pending_.active)
        {
            // A newer reliable revision owns the actor immediately. Resetting
            // this record invalidates the older delayed carry mutation before
            // it can run; there is no FIFO animation-completion wait.
            pending_ = {};
            diagnostics_.Event(
                "MultiplayerRemoteWeaponTransitionSuperseded",
                "newer action/change revision replaced pending transition");
        }
        if (!weaponCache_->Ensure(
                *entities_,
                nativeHero_,
                finalState.meleeDefinitionIndex,
                finalState.rangedDefinitionIndex) ||
            !weaponCache_->StageTransition(
                nativeHero_, finalState.activeFamily))
        {
            return false;
        }

        const bool selectionArmed = animation_->BeginReplicatedActionSelection(
            nativeHero_, NativeWeaponTransitionActionType, animationId);
        const bool submitted = game::creature::actions::native::
            CreatureActionFunctions::SubmitWeaponTransition(
                entities_->GameModule(), nativeHero_, finalState.activeFamily);
        if (selectionArmed)
        {
            animation_->EndReplicatedActionSelection();
        }
        if (!submitted)
        {
            return false;
        }
        lastSubmittedActionId_ = actionId;

        const std::uint64_t now = GetTickCount64();
        const std::uint32_t mutationDelay =
            attachmentNotifyOffsetMs > elapsedMs
                ? attachmentNotifyOffsetMs - elapsedMs
                : 0;
        const std::uint32_t remainingDuration = durationMs - elapsedMs;
        pending_.finalState = finalState;
        pending_.mutationAt = now + mutationDelay;
        pending_.expiresAt = now + remainingDuration;
        pending_.animationId = animationId;
        pending_.actionId = actionId;
        pending_.mutationAttempts = 0;
        pending_.active = true;

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu avatar=%p family=%s animation_id=%u elapsed_ms=%u mutation_delay_ms=%u remaining_ms=%u",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            FamilyName(finalState.activeFamily),
            animationId,
            elapsedMs,
            mutationDelay,
            remainingDuration);
        diagnostics_.Event(
            "MultiplayerRemoteWeaponTransitionNativeActionStarted", detail);
        return true;
    }

    bool RemoteHeroWeaponTransitionController::Submit(
        const HeroEquipmentState& finalState,
        const std::string& sourceActionType,
        std::uint32_t animationId,
        std::uint64_t actionId)
    {
        (void)sourceActionType;
        return Submit(finalState, animationId, actionId);
    }

    void RemoteHeroWeaponTransitionController::Process(
        std::uint64_t now) noexcept
    {
        if (!pending_.active)
        {
            return;
        }
        if (now < pending_.mutationAt)
        {
            return;
        }

        using game::creature::equipment::CreatureWeaponFamily;
        using game::creature::equipment::native::CreatureWeaponFunctions;
        using game::creature::equipment::native::CreatureCarryingEntry;
        using game::creature::equipment::native::CreatureCarryingInspection;
        const std::int32_t meleeDefinition =
            pending_.finalState.meleeDefinitionIndex;
        const std::int32_t rangedDefinition =
            pending_.finalState.rangedDefinitionIndex;

        CreatureWeaponInspection applied;
        const bool inspected = CreatureWeaponFunctions::Inspect(
            nativeHero_, meleeDefinition, rangedDefinition, applied);
        bool mutated = inspected &&
            MatchesTransitionState(pending_.finalState, applied);
        ++pending_.mutationAttempts;
        if (!mutated)
        {
            // Remote Heroes do not own CTCHeroInventoryWeapons, so the retail
            // action cannot always perform its inventory event. The cached
            // weapon Thing is attached at the action's normal mutation point;
            // no definition or render graph is created on this visible frame.
            mutated = weaponCache_->ApplyPresentation(
                    nativeHero_,
                    pending_.finalState.meleeAttachmentSlot,
                    pending_.finalState.rangedAttachmentSlot,
                    pending_.finalState.activeFamily,
                    &applied) &&
                hero_->UpdateAttachment();
        }
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
            // Carrying state is authoritative as soon as the latest revision
            // reaches its mutation point. Do not retain an animation-completion
            // lease: a subsequent revision must be able to submit immediately.
            pending_ = {};
            return;
        }

        if (now >= pending_.expiresAt)
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
        weaponCache_ = nullptr;
        pending_ = {};
        lastSubmittedActionId_ = 0;
        appliedState_ = {};
        appliedStateAvailable_ = false;
    }

    void RemoteHeroWeaponTransitionController::Shutdown() noexcept
    {
        Unbind();
        entities_ = nullptr;
        animation_ = nullptr;
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
