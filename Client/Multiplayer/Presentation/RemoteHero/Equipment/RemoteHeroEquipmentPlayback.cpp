#include "RemoteHeroEquipmentController.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Entity/Entity.h"
#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"

#include <cstdio>

namespace
{
    using fable::game::creature::equipment::CreatureWeaponFamily;
    using fable::game::creature::equipment::native::CreatureWeaponInspection;
    using fable::game::hero_pawn::equipment::HeroEquipmentState;

    constexpr std::uint64_t ActionEquipmentLeaseMilliseconds = 4'000;
    constexpr std::uint64_t TransitionEquipmentLeaseMilliseconds = 1'200;

    HeroEquipmentState MergeTransitionCarryState(
        const HeroEquipmentState& transition,
        const HeroEquipmentState& current) noexcept
    {
        if (!transition.IsSane() || !current.IsSane())
        {
            return transition;
        }

        HeroEquipmentState merged = transition;
        // A draw action can be observed after the new active weapon is
        // attached but before the previously stowed weapon is reattached.
        // Preserve only an already-inactive family here; switching away from
        // an active family must remain free to clear its old hand slot.
        if (transition.activeFamily == CreatureWeaponFamily::Ranged &&
            current.activeFamily != CreatureWeaponFamily::Melee &&
            current.meleeDefinitionIndex > 0 &&
            current.meleeAttachmentSlot != 0 &&
            (transition.meleeDefinitionIndex <= 0 ||
                (transition.meleeDefinitionIndex ==
                     current.meleeDefinitionIndex &&
                 transition.meleeAttachmentSlot == 0)))
        {
            merged.meleeDefinitionIndex = current.meleeDefinitionIndex;
            merged.meleeAttachmentSlot = current.meleeAttachmentSlot;
        }
        else if (transition.activeFamily == CreatureWeaponFamily::Melee &&
            current.activeFamily != CreatureWeaponFamily::Ranged &&
            current.rangedDefinitionIndex > 0 &&
            current.rangedAttachmentSlot != 0 &&
            (transition.rangedDefinitionIndex <= 0 ||
                (transition.rangedDefinitionIndex ==
                     current.rangedDefinitionIndex &&
                 transition.rangedAttachmentSlot == 0)))
        {
            merged.rangedDefinitionIndex = current.rangedDefinitionIndex;
            merged.rangedAttachmentSlot = current.rangedAttachmentSlot;
        }
        return merged;
    }
}

namespace fable::multiplayer::presentation::remote_hero::equipment
{
    namespace creature = ::fable::game::creature;

    bool RemoteHeroEquipmentController::PerformTransition(
        const HeroEquipmentState& finalState,
        const std::string& sourceActionType,
        std::uint32_t animationId,
        std::uint64_t actionId)
    {
        (void)sourceActionType;
        return PerformTransition(
            finalState,
            animationId,
            actionId,
            0,
            fable::multiplayer::protocol::equipment_transition_timing::
                DefaultTransitionDurationMilliseconds,
            fable::multiplayer::protocol::equipment_transition_timing::
                DefaultAttachmentNotifyOffsetMilliseconds);
    }

    bool RemoteHeroEquipmentController::PerformTransition(
        const HeroEquipmentState& finalState,
        const std::uint32_t animationId,
        const std::uint64_t actionId,
        const std::uint32_t elapsedMs,
        const std::uint32_t durationMs,
        const std::uint32_t attachmentNotifyOffsetMs)
    {
        if (actionId == 0)
        {
            return false;
        }
        if (actionId <= lastTransitionActionId_)
        {
            return true;
        }
        const HeroEquipmentState& current = attempted_.IsSane()
            ? attempted_
            : applied_;
        const HeroEquipmentState transitionState =
            MergeTransitionCarryState(finalState, current);
        if (!cachedWeapons_.Equals(transitionState.WeaponDefinitions()) ||
            !weaponCache_.IsReady(
                nativeHero_,
                transitionState.meleeDefinitionIndex,
                transitionState.rangedDefinitionIndex))
        {
            // Reconcile owns native cache materialization. A reliable
            // transition stays pending and is retried with its original
            // timestamp once the actor-scoped cache is warm.
            return false;
        }

        if (!transitions_.Submit(
                transitionState,
                animationId,
                actionId,
                elapsedMs,
                durationMs,
                attachmentNotifyOffsetMs))
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        actionOverrideWeapons_ = transitionState.WeaponDefinitions();
        actionOverrideFamily_ = transitionState.activeFamily;
        actionOverrideMeleeSlot_ = transitionState.meleeAttachmentSlot;
        actionOverrideRangedSlot_ = transitionState.rangedAttachmentSlot;
        actionOverrideUntil_ = now + TransitionEquipmentLeaseMilliseconds;
        attempted_ = transitionState;
        lastTransitionActionId_ = actionId;
        preparedWeapons_ = {};
        activeWeaponReady_ = false;
        nextAttemptAt_ = 0;
        return true;
    }

