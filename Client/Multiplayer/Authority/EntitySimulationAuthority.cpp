#include "EntitySimulationAuthority.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    bool ReadNativeUid(void* creature, std::uint64_t& uid) noexcept
    {
        uid = 0;
        bool readable = false;
        __try
        {
            if (creature != nullptr)
            {
                uid = *reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::uint8_t*>(creature) + 0x14);
                readable = uid != 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
            readable = false;
        }
        return readable;
    }
}

namespace fable::multiplayer::authority
{
    void EntitySimulationAuthority::Initialize(
        std::uint64_t localActorId,
        AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        entities::EntityPresenceReplication& presence,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        localActorId_ = localActorId;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        presence_ = &presence;
        diagnostics_ = diagnostics;
        diagnostics_.Event(
            "MultiplayerEntitySimulationAuthorityReady",
            "native AI decisions and action submission use the entity publisher lease");
        std::atomic_store_explicit(
            &decisions_,
            std::shared_ptr<const DecisionSnapshot>(
                std::make_shared<DecisionSnapshot>()),
            std::memory_order_release);
    }

    bool EntitySimulationAuthority::AttachBrainObserver(
        game::creature::ai::AiBrainUpdateObserver& observer)
    {
        if (localActorId_ == 0 || authority_ == nullptr ||
            lifecycle_ == nullptr || identities_ == nullptr ||
            presence_ == nullptr ||
            !observer.IsInstalled())
        {
            return false;
        }
        brainObserver_ = &observer;
        brainObserver_->SetExecutionSink(
            &EntitySimulationAuthority::ShouldExecuteBrain,
            this);
        diagnostics_.Event(
            "MultiplayerEntityBrainAuthorityAttached",
            "non-publisher CAIBrain updates are suppressed without removing native creature components");
        return true;
    }

