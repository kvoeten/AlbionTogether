#include "PresentationLifecycleCoordinator.h"

#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"
#include "Multiplayer/Runtime/RemotePlayerLifecycleInvalidation.h"

#include <Windows.h>
#include <cstdio>

namespace fable::multiplayer
{
    void PresentationLifecycleCoordinator::InvalidateRemotePlayerState(
        MultiplayerRuntimeGraph& graph) noexcept
    {
        auto& contexts = graph.Contexts();
        RemotePlayerLifecycleInvalidation::Apply(
            contexts.transport.remotePlayerChannels,
            contexts.actions.playerActions,
            contexts.actions.entityVitals);
    }

    void PresentationLifecycleCoordinator::Reset() noexcept
    {
        departingEntityMap_.clear();
        departingEntityMapId_ = 0;
        ignoredDepartingEntityMapId_ = 0;
        sourceMapFinalDrainRequired_ = false;
        reportedRemotePlayerCount_ = 0;
    }

    bool PresentationLifecycleCoordinator::Process(MultiplayerRuntimeGraph& graph)
    {
        if (!graph.IsEnabled()) return false;
        auto& contexts = graph.Contexts();
        auto& transport = contexts.transport;
        auto& players = contexts.players;
        auto& world = contexts.world;
        auto& entities = contexts.entities;
        auto& actions = contexts.actions;
        auto& diagnostics = graph.Diagnostics();
        auto& localHero = players.localHero;
        auto& remotePlayers = players.remotePlayers;
        auto& authority = world.authority;
        auto& entityPresence = entities.entityPresence;
        auto& entityLifecycle = entities.entityLifecycle;
        auto& entityMaterialization = entities.entityMaterialization;
        auto& entitySimulation = world.entitySimulation;

        if (!entityPresence.ProcessPending()) diagnostics.Event("ClientFailed", "multiplayer-entity-presence-processing");
        if (!world.mapTransitionAuthority.Process()) diagnostics.Event("MultiplayerMapPreparationDeferred", "ordered transport could not yet accept the native destination preparation");
        std::uint16_t departingMapId = 0;
        const bool connectedExitDeparted =
            world.mapTransitionAuthority.ConsumeSourceDeparture(
                departingMapId) &&
            localHero.IsWorldReady() &&
            departingMapId == localHero.MapId();
        const bool nativePresenceDeparted =
            localHero.HasDepartedNativeWorld(
                entityPresence.LiveEntities());
        if (connectedExitDeparted || nativePresenceDeparted) {
            departingEntityMap_ = localHero.MapName(); departingEntityMapId_ = departingMapId; sourceMapFinalDrainRequired_ = false;
            if (departingEntityMapId_ == 0) departingEntityMapId_ = localHero.MapId();
            if (!players.actorState.RetireLocal()) diagnostics.Event("ClientFailed", "multiplayer-player-actor-retire");
            remotePlayers.BeginWorldTransition(); world.populationSimulation.SetHighDetailReady(departingEntityMap_, false); entitySimulation.Refresh(departingEntityMap_, false); localHero.BeginWorldTransition();
            char detail[224] = {}; std::snprintf(detail, sizeof(detail), "map=%s map_id=%u boundary=%s; canonical lifecycle froze before local level teardown", departingEntityMap_.c_str(), static_cast<unsigned int>(departingEntityMapId_), connectedExitDeparted ? "connected-region-exit" : "native-mapwho-unregister"); diagnostics.Event("MultiplayerSourceMapLifecycleFrozen", detail);
            return true;
        }
        // Queue late-join baselines before dispatching any newly arrived
        // reliable actions, then process accepted lifecycle messages again.
        if (!graph.ProcessPlayerActorState()) diagnostics.Event("ClientFailed", "multiplayer-player-actor-state-replication");
        InvalidateRemotePlayerState(graph);
        if (!transport.reliableMessages.Pump()) diagnostics.Event("ClientFailed", "multiplayer-reliable-dispatch");
        InvalidateRemotePlayerState(graph);
        (void)authority.ProcessControl();
        if (!localHero.IsWorldReady()) {
            if (!graph.ProcessPlayerActorState()) diagnostics.Event("ClientFailed", "multiplayer-player-actor-state-replication");
            InvalidateRemotePlayerState(graph);
            const std::uint64_t controlNow = GetTickCount64(); PlayerState controlState;
            while (transport.transport.TryConsume(controlState)) {
                transport.remotePlayerChannels.Apply(controlState, controlNow);
            }
            const auto controlSnapshots = transport.remotePlayerChannels.Snapshots();
            if (!authority.Reconcile(nullptr, controlSnapshots)) diagnostics.Event("ClientFailed", "multiplayer-pre-world-authority-replication");
            world.populationSimulation.SetHighDetailReady(localHero.MapName(), false); entitySimulation.Refresh(!departingEntityMap_.empty() ? departingEntityMap_ : localHero.MapName(), false);
            if (!departingEntityMap_.empty() && !graph.ReconcileEntityLifecycle(departingEntityMap_, departingEntityMapId_, false, ignoredDepartingEntityMapId_)) diagnostics.Event("ClientFailed", "multiplayer-source-map-teardown-drain");
            if (localHero.IsEntryPending()) localHero.TryBind();
            if (localHero.ConsumeCompletedWorldTransition()) { remotePlayers.CompleteWorldTransition(); sourceMapFinalDrainRequired_ = true; }
            return false;
        }
        if (localHero.ConsumeCompletedWorldTransition()) { remotePlayers.CompleteWorldTransition(); sourceMapFinalDrainRequired_ = true; }
        if (sourceMapFinalDrainRequired_) {
            if (!departingEntityMap_.empty() && !graph.ReconcileEntityLifecycle(departingEntityMap_, departingEntityMapId_, false, ignoredDepartingEntityMapId_)) { world.populationSimulation.SetHighDetailReady(localHero.MapName(), false); entitySimulation.Refresh(departingEntityMap_, false); diagnostics.Event("MultiplayerSourceMapHandoffDeferred", "destination authority waits for the final local source teardown drain"); return false; }
            ignoredDepartingEntityMapId_ = departingEntityMapId_; departingEntityMap_.clear(); departingEntityMapId_ = 0; sourceMapFinalDrainRequired_ = false;
        }
        if (!localHero.WorldIsCurrent()) {
            departingEntityMap_ = localHero.MapName(); departingEntityMapId_ = localHero.MapId(); sourceMapFinalDrainRequired_ = false;
            if (!graph.ReconcileEntityLifecycle(departingEntityMap_, departingEntityMapId_, false, ignoredDepartingEntityMapId_)) diagnostics.Event("ClientFailed", "multiplayer-source-map-teardown-drain");
            if (!players.actorState.RetireLocal()) diagnostics.Event("ClientFailed", "multiplayer-player-actor-retire");
            remotePlayers.BeginWorldTransition(); world.populationSimulation.SetHighDetailReady(departingEntityMap_, false); entitySimulation.Refresh(departingEntityMap_, false); localHero.BeginWorldTransition(); return true;
        }

        const std::uint64_t now = GetTickCount64(); localHero.CaptureMovement(now); localHero.CaptureAppearance(now); localHero.CaptureEquipment(now);
        if (!graph.ProcessPlayerActorState()) diagnostics.Event("ClientFailed", "multiplayer-player-actor-state-replication");
        InvalidateRemotePlayerState(graph);
        PlayerState inbound;
        while (transport.transport.TryConsume(inbound)) {
            if (!transport.remotePlayerChannels.Apply(inbound, now)) continue;
            if (transport.remotePlayerChannels.Size() > reportedRemotePlayerCount_) { reportedRemotePlayerCount_ = transport.remotePlayerChannels.Size(); char detail[384] = {}; std::snprintf(detail, sizeof(detail), "player=%s role=%s actor_id=%llu properties=0x%08X map=%s position=(%.3f,%.3f,%.3f) remote_count=%zu", inbound.playerId.c_str(), inbound.role == PeerRole::Host ? "host" : "guest", static_cast<unsigned long long>(inbound.actorId), inbound.changedProperties, inbound.mapName.c_str(), inbound.position.x, inbound.position.y, inbound.position.z, reportedRemotePlayerCount_); diagnostics.Event("MultiplayerRemoteStateApplied", detail); }
        }
        const auto remoteSnapshots = transport.remotePlayerChannels.Snapshots();
        remotePlayers.Reconcile(remoteSnapshots, localHero.MapName(), localHero.Hero());
        if (!authority.Reconcile(localHero.CurrentState(), remoteSnapshots)) diagnostics.Event("ClientFailed", "multiplayer-authority-replication");
        if (!world.populationSimulation.Process()) diagnostics.Event("ClientFailed", "multiplayer-population-state-replication");
        if (!entityMaterialization.Reconcile(entityLifecycle.Directory(), entityPresence.LiveEntities(), authority, localHero.MapName(), localHero.MapId())) diagnostics.Event("ClientFailed", "multiplayer-entity-materialization");
        const bool ownerRosterReady = graph.IsOwnerRosterReady(localHero.MapName());
        world.populationSimulation.SetHighDetailReady(localHero.MapName(), ownerRosterReady);
        if (!graph.ReconcileEntityLifecycle(localHero.MapName(), localHero.MapId(), true, ignoredDepartingEntityMapId_)) diagnostics.Event("ClientFailed", "multiplayer-entity-lifecycle-replication");
        world.hostWorldState.Refresh();
        if (!world.villageMembership.Process(entityPresence.LiveEntities())) diagnostics.Event("ClientFailed", "multiplayer-village-membership-replication");
        if (!entities.entityLowSimulation.Process(entityPresence.LiveEntities())) diagnostics.Event("ClientFailed", "multiplayer-entity-low-simulation-replication");
        if (!entities.entityMovement.Process(
                entityPresence.LiveEntities(),
                localHero.MapName(),
                ownerRosterReady,
                transport.remotePlayerChannels.ObserverReadinessRevision()))
        {
            diagnostics.Event(
                "ClientFailed", "multiplayer-entity-movement-replication");
        }
        if (!actions.entityActions.ProcessPending(localHero.MapName(), ownerRosterReady)) diagnostics.Event("ClientFailed", "multiplayer-entity-action-replication");
        if (!actions.playerActions.ProcessPending()) diagnostics.Event("ClientFailed", "multiplayer-player-action-replication");
        if (!actions.combatHits.Process()) diagnostics.Event("ClientFailed", "multiplayer-combat-hit-replication");
        if (!actions.playerDeath.Process(localHero)) diagnostics.Event("ClientFailed", "multiplayer-player-death");
        if (!actions.entityVitals.Process(localHero, entityPresence.LiveEntities(), remotePlayers)) diagnostics.Event("ClientFailed", "multiplayer-entity-vitals-replication");
        entitySimulation.Refresh(localHero.MapName(), ownerRosterReady);
        return false;
    }
}
