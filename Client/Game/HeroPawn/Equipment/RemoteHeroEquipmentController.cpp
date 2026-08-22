#include "RemoteHeroEquipmentController.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponentProvisioner.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace
{
    using fable::game::creature::equipment::CreatureWeaponFamily;
    using fable::game::creature::equipment::native::CreatureWeaponInspection;
    using fable::game::hero_pawn::equipment::HeroEquipmentState;
    using fable::game::hero_pawn::equipment::native::HeroWeaponInspection;

    constexpr std::uint64_t ActionEquipmentLeaseMilliseconds = 4'000;

    const char* WeaponFamilyName(CreatureWeaponFamily family) noexcept
    {
        return family == CreatureWeaponFamily::Melee
            ? "melee"
            : family == CreatureWeaponFamily::Ranged
                ? "ranged"
                : "sheathed";
    }

    bool ActiveWeaponPresent(
        CreatureWeaponFamily family,
        const CreatureWeaponInspection& inspection) noexcept
    {
        return family == CreatureWeaponFamily::Melee
            ? inspection.meleePresent && inspection.meleeWeapon != nullptr
            : family == CreatureWeaponFamily::Ranged
                ? inspection.rangedPresent &&
                    inspection.rangedWeapon != nullptr
                : true;
    }

    bool HeroEquipmentMatches(
        const HeroEquipmentState& expected,
        const HeroWeaponInspection& actual) noexcept
    {
        return expected.IsSane() && actual.readable &&
            actual.meleeDefinitionIndex == expected.meleeDefinitionIndex &&
            actual.rangedDefinitionIndex == expected.rangedDefinitionIndex &&
            (expected.meleeAttachmentSlot == 0 ||
                actual.meleeAttachmentSlot == expected.meleeAttachmentSlot) &&
            (expected.rangedAttachmentSlot == 0 ||
                actual.rangedAttachmentSlot == expected.rangedAttachmentSlot) &&
            actual.activeFamily == expected.activeFamily;
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

namespace fable::game::hero_pawn::equipment
{
    bool RemoteHeroEquipmentController::Initialize(
        game::EntityService& entities,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        diagnostics_ = diagnostics;
        return transitions_.Initialize(entities, diagnostics);
    }

    bool RemoteHeroEquipmentController::Bind(
        game::Entity& hero,
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        if (entities_ == nullptr || nativeHero == nullptr || actorId == 0)
        {
            return false;
        }
        native::HeroWeaponComponentProvisioning provisioning;
        if (!native::HeroWeaponComponentProvisioner::Ensure(
                entities_->GameModule(), nativeHero, provisioning))
        {
            diagnostics_.Event(
                "MultiplayerRemoteHeroEquipmentBindFailed",
                "reason=hero-weapon-inventory-provisioning");
            return false;
        }
        hero_ = &hero;
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        transitions_.Bind(hero, nativeHero, actorId);
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p hero_core=%p hero_core_added=%s component=%p added=%s",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            provisioning.heroCore,
            provisioning.heroCoreAdded ? "true" : "false",
            provisioning.component,
            provisioning.added ? "true" : "false");
        diagnostics_.Event("MultiplayerRemoteHeroEquipmentBound", detail);
        return true;
    }

    bool RemoteHeroEquipmentController::PerformTransition(
        const HeroEquipmentState& finalState,
        const std::string& sourceActionType,
        std::uint32_t animationId)
    {
        if (!transitions_.Submit(
                finalState, sourceActionType, animationId))
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        actionOverrideWeapons_ = finalState.WeaponDefinitions();
        actionOverrideFamily_ = finalState.activeFamily;
        actionOverrideMeleeSlot_ = finalState.meleeAttachmentSlot;
        actionOverrideRangedSlot_ = finalState.rangedAttachmentSlot;
        actionOverrideUntil_ = now + ActionEquipmentLeaseMilliseconds;
        attempted_ = finalState;
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
        native::HeroWeaponInspection heroInspection;
        const bool heroInspected = native::HeroWeaponComponent::Inspect(
            nativeHero_, heroInspection);
        usesHeroInventory_ = heroInspected &&
            heroInspection.component != nullptr &&
            heroInspection.functionsResolved;
        if (usesHeroInventory_ && entities_ != nullptr)
        {
            if (heroInspection.readable &&
                HeroEquipmentMatches(requested, heroInspection))
            {
                preparedWeapons_ = requiredWeapons;
                activeFamily_ = family;
                activeWeaponReady_ = true;
                return true;
            }
            if (native::HeroWeaponComponent::ApplyDefinitions(
                    nativeHero_, requested))
            {
                native::HeroWeaponInspection verified;
                if (native::HeroWeaponComponent::Inspect(
                        nativeHero_, verified) &&
                    HeroEquipmentMatches(requested, verified))
                {
                    preparedWeapons_ = requiredWeapons;
                    activeFamily_ = family;
                    activeWeaponReady_ = true;
                    return true;
                }
            }
            return false;
        }

        const std::int32_t preparationMeleeDefinition =
            family == CreatureWeaponFamily::Melee
                ? requested.meleeDefinitionIndex
                : -1;
        const std::int32_t preparationRangedDefinition =
            family == CreatureWeaponFamily::Ranged
                ? requested.rangedDefinitionIndex
                : -1;
        const std::uint32_t preparationMeleeSlot =
            family == CreatureWeaponFamily::Melee
                ? requested.meleeAttachmentSlot
                : 0;
        const std::uint32_t preparationRangedSlot =
            family == CreatureWeaponFamily::Ranged
                ? requested.rangedAttachmentSlot
                : 0;
        creature::equipment::native::CreatureWeaponInspection inspection;
        bool inspected = creature::equipment::native::
            CreatureWeaponFunctions::Inspect(
                nativeHero_,
                preparationMeleeDefinition,
                preparationRangedDefinition,
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
            return true;
        }

        if (!activePresented)
        {
            // An attack can precede the retail auto-draw action in Fable's
            // accepted action stream. Prepare the exact carry state once so
            // that action can execute; the separately ordered transition owns
            // the visible draw animation and must never be synthesized here.
            CreatureWeaponInspection prepared;
            const bool materialized = creature::equipment::native::
                CreatureWeaponFunctions::ApplyLoadout(
                    nativeHero_,
                    preparationMeleeDefinition,
                    preparationRangedDefinition,
                    preparationMeleeSlot,
                    preparationRangedSlot,
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
                return true;
            }
        }
        return false;
    }

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
        }

        if (applied_.Equals(desired) || now < nextAttemptAt_)
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
            preparedWeapons_ = desired.WeaponDefinitions();
            activeFamily_ = desired.activeFamily;
            activeWeaponReady_ = activeReady;
            pendingReported_ = false;
        };

        native::HeroWeaponInspection heroInspection;
        const bool heroInspected = native::HeroWeaponComponent::Inspect(
            nativeHero_, heroInspection);
        usesHeroInventory_ = heroInspected &&
            heroInspection.component != nullptr &&
            heroInspection.functionsResolved;
        if (usesHeroInventory_)
        {
            bool matches = heroInspection.readable &&
                HeroEquipmentMatches(desired, heroInspection);
            if (!matches && native::HeroWeaponComponent::ApplyDefinitions(
                    nativeHero_, desired))
            {
                matches = native::HeroWeaponComponent::Inspect(
                        nativeHero_, heroInspection) &&
                    HeroEquipmentMatches(desired, heroInspection);
            }
            if (matches)
            {
                markApplied(true);
                return;
            }
            if (!pendingReported_)
            {
                pendingReported_ = true;
                diagnostics_.Event(
                    "MultiplayerRemoteHeroEquipmentPending",
                    "Hero inventory state is not ready for direct reconciliation");
            }
            return;
        }

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
        bool matches = creature::equipment::native::CreatureWeaponFunctions::
                Inspect(
                    nativeHero_,
                    desired.meleeDefinitionIndex,
                    desired.rangedDefinitionIndex,
                    inspection) &&
            CreatureEquipmentMatches(desired, inspection);
        if (!matches)
        {
            matches = creature::equipment::native::CreatureWeaponFunctions::
                    ApplyLoadout(
                        nativeHero_,
                        presentedMelee,
                        presentedRanged,
                        requireMelee ? desired.meleeAttachmentSlot : 0,
                        requireRanged ? desired.rangedAttachmentSlot : 0,
                        desired.activeFamily,
                        &inspection) &&
                hero_->UpdateAttachment() &&
                CreatureEquipmentMatches(desired, inspection);
        }
        if (matches)
        {
            markApplied(
                desired.activeFamily == CreatureWeaponFamily::None ||
                ActiveWeaponPresent(desired.activeFamily, inspection));
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
            return;
        }

        if (!pendingReported_)
        {
            pendingReported_ = true;
            char detail[352] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu carrying=%p functions=%s signature_mask=0x%03X requested_melee=%d requested_melee_slot=%u requested_ranged=%d requested_ranged_slot=%u attempt=%u operation=direct-replicated-carry-state",
                static_cast<unsigned long long>(actorId_),
                inspection.carryingComponent,
                inspection.functionsResolved ? "true" : "false",
                inspection.functionSignatureMask,
                presentedMelee,
                desired.meleeAttachmentSlot,
                presentedRanged,
                desired.rangedAttachmentSlot,
                attemptCount_);
            diagnostics_.Event(
                "MultiplayerRemoteEquipmentPending", detail);
        }
    }

    void RemoteHeroEquipmentController::Unbind() noexcept
    {
        transitions_.Unbind();
        hero_ = nullptr;
        nativeHero_ = nullptr;
        actorId_ = 0;
        applied_ = {};
        attempted_ = {};
        preparedWeapons_ = {};
        actionOverrideWeapons_ = {};
        prunedPresentation_ = {};
        activeFamily_ = CreatureWeaponFamily::None;
        actionOverrideFamily_ = CreatureWeaponFamily::None;
        actionOverrideMeleeSlot_ = 0;
        actionOverrideRangedSlot_ = 0;
        nextAttemptAt_ = 0;
        actionOverrideUntil_ = 0;
        nextPruneAttemptAt_ = 0;
        attemptCount_ = 0;
        pendingReported_ = false;
        usesHeroInventory_ = false;
        activeWeaponReady_ = false;
    }

    void RemoteHeroEquipmentController::Shutdown() noexcept
    {
        Unbind();
        transitions_.Shutdown();
        entities_ = nullptr;
        diagnostics_ = {};
    }
}
