#include "OwnerScopedPresentationAcceptanceDriver.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/HeroPawn/Appearance/Native/HeroClothingComponent.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"
#include "MapStressAcceptanceDriver.h"

#include <Windows.h>

#include <cstdio>

namespace fable::automation::multiplayer::transition
{
    namespace
    {
        using game::creature::equipment::CreatureWeaponFamily;
        using game::hero_pawn::appearance::HeroClothingState;
        using game::hero_pawn::equipment::HeroEquipmentState;

        bool SameDurableEquipmentState(
            const HeroEquipmentState& left,
            const HeroEquipmentState& right) noexcept
        {
            // Construction checkpoints compare current inventory
            // presentation. The action ID belongs to the map incarnation
            // that produced a live transition and is intentionally reset by
            // the destination baseline.
            return left.valid == right.valid &&
                left.meleeDefinitionIndex == right.meleeDefinitionIndex &&
                left.rangedDefinitionIndex == right.rangedDefinitionIndex &&
                left.meleeAttachmentSlot == right.meleeAttachmentSlot &&
                left.rangedAttachmentSlot == right.rangedAttachmentSlot &&
                left.activeFamily == right.activeFamily;
        }

    }

    void OwnerScopedPresentationAcceptanceDriver::Initialize(
        const bool enabled,
        const bool host,
        game::EntityService& entities,
        ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
        const MapStressAcceptanceDriver& mapStress,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        enabled_ = enabled;
        if (!enabled_)
        {
            return;
        }
        entities_ = &entities;
        multiplayer_ = &multiplayer;
        mapStress_ = &mapStress;
        diagnostics_ = diagnostics;
        host_ = host;
        Report(
            "MultiplayerOwnerScopedPresentationStabilityArmed",
            host_
                ? "owner=host mutation=clothing-slot-0-and-active-weapon"
                : "observer=guest assertions=owner-scoped-remote-state");
    }

    void OwnerScopedPresentationAcceptanceDriver::Tick() noexcept
    {
        if (!enabled_ || completed_ || failed_ || entities_ == nullptr ||
            multiplayer_ == nullptr || mapStress_ == nullptr)
        {
            return;
        }
        if (mapStress_->HasFailed())
        {
            Fail("map-stress-failed-before-owner-presentation-check");
            return;
        }
        if (!CaptureLocalBaseline())
        {
            return;
        }
        if (!ReadRemoteBaselines())
        {
            return;
        }

        // The stress route is allowed to finish with the peers in different
        // maps. Once enough same-map reconstruction checkpoints have already
        // passed, that final split is not a presentation failure.
        if (mapStress_->IsComplete() &&
            stableCheckpointCount_ >= RequiredStableCheckpoints)
        {
            completed_ = true;
            Report(
                "MultiplayerOwnerScopedPresentationStabilityComplete",
                "owner-only-clothing-and-equipment-survived-map-rebuild-overlap");
            return;
        }

        if (host_ && !ownerMutationApplied_ && mapStress_->IsStableSameMap())
        {
            if (mapStress_->IsComplete())
            {
                Fail("map-stress-completed-before-owner-mutation");
                return;
            }
            const std::uint64_t now = GetTickCount64();
            if (now < nextMutationAttemptAt_)
            {
                return;
            }
            if (!ApplyOwnerMutation())
            {
                nextMutationAttemptAt_ = now + MutationRetryMilliseconds;
            }
            return;
        }

        if (ownerMutationApplied_ && !ownerMutationObserved_)
        {
            if (!ReadLocalState(ownerClothingAfter_, ownerEquipmentAfter_))
            {
                return;
            }
            ownerMutationObserved_ =
                !ownerClothingAfter_.Equals(ownerClothingBefore_) &&
                !SameDurableEquipmentState(
                    ownerEquipmentAfter_, ownerEquipmentBefore_);
            if (!ownerMutationObserved_)
            {
                return;
            }
            mutationObservedAt_ = GetTickCount64();
            Report(
                "MultiplayerOwnerScopedPresentationMutationObserved",
                "owner-local=changed state=clothing-and-equipment");
        }

        if (!mapStress_->IsStableSameMap())
        {
            if (mapStress_->IsComplete())
            {
                Fail("map-stress-completed-without-stable-overlap-checkpoint");
            }
            return;
        }
        if (host_ && (!ownerMutationApplied_ || !ownerMutationObserved_))
        {
            return;
        }
        if (lastValidatedTransitionOrdinal_ ==
            mapStress_->TransitionOrdinal())
        {
            return;
        }
        if (!ValidateCheckpoint())
        {
            if (mapStress_->IsComplete())
            {
                Fail("map-stress-completed-before-owner-overlap-observed");
            }
            return;
        }
        lastValidatedTransitionOrdinal_ = mapStress_->TransitionOrdinal();
        if (stableCheckpointCount_ >= RequiredStableCheckpoints &&
            mapStress_->IsComplete())
        {
            completed_ = true;
            Report(
                "MultiplayerOwnerScopedPresentationStabilityComplete",
                "owner-only-clothing-and-equipment-survived-map-rebuild-overlap");
        }
    }

