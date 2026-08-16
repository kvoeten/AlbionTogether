#include "MultiplayerSession.h"

#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Entity/EntityService.h"
#include "Game/NPC/Village/VillageMembershipService.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Game/World/Travel/Hooks/WorldTravelObserver.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    std::string Utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr,
            nullptr);
        result.pop_back();
        return result;
    }

    std::uint64_t StablePlayerActorId(
        fable::multiplayer::PeerRole role,
        const std::string& playerId) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : playerId)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        hash ^= static_cast<std::uint8_t>(role);
        hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }
}

namespace fable::multiplayer
{
    MultiplayerSession::~MultiplayerSession()
    {
        Shutdown();
    }

    bool MultiplayerSession::Initialize(
        const automation::runtime::RuntimeConfiguration& configuration,
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::combat::CreatureCombatService& combat,
        game::creature::animation::CreatureAnimationService& animation,
        game::npc::village::VillageMembershipService& villages,
        game::npc::simulation::DummyVillagerService& dummyVillagers,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!configuration.MultiplayerEnabled())
        {
            return true;
        }

        diagnostics_ = diagnostics;
        const PeerRole role = configuration.MultiplayerRole() == L"host"
            ? PeerRole::Host
            : PeerRole::Guest;
        const std::string playerId = Utf8(
            configuration.MultiplayerPlayerId());
        const std::string appearanceDefinition = Utf8(
            configuration.MultiplayerAppearance());
        const std::uint64_t localActorId = StablePlayerActorId(role, playerId);

