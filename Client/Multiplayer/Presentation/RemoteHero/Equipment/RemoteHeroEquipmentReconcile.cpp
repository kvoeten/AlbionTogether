#include "RemoteHeroEquipmentController.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace
{
    using fable::game::creature::equipment::CreatureWeaponFamily;
    using fable::game::creature::equipment::native::CreatureWeaponInspection;
    using fable::game::hero_pawn::equipment::HeroEquipmentState;

    // Native EquipWeapon creates short-lived CThingSoundEmitter objects. Do
    // not create both cached weapons while the remote Hero and its native
    // component graph are still settling. The visible baseline is applied
    // first; hidden cache population begins later and creates at most one
    // missing weapon per attempt.
    constexpr std::uint64_t WeaponCacheWarmDelayMilliseconds = 750;
    constexpr std::uint64_t WeaponCacheWarmRetryMilliseconds = 250;

    const char* WeaponFamilyName(CreatureWeaponFamily family) noexcept
    {
        return family == CreatureWeaponFamily::Melee
            ? "melee"
            : family == CreatureWeaponFamily::Ranged
                ? "ranged"
                : "sheathed";
    }

    bool CreatureEquipmentMatches(
        const HeroEquipmentState& expected,
        const CreatureWeaponInspection& actual) noexcept
    {
        using fable::game::creature::equipment::CreatureWeaponFamily;
        const bool requiresMelee = expected.meleeDefinitionIndex > 0 &&
            (expected.meleeAttachmentSlot != 0 ||
                expected.activeFamily == CreatureWeaponFamily::Melee);
        const bool requiresRanged = expected.rangedDefinitionIndex > 0 &&
            (expected.rangedAttachmentSlot != 0 ||
                expected.activeFamily == CreatureWeaponFamily::Ranged);
        const bool familyMatches =
            expected.activeFamily == CreatureWeaponFamily::None
                ? (!requiresMelee ||
                        actual.meleeStowedSlot == 0 || actual.meleeStowed) &&
                    (!requiresRanged ||
                        actual.rangedStowedSlot == 0 || actual.rangedStowed)
                : expected.activeFamily == CreatureWeaponFamily::Melee
                    ? actual.meleePresent && !actual.meleeStowed &&
                        (!requiresRanged ||
                            actual.rangedStowedSlot == 0 ||
                            actual.rangedStowed)
                    : actual.rangedPresent && !actual.rangedStowed &&
                        (!requiresMelee ||
                            actual.meleeStowedSlot == 0 ||
                            actual.meleeStowed);
        const bool exactCarrySlots =
            (expected.meleeDefinitionIndex <= 0 ||
                expected.meleeAttachmentSlot == 0 ||
                actual.meleeAttachmentSlot ==
                    expected.meleeAttachmentSlot) &&
            (expected.rangedDefinitionIndex <= 0 ||
                expected.rangedAttachmentSlot == 0 ||
                actual.rangedAttachmentSlot ==
                    expected.rangedAttachmentSlot);
        return expected.IsSane() && familyMatches && exactCarrySlots &&
            (!requiresMelee || actual.meleePresent) &&
            (!requiresRanged || actual.rangedPresent) &&
            (requiresMelee || !actual.meleePresent) &&
            (requiresRanged || !actual.rangedPresent);
    }
}

namespace fable::multiplayer::presentation::remote_hero::equipment
{
    namespace creature = ::fable::game::creature;

