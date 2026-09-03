#include "EntitySimulationAuthority.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
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

    bool IsVictimPresentationAction(void* action) noexcept
    {
        char actionType[128] = {};
        if (!fable::game::creature::actions::
                CreatureActionLifecycleObserver::DescribeActionType(
                    action, actionType, sizeof(actionType)))
        {
            return false;
        }

        constexpr char genericStrikePrefix[] =
            "CCombatAction_GenericStrikeResponse";
        return std::strncmp(
                actionType,
                genericStrikePrefix,
                sizeof(genericStrikePrefix) - 1) == 0 ||
            std::strcmp(actionType, "CCreatureAction_BeingForcePushed") == 0 ||
            std::strcmp(actionType, "CCreatureAction_BlockRespond") == 0;
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
        combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        localActorId_ = localActorId;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        presence_ = &presence;
        combatants_ = &combatants;
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
        brainObserver_->SetStateGroupExecutionSink(
            &EntitySimulationAuthority::ShouldExecuteStateGroup,
            this);
        diagnostics_.Event(
            "MultiplayerEntityBrainAuthorityAttached",
            "non-publisher CAIBrain and direct state-group decisions are suppressed without removing native creature components");
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
            const std::uint64_t canonicalUid =
                identities_->Canonicalize(live.thingUid);
            const entities::WorldEntityRecord* const record =
                lifecycle_->Directory().Find(canonicalUid);
            const bool isPlayerPresentation =
                entities::LiveEntityRegistry::IsPlayerPresentation(live);
            if (isPlayerPresentation)
            {
                ++playerPresentationCount;
            }
            std::uint64_t playerActorId = combatants_->FindActor(live.thing);
            if (playerActorId == 0)
            {
                playerActorId = combatants_->FindActorByThingUid(canonicalUid);
            }

            // Known player combatants get an explicit decision: only the
            // local actor may run native simulation.
            if (playerActorId != 0)
            {
                const std::uint64_t localUid = identities_->FindLocal(canonicalUid);
                const std::uint64_t nativeUid = localUid != 0
                    ? localUid
                    : canonicalUid;
                const bool canSimulate = playerActorId == localActorId_;
                next->byCreature[live.thing] = {
                    nativeUid, canSimulate};
                if (canSimulate)
                {
                    ++localSimulationCount;
                }
                else
                {
                    ++fencedCount;
                }
                continue;
            }

            // A canonical world record takes precedence over the presentation
            // label: world entities with a Hero morph still follow publisher
            // authority. An unbound player presentation is fenced until its
            // actor binding is available.
            if (record == nullptr && isPlayerPresentation)
            {
                std::uint64_t nativeUid = identities_->FindLocal(canonicalUid);
                if (nativeUid == 0)
                {
                    nativeUid = canonicalUid;
                }
                next->byCreature[live.thing] = {nativeUid, false};
                ++fencedCount;
                continue;
            }
            if (!entities::LiveEntityRegistry::IsReplicable(live))
            {
                continue;
            }
            ++replicableCount;

            bool canSimulate = false;
            if (record == nullptr)
            {
                // A creature observed before its ordered lifecycle record has
                // no authoritative incarnation yet. It cannot simulate until
                // the host directory and map lease classify it.
                canSimulate = false;
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
            brainObserver_->SetStateGroupExecutionSink(nullptr, nullptr);
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
        combatants_ = nullptr;
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
        void* action) noexcept
    {
        // Hit reactions are presentation consequences, not simulation
        // decisions. Let the retail OnHit path submit and update them on a
        // fenced replica so the attacker sees the native flinch/knockdown and
        // impact effects immediately. Health remains protected and the
        // reliable CombatHit result suppresses duplicate replay after it
        // correlates this native response.
        if (IsVictimPresentationAction(action))
        {
            return true;
        }
        const auto* const simulation =
            static_cast<const EntitySimulationAuthority*>(context);
        return simulation == nullptr || simulation->CanSimulate(creature);
    }

    bool EntitySimulationAuthority::ShouldExecuteStateGroup(
        void* context,
        void* creature,
        int frameTime,
        void* nativeProposal) noexcept
    {
        const auto* const simulation =
            static_cast<const EntitySimulationAuthority*>(context);
        (void)frameTime;
        (void)nativeProposal;
        return simulation == nullptr || simulation->CanSimulate(creature);
    }

    bool EntitySimulationAuthority::CanSimulate(void* creature) const noexcept
    {
        if (creature == nullptr)
        {
            return false;
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
        // Once a decision snapshot exists, a newly observed creature is
        // fenced until the next reconciliation classifies its incarnation.
        // Before the first world snapshot, the null-table case above remains
        // fail-open for retail bootstrap.
        if (decision == decisions->byCreature.end())
        {
            return false;
        }

        std::uint64_t nativeUid = 0;
        const bool readable = ReadNativeUid(creature, nativeUid);
        // A native address can be reused between reconciliation passes. Never
        // apply an old NPC decision to a newly constructed Thing (especially
        // a Hero presentation) that happens to occupy the same address.
        return readable && nativeUid == decision->second.nativeUid &&
            decision->second.canSimulate;
    }
}