    bool RemoteHeroEquipmentController::PrepareWeapon(
        CreatureWeaponFamily family,
        const HeroWeaponDefinitions& requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot)
    {
        if (family == CreatureWeaponFamily::None)
        {
            return true;
        }
        if (hero_ == nullptr || !hero_->IsValid() || nativeHero_ == nullptr ||
            !requiredWeapons.IsSane() || !requiredWeapons.Supports(family))
        {
            return false;
        }
        const std::uint64_t now = GetTickCount64();
        actionOverrideWeapons_ = requiredWeapons;
        actionOverrideFamily_ = family;
        actionOverrideMeleeSlot_ = meleeAttachmentSlot;
        actionOverrideRangedSlot_ = rangedAttachmentSlot;
        actionOverrideUntil_ = now + ActionEquipmentLeaseMilliseconds;
        if (activeFamily_ == family && activeWeaponReady_ &&
            preparedWeapons_.Equals(requiredWeapons))
        {
            return true;
        }

        HeroEquipmentState requested;
        if (attempted_.IsSane() &&
            attempted_.WeaponDefinitions().Equals(requiredWeapons))
        {
            requested = attempted_;
        }
        else
        {
            requested.valid = true;
            requested.meleeDefinitionIndex =
                requiredWeapons.meleeDefinitionIndex;
            requested.rangedDefinitionIndex =
                requiredWeapons.rangedDefinitionIndex;
        }
        requested.activeFamily = family;
        requested.meleeAttachmentSlot = meleeAttachmentSlot;
        requested.rangedAttachmentSlot = rangedAttachmentSlot;
        if (!requested.IsSane())
        {
            return false;
        }
        creature::equipment::native::CreatureWeaponInspection inspection;
        bool inspected = creature::equipment::native::
            CreatureWeaponFunctions::Inspect(
                nativeHero_,
                requested.meleeDefinitionIndex,
                requested.rangedDefinitionIndex,
                inspection);
        const bool activePresent = inspected &&
            ActiveWeaponPresent(family, inspection);
        const bool activePresented = activePresent &&
            (family == CreatureWeaponFamily::Melee
                ? !inspection.meleeStowed
                : !inspection.rangedStowed);
        if (activePresented)
        {
            activeFamily_ = family;
            activeWeaponReady_ = true;
            preparedWeapons_ = requiredWeapons;
            TrackRangedOrientation(
                family,
                inspection.rangedWeapon,
                inspection.rangedWeaponType,
                requested.rangedDefinitionIndex);
            return true;
        }

        if (!cachedWeapons_.Equals(requiredWeapons) ||
            !weaponCache_.IsReady(
                nativeHero_,
                requested.meleeDefinitionIndex,
                requested.rangedDefinitionIndex))
        {
            // The active visible weapon path above is immediately usable.
            // Missing inactive assets are materialized only by Reconcile's
            // delayed cache warm, never synchronously from an attack.
            return false;
        }

        if (!activePresented)
        {
            // An attack can precede the retail auto-draw action in Fable's
            // accepted action stream. The retained cache keeps both weapon
            // Things warm; this path only changes their visible carry slots.
            CreatureWeaponInspection prepared;
            const bool materialized = weaponCache_.ApplyPresentation(
                    nativeHero_,
                    requested.meleeAttachmentSlot,
                    requested.rangedAttachmentSlot,
                    family,
                    &prepared);
            if (!materialized || !hero_->UpdateAttachment())
            {
                return false;
            }
            const bool ready = ActiveWeaponPresent(family, prepared) &&
                (family == CreatureWeaponFamily::Melee
                    ? !prepared.meleeStowed
                    : !prepared.rangedStowed);
            if (ready)
            {
                activeFamily_ = family;
                activeWeaponReady_ = true;
                preparedWeapons_ = requiredWeapons;
                TrackRangedOrientation(
                    family,
                    prepared.rangedWeapon,
                    prepared.rangedWeaponType,
                    requested.rangedDefinitionIndex);
                return true;
            }
        }
        return false;
    }
}