    void RemoteHeroEquipmentController::Reconcile(
        const HeroEquipmentState& state,
        std::uint64_t now)
    {
        transitions_.Process(now);
        if (hero_ == nullptr || !hero_->IsValid() || nativeHero_ == nullptr ||
            !state.IsSane())
        {
            return;
        }
        HeroEquipmentState transitioned;
        if (transitions_.ConsumeAppliedState(transitioned))
        {
            applied_ = transitioned;
            attempted_ = transitioned;
            preparedWeapons_ = transitioned.WeaponDefinitions();
            activeFamily_ = transitioned.activeFamily;
            activeWeaponReady_ = true;
            attemptCount_ = 0;
            nextAttemptAt_ = 0;
            pendingReported_ = false;
            actionOverrideUntil_ = 0;
            CreatureWeaponInspection transitionedInspection;
            const bool inspected = creature::equipment::native::
                CreatureWeaponFunctions::Inspect(
                    nativeHero_,
                    transitioned.meleeDefinitionIndex,
                    transitioned.rangedDefinitionIndex,
                    transitionedInspection);
            TrackRangedOrientation(
                transitioned.activeFamily,
                inspected ? transitionedInspection.rangedWeapon : nullptr,
                inspected ? transitionedInspection.rangedWeaponType : -1,
                transitioned.rangedDefinitionIndex);
        }
        HeroEquipmentState desired = state;
        if (now < actionOverrideUntil_ &&
            actionOverrideWeapons_.IsSane() &&
            actionOverrideWeapons_.Supports(actionOverrideFamily_))
        {
            desired.meleeDefinitionIndex =
                actionOverrideWeapons_.meleeDefinitionIndex;
            desired.rangedDefinitionIndex =
                actionOverrideWeapons_.rangedDefinitionIndex;
            desired.activeFamily = actionOverrideFamily_;
            desired.meleeAttachmentSlot = actionOverrideMeleeSlot_;
            desired.rangedAttachmentSlot = actionOverrideRangedSlot_;
        }
        if (!desired.IsSane())
        {
            return;
        }
        // Reliable draw/stow owns its short mutation window. State snapshots
        // may arrive before or after it and must not start a competing native
        // equipment action while the semantic transition is in flight.
        if (transitions_.IsPending())
        {
            return;
        }
        const HeroWeaponDefinitions desiredWeapons =
            desired.WeaponDefinitions();
        if (!prunedPresentation_.Equals(desired) &&
            now >= nextPruneAttemptAt_)
        {
            std::size_t removed = 0;
            const bool pruned = creature::equipment::native::
                CreatureWeaponFunctions::PruneUnexpectedWeapons(
                    nativeHero_,
                    desired.meleeDefinitionIndex,
                    desired.meleeAttachmentSlot,
                    desired.rangedDefinitionIndex,
                    desired.rangedAttachmentSlot,
                    removed);
            if (pruned)
            {
                prunedPresentation_ = desired;
                nextPruneAttemptAt_ = 0;
                if (removed != 0)
                {
                    hero_->UpdateAttachment();
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "actor_id=%llu removed=%zu allowed_melee=%d allowed_ranged=%d operation=remove-template-weapons",
                        static_cast<unsigned long long>(actorId_),
                        removed,
                        desired.meleeDefinitionIndex,
                        desired.rangedDefinitionIndex);
                    diagnostics_.Event(
                        "MultiplayerRemoteTemplateWeaponsRemoved", detail);
                }
            }
            else
            {
                nextPruneAttemptAt_ = now + 500;
            }
        }
        if (!attempted_.Equals(desired))
        {
            attempted_ = desired;
            preparedWeapons_ = {};
            activeWeaponReady_ = false;
            attemptCount_ = 0;
            nextAttemptAt_ = 0;
            pendingReported_ = false;
            if (!cachedWeapons_.Equals(desired.WeaponDefinitions()))
            {
                nextCacheWarmAt_ = now +
                    WeaponCacheWarmDelayMilliseconds;
            }
        }

        if (applied_.Equals(desired))
        {
            (void)WarmWeaponCache(desired, now);
            return;
        }
        if (now < nextAttemptAt_)
        {
            return;
        }

        if (attemptCount_ != (std::numeric_limits<std::uint32_t>::max)())
        {
            ++attemptCount_;
        }
        const std::uint32_t backoffStep = (std::min)(attemptCount_, 5u);
        nextAttemptAt_ = now + (250ull << backoffStep);

