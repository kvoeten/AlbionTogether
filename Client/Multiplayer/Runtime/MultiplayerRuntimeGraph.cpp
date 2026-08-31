#include "MultiplayerRuntimeGraph.h"

#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/Entity/EntityService.h"
#include "Game/Quest/QuestService.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Game/NPC/Village/VillageMembershipService.h"
#include "Game/World/Travel/Hooks/WorldTravelObserver.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>

namespace
{
    std::string Utf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1) return {};
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::uint64_t StablePlayerActorId(fable::multiplayer::PeerRole role, const std::string& playerId) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : playerId) { hash ^= character; hash *= 1099511628211ull; }
        hash ^= static_cast<std::uint8_t>(role); hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }

    std::uint32_t NewAuthorityEpoch(std::uint64_t actorId) noexcept
    {
        LARGE_INTEGER counter = {};
        QueryPerformanceCounter(&counter);
        std::uint64_t value = static_cast<std::uint64_t>(counter.QuadPart) ^
            GetTickCount64() ^ actorId ^
            (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32);
        value ^= value >> 33;
        value *= 0xFF51AFD7ED558CCDull;
        value ^= value >> 33;
        const std::uint32_t epoch = static_cast<std::uint32_t>(
            value ^ (value >> 32));
        return epoch == 0 ? 1 : epoch;
    }
}

namespace fable::multiplayer
{
    MultiplayerRuntimeGraph::~MultiplayerRuntimeGraph()
    {
        Shutdown();
    }

    void MultiplayerRuntimeGraph::MarkStage(
        const InitializationStage stage) noexcept
    {
        initializedStages_ |= static_cast<std::uint32_t>(stage);
    }

    bool MultiplayerRuntimeGraph::HasStage(
        const InitializationStage stage) const noexcept
    {
        return (initializedStages_ & static_cast<std::uint32_t>(stage)) != 0;
    }

    bool MultiplayerRuntimeGraph::Initialize(
        const automation::runtime::RuntimeConfiguration& configuration,
        game::EntityService& entities, game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::animation::CreatureAnimationService& animation,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        ui::HudService& hud,
        game::QuestService& quests,
        game::npc::village::VillageMembershipService& villages,
        game::npc::simulation::DummyVillagerService& dummyVillagers,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!configuration.MultiplayerEnabled()) return true;
        diagnostics_ = diagnostics;
        const PeerRole role = configuration.MultiplayerRole() == L"host" ? PeerRole::Host : PeerRole::Guest;
        const std::string playerId = Utf8(configuration.MultiplayerPlayerId());
        const std::string appearance = Utf8(configuration.MultiplayerAppearance());
        const std::uint64_t actorId = StablePlayerActorId(role, playerId);
        const std::uint32_t sessionAuthorityEpoch =
            NewAuthorityEpoch(actorId);