        if (!remotePlayers_.Initialize(
                entities, npcs, locomotion, look, combat, diagnostics_,
                localActorId))
        {
            Shutdown();
            return false;
        }
        const bool started = role == PeerRole::Host
            ? transport_.StartHost(
                configuration.MultiplayerPort(), localActorId, diagnostics_)
            : transport_.StartGuest(
                Utf8(configuration.MultiplayerAddress()),
                configuration.MultiplayerPort(), localActorId, diagnostics_);
        if (!started)
        {
            diagnostics_.Event("ClientFailed", "multiplayer-transport-start");
            Shutdown();
            return false;
        }
        localHero_.Initialize(
            entities, locomotion, localPlayerChannel_, transport_, diagnostics_,
            role, localActorId, playerId, appearanceDefinition,
            configuration.MorphSelfTest());
        authority_.Initialize(
            role, localActorId, transport_, diagnostics_);
        mapTransitionAuthority_.Initialize(authority_, diagnostics_);
        entityIdentities_.Initialize(diagnostics_);
        entityPresence_.Initialize(entityIdentities_, diagnostics_);
        entityLifecycle_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            diagnostics_);
        entityMaterialization_.Initialize(
            role,
            localActorId,
            entities,
            entityPresence_,
            entityIdentities_,
            villages,
            diagnostics_);
        hostWorldState_.Initialize(
            role,
            entityLifecycle_,
            entityIdentities_,
            diagnostics_);
        savedEntityMapBaseline_.Initialize(
            role,
            localActorId,
            transport_,
            diagnostics_);
        authority_.SetMapBaselineGate(&savedEntityMapBaseline_);
        populationSimulation_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            localHero_,
            diagnostics_);
        entityMovement_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            locomotion,
            look,
            diagnostics_);
        entityActions_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            entityPresence_,
            animation,
            combat,
            diagnostics_);
        entityVitals_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            combat,
            diagnostics_);
        entityLowSimulation_.Initialize(
            role,
            localActorId,
            transport_,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            dummyVillagers,
            diagnostics_);
        villageMembership_.Initialize(
            role,
            localActorId,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            villages,
            diagnostics_);
        entitySimulation_.Initialize(
            localActorId,
            authority_,
            entityLifecycle_,
            entityIdentities_,
            entityPresence_,
            diagnostics_);
        reliableMessages_.Initialize(transport_, diagnostics_);
        savedEntityConstructionGate_.Initialize(
            role,
            transport_,
            reliableMessages_,
            remotePlayerChannels_,
            authority_,
            diagnostics_);
        if (!reliableMessages_.Register(
                protocol::PacketType::Authority,
                authority_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-authority-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::EntityLifecycle,
                entityLifecycle_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-lifecycle-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::EntityAction,
                entityActions_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-action-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::EntityVitals,
                entityVitals_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-vitals-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::EntityLowSimulation,
                entityLowSimulation_))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-entity-low-simulation-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::PopulationState,
                populationSimulation_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-population-state-dispatch");
            Shutdown();
            return false;
        }
        if (!reliableMessages_.Register(
                protocol::PacketType::SavedEntityMapBaseline,
                savedEntityMapBaseline_))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-saved-entity-map-baseline-dispatch");
            Shutdown();
            return false;
        }
        enabled_ = true;

        char detail[320] = {};
        std::snprintf(
            detail, sizeof(detail),
            "role=%s player=%s actor_id=%llu authority_epoch=1 appearance=%s",
            role == PeerRole::Host ? "host" : "guest", playerId.c_str(),
            static_cast<unsigned long long>(localActorId),
            appearanceDefinition.c_str());
        diagnostics_.Event("MultiplayerSessionReady", detail);
        return true;
    }

    bool MultiplayerSession::AttachThingPresenceObserver(
        game::entity::presence::ThingPresenceObserver& observer)
    {
        return !enabled_ || entityPresence_.Attach(observer);
    }

    bool MultiplayerSession::AttachSavedEntityMapBlobObserver(
        game::entity::persistence::SavedEntityMapBlobObserver& observer)
    {
        return !enabled_ ||
            (savedEntityMapBaseline_.Attach(observer) &&
                savedEntityConstructionGate_.Attach(observer));
    }

    bool MultiplayerSession::AttachThingSaveProjectionHook(
        game::entity::persistence::ThingSaveProjectionHook& hook)
    {
        return !enabled_ || hostWorldState_.Attach(hook);
    }

    bool MultiplayerSession::AttachPopulationSimulationHook(
        game::npc::population::PopulationSimulationHook& hook)
    {
        return !enabled_ || populationSimulation_.Attach(hook);
    }

    bool MultiplayerSession::AttachCreatureActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        return !enabled_ ||
            (entityActions_.Attach(observer) &&
                entitySimulation_.AttachActionObserver(observer));
    }

    bool MultiplayerSession::AttachAiBrainUpdateObserver(
        game::creature::ai::AiBrainUpdateObserver& observer)
    {
        return !enabled_ || entitySimulation_.AttachBrainObserver(observer);
    }

    bool MultiplayerSession::AttachWorldTravelObserver(
        game::world::travel::WorldTravelObserver& observer)
    {
        return !enabled_ || mapTransitionAuthority_.Attach(observer);
    }

    bool MultiplayerSession::OnWorldReady()
    {
        return !enabled_ || localHero_.OnWorldReady();
    }

    bool MultiplayerSession::ProcessPresentationLifecycle()
    {
        if (!enabled_)
        {
            return false;
        }
        if (!entityPresence_.ProcessPending())
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-presence-processing");
        }
        if (!mapTransitionAuthority_.Process())
        {
            diagnostics_.Event(
                "MultiplayerMapPreparationDeferred",
                "ordered transport could not yet accept the native destination preparation");
        }
        std::uint16_t departingMapId = 0;
        if (mapTransitionAuthority_.ConsumeSourceDeparture(departingMapId) &&
            localHero_.IsWorldReady() &&
            departingMapId == localHero_.MapId())
        {
            departingEntityMap_ = localHero_.MapName();
            departingEntityMapId_ = departingMapId;
            sourceMapFinalDrainRequired_ = false;
            remotePlayers_.BeginWorldTransition();
            populationSimulation_.SetHighDetailReady(
                departingEntityMap_, false);
            entitySimulation_.Refresh(departingEntityMap_, false);
            localHero_.BeginWorldTransition();
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "map=%s map_id=%u; native region exit froze canonical lifecycle before local level teardown",
                departingEntityMap_.c_str(),
                static_cast<unsigned int>(departingEntityMapId_));
            diagnostics_.Event(
                "MultiplayerSourceMapLifecycleFrozen",
                detail);
            return true;
        }
        // The ordered control lane remains active through menus and world
        // teardown. This lets a guest stage a host map baseline before the
        // selected save or destination world consumes CSavedEntities.
        if (!reliableMessages_.Pump())
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-reliable-dispatch");
        }
        (void)authority_.ProcessControl();
        if (!localHero_.IsWorldReady())
        {
            // A guest can reach UE3 PrepareMapChange before its selected-save
            // world exists. Consume host PlayerState here so the held level
            // name can be resolved to the host-verified native map ID without
            // allowing retail NPC construction to race the saved-map baseline.
            const std::uint64_t controlNow = GetTickCount64();
            PlayerState controlState;
            while (transport_.TryConsume(controlState))
            {
                if ((controlState.changedProperties &
                        player_property::Retired) != 0)
                {
                    remotePlayerChannels_.Remove(controlState.actorId);
                    remotePlayers_.Remove(controlState.actorId);
                    entityVitals_.RetirePlayer(controlState.actorId);
                    continue;
                }
                remotePlayerChannels_.Apply(controlState, controlNow);
            }
            const auto controlSnapshots =
                remotePlayerChannels_.Snapshots();
            if (!authority_.Reconcile(nullptr, controlSnapshots))
            {
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-pre-world-authority-replication");
            }
            populationSimulation_.SetHighDetailReady(
                localHero_.MapName(), false);
            entitySimulation_.Refresh(
                !departingEntityMap_.empty()
                    ? departingEntityMap_
                    : localHero_.MapName(),
                false);
            if (!departingEntityMap_.empty() &&
                !ReconcileEntityLifecycle(
                    departingEntityMap_,
                    departingEntityMapId_,
                    false))
            {
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-source-map-teardown-drain");
            }
            if (localHero_.IsEntryPending())
            {
                localHero_.TryBind();
            }
            if (localHero_.ConsumeCompletedWorldTransition())
            {
                remotePlayers_.CompleteWorldTransition();
                sourceMapFinalDrainRequired_ = true;
            }
            return false;
        }
        if (localHero_.ConsumeCompletedWorldTransition())
        {
            remotePlayers_.CompleteWorldTransition();
            sourceMapFinalDrainRequired_ = true;
        }
        if (sourceMapFinalDrainRequired_)
        {
            if (!departingEntityMap_.empty() &&
                !ReconcileEntityLifecycle(
                    departingEntityMap_,
                    departingEntityMapId_,
                    false))
            {
                populationSimulation_.SetHighDetailReady(
                    localHero_.MapName(), false);
                entitySimulation_.Refresh(
                    departingEntityMap_, false);
                diagnostics_.Event(
                    "MultiplayerSourceMapHandoffDeferred",
                    "destination authority waits for the final local source teardown drain");
                return false;
            }
            ignoredDepartingEntityMapId_ = departingEntityMapId_;
            departingEntityMap_.clear();
            departingEntityMapId_ = 0;
            sourceMapFinalDrainRequired_ = false;
        }
        if (!localHero_.WorldIsCurrent())
        {
            departingEntityMap_ = localHero_.MapName();
            departingEntityMapId_ = localHero_.MapId();
            sourceMapFinalDrainRequired_ = false;
            if (!ReconcileEntityLifecycle(
                    departingEntityMap_,
                    departingEntityMapId_,
                    false))
            {
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-source-map-teardown-drain");
            }
            remotePlayers_.BeginWorldTransition();
            populationSimulation_.SetHighDetailReady(
                departingEntityMap_, false);
            entitySimulation_.Refresh(departingEntityMap_, false);
            localHero_.BeginWorldTransition();
            return true;
        }

        const std::uint64_t now = GetTickCount64();
        localHero_.CaptureAppearance(now);
        PlayerState inbound;
        while (transport_.TryConsume(inbound))
        {
            if ((inbound.changedProperties & player_property::Retired) != 0)
            {
                remotePlayerChannels_.Remove(inbound.actorId);
                remotePlayers_.Remove(inbound.actorId);
                entityVitals_.RetirePlayer(inbound.actorId);
                continue;
            }
            if (!remotePlayerChannels_.Apply(inbound, now))
            {
                continue;
            }
            if (remotePlayerChannels_.Size() > reportedRemotePlayerCount_)
            {
                reportedRemotePlayerCount_ = remotePlayerChannels_.Size();
                char detail[384] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "player=%s role=%s actor_id=%llu properties=0x%08X map=%s position=(%.3f,%.3f,%.3f) remote_count=%zu",
                    inbound.playerId.c_str(),
                    inbound.role == PeerRole::Host ? "host" : "guest",
                    static_cast<unsigned long long>(inbound.actorId),
                    inbound.changedProperties, inbound.mapName.c_str(),
                    inbound.position.x, inbound.position.y,
                    inbound.position.z, reportedRemotePlayerCount_);
                diagnostics_.Event("MultiplayerRemoteStateApplied", detail);
            }
        }
        const auto remoteSnapshots = remotePlayerChannels_.Snapshots();
        remotePlayers_.Reconcile(
            remoteSnapshots,
            localHero_.MapName(),
            localHero_.Hero());
        if (!authority_.Reconcile(
                localHero_.CurrentState(),
                remoteSnapshots))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-authority-replication");
        }
        if (!populationSimulation_.Process())
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-population-state-replication");
        }
        if (!entityMaterialization_.Reconcile(
                entityLifecycle_.Directory(),
                entityPresence_.LiveEntities(),
                authority_,
                localHero_.MapName(),
                localHero_.MapId()))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-materialization");
        }
        const bool ownerRosterReady = IsOwnerRosterReady(
            localHero_.MapName());
        populationSimulation_.SetHighDetailReady(
            localHero_.MapName(), ownerRosterReady);
        if (!ReconcileEntityLifecycle(
                localHero_.MapName(),
                localHero_.MapId()))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-lifecycle-replication");
        }
        hostWorldState_.Refresh();
        if (!villageMembership_.Process(entityPresence_.LiveEntities()))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-village-membership-replication");
        }
        if (!entityLowSimulation_.Process(
                entityPresence_.LiveEntities()))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-entity-low-simulation-replication");
        }
        if (!entityMovement_.Process(
                entityPresence_.LiveEntities(),
                localHero_.MapName(),
                ownerRosterReady))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-movement-replication");
        }
        if (!entityActions_.ProcessPending(
                localHero_.MapName(),
                ownerRosterReady))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-action-replication");
        }
        if (!entityVitals_.Process(
                localHero_,
                entityPresence_.LiveEntities(),
                remotePlayers_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-vitals-replication");
        }
        entitySimulation_.Refresh(
            localHero_.MapName(),
            ownerRosterReady);
        return false;
    }

    bool MultiplayerSession::ReconcileEntityLifecycle(
        const std::string& mapName,
        std::uint16_t mapId,
        bool publishLocalChanges)
    {
        if (mapName.empty() || mapId == 0)
        {
            return false;
        }
        std::vector<entities::LiveEntityChange> entityChanges;
        bool entityBaselineRequired = false;
        entityPresence_.TakeChanges(
            entityChanges,
            entityBaselineRequired);
        if (!publishLocalChanges)
        {
            if (!entityChanges.empty() || entityBaselineRequired)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "map=%s map_id=%u discarded_changes=%zu baseline_discarded=%s; level teardown does not mutate canonical world state",
                    mapName.c_str(),
                    static_cast<unsigned int>(mapId),
                    entityChanges.size(),
                    entityBaselineRequired ? "true" : "false");
                diagnostics_.Event(
                    "MultiplayerSourceMapTeardownDrained",
                    detail);
            }
            entityChanges.clear();
            entityBaselineRequired = false;
        }
        else if (ignoredDepartingEntityMapId_ != 0 &&
            ignoredDepartingEntityMapId_ != mapId)
        {
            entityChanges.erase(
                std::remove_if(
                    entityChanges.begin(),
                    entityChanges.end(),
                    [this](const entities::LiveEntityChange& change)
                    {
                        return change.kind == entities::LiveEntityChangeKind::
                                Unregistered &&
                            change.record.mapId ==
                                ignoredDepartingEntityMapId_;
                    }),
                entityChanges.end());
        }
        return entityLifecycle_.Reconcile(
            entityPresence_.LiveEntities(),
            entityChanges,
            entityBaselineRequired,
            mapName,
            mapId,
            publishLocalChanges && IsOwnerRosterReady(mapName));
    }

    bool MultiplayerSession::IsOwnerRosterReady(
        const std::string& mapName) const noexcept
    {
        const authority::MapAuthorityLease* const lease =
            authority_.FindMapLease(mapName);
        if (lease == nullptr || lease->epoch == 0)
        {
            return false;
        }
        return !lease->localAuthority ||
            entityMaterialization_.IsLocalRosterReady(
                mapName,
                lease->epoch);
    }

    void MultiplayerSession::DriveReplicatedMovement()
    {
        if (enabled_ && localHero_.IsWorldReady())
        {
            remotePlayers_.DriveMovement();
            entityMovement_.Drive();
        }
    }

    void MultiplayerSession::Shutdown() noexcept
    {
        // Detach native mutation/action callbacks before retiring the actor
        // registries and presentations they publish into.
        villageMembership_.Shutdown();
        entityLowSimulation_.Shutdown();
        entityVitals_.Shutdown();
        entityActions_.Shutdown();
        localHero_.Shutdown();
        remotePlayers_.Shutdown();
        entitySimulation_.Shutdown();
        entityMovement_.Shutdown();
        entityMaterialization_.Shutdown();
        entityPresence_.Shutdown();
        populationSimulation_.Shutdown();
        mapTransitionAuthority_.Shutdown();
        savedEntityConstructionGate_.Shutdown();
        authority_.SetMapBaselineGate(nullptr);
        savedEntityMapBaseline_.Shutdown();
        hostWorldState_.Shutdown();
        entityLifecycle_.Shutdown();
        entityIdentities_.Clear();
        localPlayerChannel_.Close();
        remotePlayerChannels_.Clear();
        reliableMessages_.Shutdown();
        authority_.Shutdown();
        transport_.Shutdown();
        diagnostics_ = {};
        departingEntityMap_.clear();
        departingEntityMapId_ = 0;
        ignoredDepartingEntityMapId_ = 0;
        sourceMapFinalDrainRequired_ = false;
        enabled_ = false;
        reportedRemotePlayerCount_ = 0;
    }

    bool MultiplayerSession::IsEnabled() const noexcept
    {
        return enabled_;
    }

    bool MultiplayerSession::IsWorldReady() const noexcept
    {
        return localHero_.IsWorldReady();
    }

    bool MultiplayerSession::TransferOwnedEntity(
        std::uint64_t entityUid,
        std::uint16_t destinationMapId,
        const game::Vector3& destinationPosition,
        float destinationFacing)
    {
        return enabled_ &&
            entityPresence_.UnregisterLocalPresence(entityUid) &&
            entityLifecycle_.SubmitOwnedTransfer(
                entityUid,
                destinationMapId,
                destinationPosition,
                destinationFacing);
    }

    bool MultiplayerSession::HasActiveRemotePresentation() const
    {
        return enabled_ && remotePlayers_.ActiveCount() != 0;
    }
}
