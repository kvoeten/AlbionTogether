#include "MultiplayerSession.h"

namespace fable::multiplayer
{
    MultiplayerSession::~MultiplayerSession() { Shutdown(); }

    bool MultiplayerSession::Initialize(
        const automation::runtime::RuntimeConfiguration& configuration,
        game::EntityService& entities, game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        game::npc::village::VillageMembershipService& villages,
        game::npc::simulation::DummyVillagerService& dummyVillagers,
        const core::Diagnostics& diagnostics)
    {
        lifecycle_.Reset();
        return graph_.Initialize(configuration, entities, npcs, locomotion, look, combat, abilities, villages, dummyVillagers, diagnostics);
    }
    bool MultiplayerSession::AttachThingPresenceObserver(game::entity::presence::ThingPresenceObserver& observer) { return graph_.AttachThingPresenceObserver(observer); }
    bool MultiplayerSession::AttachSavedEntityMapBlobObserver(game::entity::persistence::SavedEntityMapBlobObserver& observer) { return graph_.AttachSavedEntityMapBlobObserver(observer); }
    bool MultiplayerSession::AttachThingSaveProjectionHook(game::entity::persistence::ThingSaveProjectionHook& hook) { return graph_.AttachThingSaveProjectionHook(hook); }
    bool MultiplayerSession::AttachPopulationSimulationHook(game::npc::population::PopulationSimulationHook& hook) { return graph_.AttachPopulationSimulationHook(hook); }
    bool MultiplayerSession::AttachCreatureActionObserver(game::creature::actions::CreatureActionLifecycleObserver& observer) { return graph_.AttachCreatureActionObserver(observer); }
    bool MultiplayerSession::AttachAiBrainUpdateObserver(game::creature::ai::AiBrainUpdateObserver& observer) { return graph_.AttachAiBrainUpdateObserver(observer); }
    bool MultiplayerSession::AttachWorldTravelObserver(game::world::travel::WorldTravelObserver& observer) { return graph_.AttachWorldTravelObserver(observer); }
    bool MultiplayerSession::OnWorldReady() { return graph_.OnWorldReady(); }
    bool MultiplayerSession::ProcessPresentationLifecycle() { return lifecycle_.Process(graph_); }
    void MultiplayerSession::DriveReplicatedMovement() { graph_.DriveReplicatedMovement(); }
    void MultiplayerSession::Shutdown() noexcept { lifecycle_.Reset(); graph_.Shutdown(); }
    bool MultiplayerSession::IsEnabled() const noexcept { return graph_.IsEnabled(); }
    bool MultiplayerSession::IsWorldReady() const noexcept { return graph_.IsWorldReady(); }
    bool MultiplayerSession::HasActiveRemotePresentation() const { return graph_.HasActiveRemotePresentation(); }
    bool MultiplayerSession::TransferOwnedEntity(std::uint64_t entityUid, std::uint16_t destinationMapId, const game::Vector3& destinationPosition, float destinationFacing) { return graph_.TransferOwnedEntity(entityUid, destinationMapId, destinationPosition, destinationFacing); }
}