    bool EntitySimulationAuthority::AttachActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (localActorId_ == 0 || authority_ == nullptr ||
            lifecycle_ == nullptr || identities_ == nullptr ||
            presence_ == nullptr ||
            !observer.IsInstalled())
        {
            return false;
        }
        actionObserver_ = &observer;
        actionObserver_->SetAuthorityGate(
            &EntitySimulationAuthority::ShouldSubmitAction,
            this);
        diagnostics_.Event(
            "MultiplayerEntityActionAuthorityAttached",
            "non-publisher native creature action submissions are rejected at the retail boundary");
        return true;
    }

    void EntitySimulationAuthority::Refresh(
        const std::string& localMap,
        bool ownerRosterReady)
    {
        if (localActorId_ == 0 || authority_ == nullptr ||
            lifecycle_ == nullptr || identities_ == nullptr ||
            presence_ == nullptr)
        {
            return;
        }

        auto next = std::make_shared<DecisionSnapshot>();
        const std::vector<entities::LiveEntityRecord> liveEntities =
            presence_->LiveEntities().Snapshot();
        next->byCreature.reserve(liveEntities.size());
        std::size_t creatureCount = 0;
        std::size_t playerPresentationCount = 0;
        std::size_t replicableCount = 0;
        std::size_t localSimulationCount = 0;
        std::size_t fencedCount = 0;
        for (const entities::LiveEntityRecord& live : liveEntities)
        {
            if (live.thing == nullptr || !live.creature)
            {
                continue;
            }
            ++creatureCount;
            if (entities::LiveEntityRegistry::IsPlayerPresentation(live))
            {
                ++playerPresentationCount;
                continue;
            }
            if (!entities::LiveEntityRegistry::IsReplicable(live))
            {
                continue;
            }
            ++replicableCount;

            const std::uint64_t canonicalUid =
                identities_->Canonicalize(live.thingUid);
            const entities::WorldEntityRecord* const record =
                lifecycle_->Directory().Find(canonicalUid);
            bool canSimulate = true;
            if (record == nullptr)
            {
                // A live non-Hero creature can be observed before its reliable
                // lifecycle record. Fence it when the map lease is already
                // known; otherwise remain fail-open during engine bootstrap.
                const std::string* const mapName =
                    authority_->ResolveMapName(live.mapId);
                const MapAuthorityLease* const lease = mapName != nullptr
                    ? authority_->FindMapLease(*mapName)
                    : nullptr;
                if (!ownerRosterReady && mapName != nullptr &&
                    *mapName == localMap)
                {
                    canSimulate = false;
                }
                else if (lease != nullptr && lease->epoch != 0)
                {
                    canSimulate = lease->actorId == localActorId_;
                }
            }
            else if (!record->available || !record->live ||
                !record->creature || record->mapId == 0 ||
                record->mapId != live.mapId || record->mapName.empty() ||
                record->mapEpoch == 0)
            {
                canSimulate = false;
            }
            else
            {
                canSimulate = ownerRosterReady ||
                    record->mapName != localMap;
                if (canSimulate)
                {
                    canSimulate = authority_->IsEntityPublisher(
                        {record->thingUid, record->generation},
                        record->mapName,
                        localActorId_,
                        record->mapEpoch);
                }
            }
            std::uint64_t nativeUid = identities_->FindLocal(canonicalUid);
            if (nativeUid == 0)
            {
                nativeUid = canonicalUid;
            }
            next->byCreature[live.thing] = {nativeUid, canSimulate};
            if (canSimulate)
            {
                ++localSimulationCount;
            }
            else
            {
                ++fencedCount;
            }
        }

        if (!coverageReported_ || reportedCoverageMap_ != localMap ||
            reportedCreatureCount_ != creatureCount ||
            reportedPlayerPresentationCount_ != playerPresentationCount ||
            reportedReplicableCount_ != replicableCount ||
            reportedLocalSimulationCount_ != localSimulationCount ||
            reportedFencedCount_ != fencedCount ||
            reportedOwnerRosterReady_ != ownerRosterReady)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "map=%s roster_ready=%s creatures=%zu player_presentations=%zu replicable_npcs=%zu local_simulation=%zu fenced=%zu",
                localMap.c_str(),
                ownerRosterReady ? "true" : "false",
                creatureCount,
                playerPresentationCount,
                replicableCount,
                localSimulationCount,
                fencedCount);
            diagnostics_.Event(
                "MultiplayerEntitySimulationCoverage",
                detail);
            reportedCoverageMap_ = localMap;
            reportedCreatureCount_ = creatureCount;
            reportedPlayerPresentationCount_ = playerPresentationCount;
            reportedReplicableCount_ = replicableCount;
            reportedLocalSimulationCount_ = localSimulationCount;
            reportedFencedCount_ = fencedCount;
            reportedOwnerRosterReady_ = ownerRosterReady;
            coverageReported_ = true;
        }

        std::atomic_store_explicit(
            &decisions_,
            std::shared_ptr<const DecisionSnapshot>(std::move(next)),
            std::memory_order_release);
    }

    void EntitySimulationAuthority::Shutdown() noexcept
    {
        if (brainObserver_ != nullptr)
        {
            brainObserver_->SetExecutionSink(nullptr, nullptr);
        }
        if (actionObserver_ != nullptr)
        {
            actionObserver_->SetAuthorityGate(nullptr, nullptr);
        }
        brainObserver_ = nullptr;
        actionObserver_ = nullptr;
        std::atomic_store_explicit(
            &decisions_,
            std::shared_ptr<const DecisionSnapshot>(),
            std::memory_order_release);
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        reportedCoverageMap_.clear();
        reportedCreatureCount_ = 0;
        reportedPlayerPresentationCount_ = 0;
        reportedReplicableCount_ = 0;
        reportedLocalSimulationCount_ = 0;
        reportedFencedCount_ = 0;
        reportedOwnerRosterReady_ = false;
        coverageReported_ = false;
    }

    bool EntitySimulationAuthority::ShouldExecuteBrain(
        void* context,
        void* ownerThing) noexcept
    {
        const auto* const simulation =
            static_cast<const EntitySimulationAuthority*>(context);
        return simulation == nullptr || simulation->CanSimulate(ownerThing);
    }

    bool EntitySimulationAuthority::ShouldSubmitAction(
        void* context,
        void* creature,
        void*) noexcept
    {
        const auto* const simulation =
            static_cast<const EntitySimulationAuthority*>(context);
        return simulation == nullptr || simulation->CanSimulate(creature);
    }

    bool EntitySimulationAuthority::CanSimulate(void* creature) const noexcept
    {
        if (creature == nullptr)
        {
            return true;
        }
        const std::shared_ptr<const DecisionSnapshot> decisions =
            std::atomic_load_explicit(
                &decisions_,
                std::memory_order_acquire);
        if (decisions == nullptr)
        {
            return true;
        }
        const auto decision = decisions->byCreature.find(creature);
        // Unknown Things remain fail-open so the Hero and engine bootstrap
        // objects are never caught by an NPC policy. Refresh publishes an
        // explicit decision as soon as Mapwho reports a replicable creature.
        if (decision == decisions->byCreature.end())
        {
            return true;
        }

        std::uint64_t nativeUid = 0;
        const bool readable = ReadNativeUid(creature, nativeUid);
        // A native address can be reused between reconciliation passes. Never
        // apply an old NPC decision to a newly constructed Thing (especially
        // a Hero presentation) that happens to occupy the same address.
        return !readable || nativeUid != decision->second.nativeUid ||
            decision->second.canSimulate;
    }
}