        auto& t = contexts_.transport;
        auto& p = contexts_.players;
        auto& w = contexts_.world;
        auto& e = contexts_.entities;
        auto& a = contexts_.actions;
        if (!p.remotePlayers.Initialize(entities, npcs, locomotion, look, animation, combat, abilities, hud, p.playerCombatants, diagnostics_, actorId)) return false;
        MarkStage(InitializationStage::Players);
        const bool started = role == PeerRole::Host
            ? t.transport.StartHost(configuration.MultiplayerPort(), actorId, diagnostics_)
            : t.transport.StartGuest(Utf8(configuration.MultiplayerAddress()), configuration.MultiplayerPort(), actorId, diagnostics_);
        if (!started) { diagnostics_.Event("ClientFailed", "multiplayer-transport-start"); Shutdown(); return false; }
        MarkStage(InitializationStage::Transport);
        p.localHero.Initialize(entities, locomotion, t.localPlayerChannel, t.transport, p.playerCombatants, diagnostics_, role, actorId, sessionAuthorityEpoch, playerId, appearance, configuration.MorphSelfTest());
        p.actorState.Initialize(role, actorId, t.transport, sessionAuthorityEpoch, p.localHero, t.remotePlayerChannels, diagnostics_);
        w.authority.Initialize(role, actorId, t.transport, diagnostics_);
        w.mapTransitionAuthority.Initialize(w.authority, diagnostics_);
        e.entityIdentities.Initialize(diagnostics_);
        e.entityPresence.Initialize(e.entityIdentities, diagnostics_);
        e.entityLifecycle.Initialize(role, actorId, t.transport, w.authority, diagnostics_);
        e.entityMaterialization.Initialize(
            role,
            actorId,
            entities,
            e.entityPresence,
            e.entityIdentities,
            dummyVillagers,
            villages,
            diagnostics_);
        w.hostWorldState.Initialize(role, e.entityLifecycle, e.entityIdentities, diagnostics_);
        w.savedEntityMapBaseline.Initialize(role, actorId, t.transport, diagnostics_);
        w.authority.SetMapBaselineGate(&w.savedEntityMapBaseline);
        w.populationSimulation.Initialize(role, actorId, t.transport, w.authority, p.localHero, diagnostics_);
        e.entityMovement.Initialize(role, actorId, t.transport, w.authority, e.entityLifecycle, e.entityIdentities, locomotion, look, diagnostics_);
        a.entityActions.Initialize(role, actorId, t.transport, w.authority, e.entityLifecycle, e.entityIdentities, e.entityPresence, p.playerCombatants, a.combatLedger, combat, diagnostics_);
        a.playerActions.Initialize(role, actorId, t.transport, p.localHero, t.remotePlayerChannels, p.remotePlayers, e.entityIdentities, e.entityPresence, p.playerCombatants, a.combatLedger, combat, abilities, diagnostics_);
        a.combatHits.Initialize(role, actorId, t.transport, w.authority,
            e.entityLifecycle, e.entityIdentities, e.entityPresence,
            p.localHero, t.remotePlayerChannels, p.playerCombatants,
            a.combatLedger, combat, diagnostics_);
        a.entityVitals.Initialize(role, actorId, t.transport, w.authority,
            e.entityLifecycle, e.entityIdentities, t.remotePlayerChannels,
            combat, diagnostics_);
        e.entityLowSimulation.Initialize(role, actorId, t.transport, w.authority, e.entityLifecycle, e.entityIdentities, dummyVillagers, diagnostics_);
        w.villageMembership.Initialize(role, actorId, w.authority, e.entityLifecycle, e.entityIdentities, villages, diagnostics_);
        w.entitySimulation.Initialize(actorId, w.authority, e.entityLifecycle, e.entityIdentities, e.entityPresence, p.playerCombatants, diagnostics_);
        MarkStage(InitializationStage::Components);
        if (!a.playerDeath.Initialize(combat, quests, diagnostics_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-death-initialization");
            Shutdown();
            return false;
        }
        t.reliableMessages.Initialize(t.transport, diagnostics_);
        MarkStage(InitializationStage::ReliableDispatcher);
        w.savedEntityConstructionGate.Initialize(role, t.transport, t.reliableMessages, t.remotePlayerChannels, w.authority, diagnostics_);
        MarkStage(InitializationStage::ConstructionGate);
        if (!ReliableSinkDescriptorRegistry::RegisterDiscovered(
                contexts_, t.reliableMessages, diagnostics_))
        {
            Shutdown();
            return false;
        }
        enabled_ = true;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s player=%s actor_id=%llu authority_epoch=%u appearance=%s",
            role == PeerRole::Host ? "host" : "guest",
            playerId.c_str(),
            static_cast<unsigned long long>(actorId),
            sessionAuthorityEpoch,
            appearance.c_str());
        diagnostics_.Event("MultiplayerSessionReady", detail);
        return true;
    }

    bool MultiplayerRuntimeGraph::AttachThingPresenceObserver(game::entity::presence::ThingPresenceObserver& observer) { return !enabled_ || contexts_.entities.entityPresence.Attach(observer); }
    bool MultiplayerRuntimeGraph::AttachSavedEntityMapBlobObserver(game::entity::persistence::SavedEntityMapBlobObserver& observer) { return !enabled_ || (contexts_.world.savedEntityMapBaseline.Attach(observer) && contexts_.world.savedEntityConstructionGate.Attach(observer)); }
    bool MultiplayerRuntimeGraph::AttachThingSaveProjectionHook(game::entity::persistence::ThingSaveProjectionHook& hook) { return !enabled_ || contexts_.world.hostWorldState.Attach(hook); }
    bool MultiplayerRuntimeGraph::AttachPopulationSimulationHook(game::npc::population::PopulationSimulationHook& hook) { return !enabled_ || contexts_.world.populationSimulation.Attach(hook); }
    bool MultiplayerRuntimeGraph::AttachCreatureActionObserver(game::creature::actions::CreatureActionLifecycleObserver& observer) { return !enabled_ || (contexts_.actions.entityActions.Attach(observer) && contexts_.actions.playerActions.AttachActionObserver(observer) && contexts_.world.entitySimulation.AttachActionObserver(observer)); }
    bool MultiplayerRuntimeGraph::AttachCreatureModeObserver(game::creature::locomotion::CreatureModeManagerObserver& observer) { return !enabled_ || contexts_.actions.playerActions.AttachModeObserver(observer); }
    bool MultiplayerRuntimeGraph::AttachAiBrainUpdateObserver(game::creature::ai::AiBrainUpdateObserver& observer) { return !enabled_ || contexts_.world.entitySimulation.AttachBrainObserver(observer); }
    bool MultiplayerRuntimeGraph::AttachWorldTravelObserver(game::world::travel::WorldTravelObserver& observer) { return !enabled_ || contexts_.world.mapTransitionAuthority.Attach(observer); }
    bool MultiplayerRuntimeGraph::OnWorldReady() { return !enabled_ || contexts_.players.localHero.OnWorldReady(); }
    bool MultiplayerRuntimeGraph::ProcessPresentationLifecycle() { return lifecycle_.Process(*this); }
    bool MultiplayerRuntimeGraph::ProcessPlayerActorState() { return !enabled_ || contexts_.players.actorState.Process(); }

    bool MultiplayerRuntimeGraph::ReconcileEntityLifecycle(const std::string& mapName, std::uint16_t mapId, bool publishLocalChanges, std::uint16_t ignoredDepartingMapId)
    {
        if (mapName.empty() || mapId == 0) return false;
        auto& e = contexts_.entities;
        std::vector<entities::LiveEntityChange> changes;
        bool baseline = false;
        e.entityPresence.TakeChanges(changes, baseline);
        if (!publishLocalChanges) {
            if (!changes.empty() || baseline) { char detail[256] = {}; std::snprintf(detail, sizeof(detail), "map=%s map_id=%u discarded_changes=%zu baseline_discarded=%s; level teardown does not mutate canonical world state", mapName.c_str(), static_cast<unsigned int>(mapId), changes.size(), baseline ? "true" : "false"); diagnostics_.Event("MultiplayerSourceMapTeardownDrained", detail); }
            changes.clear(); baseline = false;
        } else if (ignoredDepartingMapId != 0 && ignoredDepartingMapId != mapId) {
            changes.erase(std::remove_if(changes.begin(), changes.end(), [ignoredDepartingMapId](const entities::LiveEntityChange& change) { return change.kind == entities::LiveEntityChangeKind::Unregistered && change.record.mapId == ignoredDepartingMapId; }), changes.end());
        }
        const authority::MapAuthorityLease* lease = contexts_.world.authority.FindMapLease(mapName);
        const bool rosterReady = lease != nullptr && lease->epoch != 0 && (!lease->localAuthority || e.entityMaterialization.IsLocalRosterReady(mapName, lease->epoch));
        return e.entityLifecycle.Reconcile(e.entityPresence.LiveEntities(), changes, baseline, mapName, mapId, publishLocalChanges && rosterReady);
    }

    bool MultiplayerRuntimeGraph::IsOwnerRosterReady(const std::string& mapName) const noexcept
    {
        const authority::MapAuthorityLease* lease = contexts_.world.authority.FindMapLease(mapName);
        return lease != nullptr && lease->epoch != 0 && (!lease->localAuthority || contexts_.entities.entityMaterialization.IsLocalRosterReady(mapName, lease->epoch));
    }
    void MultiplayerRuntimeGraph::DriveReplicatedMovement() { if (enabled_ && contexts_.players.localHero.IsWorldReady()) { contexts_.players.remotePlayers.DriveMovement(); contexts_.entities.entityMovement.Drive(); } }
    bool MultiplayerRuntimeGraph::HasActiveRemotePresentation() const { return enabled_ && contexts_.players.remotePlayers.ActiveCount() != 0; }
    bool MultiplayerRuntimeGraph::TransferOwnedEntity(std::uint64_t uid, std::uint16_t mapId, const game::Vector3& position, float facing) { return enabled_ && contexts_.entities.entityPresence.UnregisterLocalPresence(uid) && contexts_.entities.entityLifecycle.SubmitOwnedTransfer(uid, mapId, position, facing); }

    void MultiplayerRuntimeGraph::Shutdown() noexcept
    {
        lifecycle_.Reset();
        auto& t = contexts_.transport;
        auto& p = contexts_.players;
        auto& w = contexts_.world;
        auto& e = contexts_.entities;
        auto& a = contexts_.actions;

        if (HasStage(InitializationStage::ConstructionGate))
        {
            w.savedEntityConstructionGate.Shutdown();
        }
        if (HasStage(InitializationStage::ReliableDispatcher))
        {
            t.reliableMessages.Shutdown();
        }
        if (HasStage(InitializationStage::Components))
        {
            a.playerDeath.Shutdown();
            w.villageMembership.Shutdown();
            e.entityLowSimulation.Shutdown();
            a.entityVitals.Shutdown();
            a.combatHits.Shutdown();
            a.playerActions.Shutdown();
            p.actorState.Shutdown();
            a.entityActions.Shutdown();
            p.localHero.Shutdown();
            w.entitySimulation.Shutdown();
            e.entityMovement.Shutdown();
            e.entityMaterialization.Shutdown();
            e.entityPresence.Shutdown();
            w.populationSimulation.Shutdown();
            w.mapTransitionAuthority.Shutdown();
            w.authority.SetMapBaselineGate(nullptr);
            w.savedEntityMapBaseline.Shutdown();
            w.hostWorldState.Shutdown();
            e.entityLifecycle.Shutdown();
            e.entityIdentities.Clear();
            w.authority.Shutdown();
            a.combatLedger.Clear();
        }
        if (HasStage(InitializationStage::Players))
        {
            p.remotePlayers.Shutdown();
            p.playerCombatants.Clear();
        }
        if (HasStage(InitializationStage::Transport))
        {
            t.localPlayerChannel.Close();
            t.remotePlayerChannels.Clear();
            t.transport.Shutdown();
        }
        initializedStages_ = 0;
        diagnostics_ = {};
        enabled_ = false;
    }
}