        const auto markApplied = [&](bool activeReady)
        {
            applied_ = desired;
            lastTransitionActionId_ = (std::max)(
                lastTransitionActionId_, desired.transitionActionId);
            preparedWeapons_ = desired.WeaponDefinitions();
            activeFamily_ = desired.activeFamily;
            activeWeaponReady_ = activeReady;
            pendingReported_ = false;
        };

        const bool requireMelee = desired.meleeDefinitionIndex > 0 &&
            (desired.meleeAttachmentSlot != 0 ||
                desired.activeFamily == CreatureWeaponFamily::Melee);
        const bool requireRanged = desired.rangedDefinitionIndex > 0 &&
            (desired.rangedAttachmentSlot != 0 ||
                desired.activeFamily == CreatureWeaponFamily::Ranged);
        const std::int32_t presentedMelee = requireMelee
            ? desired.meleeDefinitionIndex
            : -1;
        const std::int32_t presentedRanged = requireRanged
            ? desired.rangedDefinitionIndex
            : -1;
        CreatureWeaponInspection inspection;
        bool matches = creature::equipment::native::
            CreatureWeaponFunctions::Inspect(
                    nativeHero_,
                    desired.meleeDefinitionIndex,
                    desired.rangedDefinitionIndex,
                    inspection) &&
            CreatureEquipmentMatches(desired, inspection);
        if (!matches)
        {
            const bool cacheReady = cachedWeapons_.Equals(desiredWeapons) &&
                weaponCache_.IsReady(
                    nativeHero_,
                    desired.meleeDefinitionIndex,
                    desired.rangedDefinitionIndex);
            const bool appliedPresentation = cacheReady
                ? weaponCache_.ApplyPresentation(
                    nativeHero_,
                    desired.meleeAttachmentSlot,
                    desired.rangedAttachmentSlot,
                    desired.activeFamily,
                    &inspection)
                : creature::equipment::native::CreatureWeaponFunctions::
                    ApplyLoadout(
                        nativeHero_,
                        presentedMelee,
                        presentedRanged,
                        requireMelee ? desired.meleeAttachmentSlot : 0,
                        requireRanged ? desired.rangedAttachmentSlot : 0,
                        desired.activeFamily,
                        &inspection);
            matches = appliedPresentation &&
                hero_->UpdateAttachment() &&
                CreatureEquipmentMatches(desired, inspection);
        }
        if (matches)
        {
            markApplied(
                desired.activeFamily == CreatureWeaponFamily::None ||
                ActiveWeaponPresent(desired.activeFamily, inspection));
            TrackRangedOrientation(
                desired.activeFamily,
                inspection.rangedWeapon,
                inspection.rangedWeaponType,
                desired.rangedDefinitionIndex);
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu melee=%d melee_slot=%u ranged=%d ranged_slot=%u active=%s operation=direct-replicated-carry-state",
                static_cast<unsigned long long>(actorId_),
                presentedMelee,
                inspection.meleeAttachmentSlot,
                presentedRanged,
                inspection.rangedAttachmentSlot,
                WeaponFamilyName(desired.activeFamily));
            diagnostics_.Event(
                "MultiplayerRemoteEquipmentApplied", detail);
            (void)WarmWeaponCache(desired, now);
            return;
        }

        if (!pendingReported_)
        {
            pendingReported_ = true;
            char detail[640] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu carrying=%p functions=%s signature_mask=0x%04X requested_melee=%d requested_melee_slot=%u requested_ranged=%d requested_ranged_slot=%u actual_melee_present=%s actual_melee_slot=%u actual_melee_stowed=%s actual_melee_stowed_slot=%u actual_ranged_present=%s actual_ranged_slot=%u actual_ranged_stowed=%s actual_ranged_stowed_slot=%u attempt=%u operation=direct-replicated-carry-state",
                static_cast<unsigned long long>(actorId_),
                inspection.carryingComponent,
                inspection.functionsResolved ? "true" : "false",
                inspection.functionSignatureMask,
                presentedMelee,
                desired.meleeAttachmentSlot,
                presentedRanged,
                desired.rangedAttachmentSlot,
                inspection.meleePresent ? "true" : "false",
                inspection.meleeAttachmentSlot,
                inspection.meleeStowed ? "true" : "false",
                inspection.meleeStowedSlot,
                inspection.rangedPresent ? "true" : "false",
                inspection.rangedAttachmentSlot,
                inspection.rangedStowed ? "true" : "false",
                inspection.rangedStowedSlot,
                attemptCount_);
            diagnostics_.Event(
                "MultiplayerRemoteEquipmentPending", detail);
        }
    }

    void RemoteHeroEquipmentController::TrackRangedOrientation(
        CreatureWeaponFamily family,
        void* rangedWeapon,
        std::int32_t rangedWeaponType,
        const std::int32_t rangedDefinitionIndex) noexcept
    {
        if (orientationHook_ == nullptr || orientationToken_ == 0)
        {
            return;
        }
        void* const activeRangedWeapon =
            family == CreatureWeaponFamily::Ranged ? rangedWeapon : nullptr;
        if (activeRangedWeapon != nullptr && rangedWeaponType == -1 &&
            entities_ != nullptr && rangedDefinitionIndex > 0 &&
            rangedDefinitionIndex <=
                (std::numeric_limits<std::uint16_t>::max)())
        {
            std::string definitionName;
            if (entities_->ResolveDefinitionName(
                    static_cast<std::uint16_t>(rangedDefinitionIndex),
                    definitionName))
            {
                // Some freshly materialized ranged Things do not expose their
                // weapon properties until after their first attachment. The
                // authoritative definition name still distinguishes the two
                // native model bases without hard-coding individual weapons.
                if (definitionName.find("CROSSBOW") != std::string::npos)
                {
                    rangedWeaponType = 4;
                }
                else if (definitionName.find("BOW") != std::string::npos)
                {
                    rangedWeaponType = 3;
                }
            }
        }
        if (!orientationHook_->SetActiveWeapon(
                orientationToken_, activeRangedWeapon, rangedWeaponType))
        {
            diagnostics_.Event(
                "MultiplayerRemoteRangedOrientationTrackingFailed",
                "remote Hero carrying registration was retired");
        }
    }

    bool RemoteHeroEquipmentController::WarmWeaponCache(
        const HeroEquipmentState& state,
        const std::uint64_t now)
    {
        if (entities_ == nullptr || hero_ == nullptr || !hero_->IsValid() ||
            nativeHero_ == nullptr || !state.IsSane())
        {
            return false;
        }
        const HeroWeaponDefinitions definitions = state.WeaponDefinitions();
        if (cachedWeapons_.Equals(definitions) &&
            weaponCache_.IsReady(
                nativeHero_,
                definitions.meleeDefinitionIndex,
                definitions.rangedDefinitionIndex))
        {
            return true;
        }
        if (now < nextCacheWarmAt_)
        {
            return false;
        }
        nextCacheWarmAt_ = now + WeaponCacheWarmRetryMilliseconds;
        if (!weaponCache_.Ensure(
                *entities_,
                nativeHero_,
                definitions.meleeDefinitionIndex,
                definitions.rangedDefinitionIndex))
        {
            return false;
        }

        CreatureWeaponInspection inspection;
        const bool restored = weaponCache_.ApplyPresentation(
                nativeHero_,
                state.meleeAttachmentSlot,
                state.rangedAttachmentSlot,
                state.activeFamily,
                &inspection) &&
            hero_->UpdateAttachment() &&
            CreatureEquipmentMatches(state, inspection);
        if (!restored)
        {
            return false;
        }
        cachedWeapons_ = definitions;
        nextCacheWarmAt_ = 0;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu melee=%d ranged=%d operation=delayed-hidden-cache-warm",
            static_cast<unsigned long long>(actorId_),
            definitions.meleeDefinitionIndex,
            definitions.rangedDefinitionIndex);
        diagnostics_.Event("MultiplayerRemoteWeaponCacheWarmed", detail);
        return true;
    }
}