    bool OwnerScopedPresentationAcceptanceDriver::CaptureLocalBaseline() noexcept
    {
        if (ownerClothingBefore_.valid && ownerEquipmentBefore_.valid)
        {
            return true;
        }
        if (!ReadLocalState(ownerClothingBefore_, ownerEquipmentBefore_))
        {
            return false;
        }
        Report(
            "MultiplayerOwnerScopedPresentationBaselineCaptured",
            "local-hero-clothing-and-equipment-baseline-ready");
        return true;
    }

    bool OwnerScopedPresentationAcceptanceDriver::ApplyOwnerMutation() noexcept
    {
        game::Entity* const hero = entities_->GetHero();
        if (hero == nullptr || !hero->IsValid())
        {
            if (hero != nullptr)
            {
                hero->Release();
            }
            return false;
        }
        void* const nativeHero = entities_->ResolveNative(hero->NativeHandle());
        bool clothingChanged = false;
        bool equipmentChanged = false;
        if (nativeHero != nullptr)
        {
            ownerClothingAfter_ = ownerClothingBefore_;
            // Removing the first occupied category is deterministic and does
            // not invent an item ID or mutate the player's inventory.
            for (std::size_t slot = 0;
                 slot < HeroClothingState::SlotCount;
                 ++slot)
            {
                if (ownerClothingAfter_.definitionIndices[slot] != -1)
                {
                    ownerClothingAfter_.definitionIndices[slot] = -1;
                    clothingChanged = game::hero_pawn::appearance::native::
                        HeroClothingComponent::Apply(
                            nativeHero,
                            ownerClothingAfter_) || clothingChanged;
                    break;
                }
            }
            ownerEquipmentAfter_ = ownerEquipmentBefore_;
            CreatureWeaponFamily requested =
                ownerEquipmentBefore_.activeFamily;
            if (ownerEquipmentBefore_.rangedDefinitionIndex > 0 &&
                requested != CreatureWeaponFamily::Ranged)
            {
                requested = CreatureWeaponFamily::Ranged;
            }
            else if (ownerEquipmentBefore_.meleeDefinitionIndex > 0 &&
                requested != CreatureWeaponFamily::Melee)
            {
                requested = CreatureWeaponFamily::Melee;
            }
            else if (requested != CreatureWeaponFamily::None)
            {
                requested = CreatureWeaponFamily::None;
            }
            if (requested != ownerEquipmentBefore_.activeFamily &&
                game::hero_pawn::equipment::native::HeroWeaponComponent::
                    RequestActiveFamily(
                        entities_->Interface(), hero->NativeHandle(), requested))
            {
                ownerEquipmentAfter_.activeFamily = requested;
                equipmentChanged = true;
            }
        }
        hero->Release();
        if (!clothingChanged || !equipmentChanged)
        {
            return false;
        }
        ownerMutationApplied_ = true;
        nextMutationAttemptAt_ = 0;
        Report(
            "MultiplayerOwnerScopedPresentationMutationApplied",
            "owner=host mutation=one-clothing-category-and-active-weapon");
        return true;
    }

    bool OwnerScopedPresentationAcceptanceDriver::ReadLocalState(
        HeroClothingState& clothing,
        HeroEquipmentState& equipment) const noexcept
    {
        if (entities_ == nullptr)
        {
            return false;
        }
        game::Entity* const hero = entities_->GetHero();
        if (hero == nullptr || !hero->IsValid())
        {
            if (hero != nullptr)
            {
                hero->Release();
            }
            return false;
        }
        void* const nativeHero = entities_->ResolveNative(hero->NativeHandle());
        const bool ready = nativeHero != nullptr &&
            game::hero_pawn::appearance::native::HeroClothingComponent::
                Capture(nativeHero, clothing) &&
            game::hero_pawn::equipment::native::HeroWeaponComponent::
                Capture(nativeHero, equipment);
        hero->Release();
        return ready;
    }

    bool OwnerScopedPresentationAcceptanceDriver::ReadRemoteBaselines() noexcept
    {
        const auto snapshots = multiplayer_->Contexts().transport.
            remotePlayerChannels.Snapshots();
        for (const auto& snapshot : snapshots)
        {
            if (!snapshot.lifecycle.active || snapshot.state.actorId == 0 ||
                !snapshot.state.heroClothing.IsSane() ||
                !snapshot.state.heroEquipment.IsSane())
            {
                continue;
            }
            ActorBaseline* found = nullptr;
            for (auto& baseline : baselines_)
            {
                if (baseline.actorId == snapshot.state.actorId)
                {
                    found = &baseline;
                    break;
                }
                if (found == nullptr && baseline.actorId == 0)
                {
                    found = &baseline;
                }
            }
            if (found == nullptr)
            {
                Fail("owner-scoped-presentation-baseline-capacity");
                return false;
            }
            if (!found->captured)
            {
                found->actorId = snapshot.state.actorId;
                found->clothing = snapshot.state.heroClothing;
                found->equipment = snapshot.state.heroEquipment;
                found->captured = true;
            }
        }
        return true;
    }

    bool OwnerScopedPresentationAcceptanceDriver::ValidateCheckpoint() noexcept
    {
        if (host_ && (!ownerMutationApplied_ || !ownerMutationObserved_))
        {
            return true;
        }
        const auto snapshots = multiplayer_->Contexts().transport.
            remotePlayerChannels.Snapshots();
        bool remoteOwnerChanged = false;
        bool remoteClothingChanged = false;
        bool remoteEquipmentChanged = false;
        std::uint64_t verifiedRemoteActorId = 0;
        for (const auto& snapshot : snapshots)
        {
            if (!snapshot.lifecycle.active || snapshot.state.actorId == 0)
            {
                continue;
            }
            ActorBaseline* baseline = nullptr;
            for (auto& candidate : baselines_)
            {
                if (candidate.actorId == snapshot.state.actorId)
                {
                    baseline = &candidate;
                    break;
                }
            }
            if (baseline == nullptr || !baseline->captured)
            {
                continue;
            }
            const bool clothingChanged =
                !baseline->clothing.Equals(snapshot.state.heroClothing);
            const bool equipmentChanged =
                !SameDurableEquipmentState(
                    baseline->equipment, snapshot.state.heroEquipment);
            if (host_ && clothingChanged)
            {
                Fail("wrong-owner-remote-clothing-changed");
                return false;
            }
            if (!host_ && clothingChanged && equipmentChanged)
            {
                remoteOwnerChanged = true;
                remoteClothingChanged = true;
                remoteEquipmentChanged = true;
                verifiedRemoteActorId = snapshot.state.actorId;
                if (!baseline->expectedCaptured)
                {
                    baseline->expectedClothing = snapshot.state.heroClothing;
                    baseline->expectedEquipment = snapshot.state.heroEquipment;
                    baseline->expectedCaptured = true;
                }
                else if (!baseline->expectedClothing.Equals(
                             snapshot.state.heroClothing) ||
                    !SameDurableEquipmentState(
                        baseline->expectedEquipment,
                        snapshot.state.heroEquipment))
                {
                    Fail("stale-queued-remote-presentation-state");
                    return false;
                }
            }
            if (host_ && !clothingChanged)
            {
                verifiedRemoteActorId = snapshot.state.actorId;
            }
        }
        if (!host_)
        {
            HeroClothingState localClothing;
            HeroEquipmentState localEquipment;
            if (!ReadLocalState(localClothing, localEquipment))
            {
                return false;
            }
            if (!localClothing.Equals(ownerClothingBefore_))
            {
                Fail("observer-local-clothing-mutated-by-remote-owner");
                return false;
            }
        }
        if (!host_ && !remoteOwnerChanged)
        {
            return false;
        }
        char verification[256] = {};
        std::snprintf(
            verification,
            sizeof(verification),
            "actor_id=%llu clothing_changed=%s equipment_changed=%s local_owner_unchanged=%s",
            static_cast<unsigned long long>(verifiedRemoteActorId),
            remoteClothingChanged ? "true" : "false",
            remoteEquipmentChanged ? "true" : "false",
            host_ ? "n/a" : "true");
        Report(
            "MultiplayerOwnerScopedRemotePresentationVerified",
            verification);
        ++stableCheckpointCount_;
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s checkpoint=%u remote_owner_changed=%s local_owner_unchanged=%s mutation_age_ms=%llu",
            host_ ? "host" : "guest",
            stableCheckpointCount_,
            remoteOwnerChanged ? "true" : "false",
            host_ ? "n/a" : "true",
            static_cast<unsigned long long>(mutationObservedAt_ == 0
                ? 0
                : GetTickCount64() - mutationObservedAt_));
        Report("MultiplayerOwnerScopedPresentationCheckpoint", detail);
        return true;
    }

    void OwnerScopedPresentationAcceptanceDriver::Fail(
        const char* const reason) noexcept
    {
        if (failed_)
        {
            return;
        }
        failed_ = true;
        Report(
            "MultiplayerOwnerScopedPresentationFailed",
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event("ClientFailed", "multiplayer-owner-scoped-presentation");
    }

    void OwnerScopedPresentationAcceptanceDriver::Report(
        const char* const event,
        const char* const detail) const noexcept
    {
        diagnostics_.Event(event, detail);
    }

    void OwnerScopedPresentationAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        multiplayer_ = nullptr;
        mapStress_ = nullptr;
        diagnostics_ = {};
        baselines_ = {};
        ownerClothingBefore_ = {};
        ownerClothingAfter_ = {};
        ownerEquipmentBefore_ = {};
        ownerEquipmentAfter_ = {};
        nextMutationAttemptAt_ = 0;
        mutationObservedAt_ = 0;
        stableCheckpointCount_ = 0;
        lastValidatedTransitionOrdinal_ =
            static_cast<unsigned int>(-1);
        host_ = false;
        enabled_ = false;
        ownerMutationApplied_ = false;
        ownerMutationObserved_ = false;
        completed_ = false;
        failed_ = false;
    }
}
