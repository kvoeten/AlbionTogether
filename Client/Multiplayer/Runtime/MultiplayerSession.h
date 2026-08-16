#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Authority/EntitySimulationAuthority.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityMaterializationService.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Movement/EntityMovementReplication.h"
#include "Multiplayer/Persistence/HostWorldStateProjection.h"
#include "Multiplayer/Persistence/SavedEntityMapBaselineService.h"
#include "Multiplayer/Persistence/SavedEntityConstructionGate.h"
#include "Multiplayer/Population/PopulationSimulationAuthority.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/LocalPlayerChannel.h"
#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityVitalsReplication.h"
#include "Multiplayer/Replication/EntityLowSimulationReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Replication/VillageMembershipReplication.h"
#include "Multiplayer/Transport/UdpPeer.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Multiplayer/World/MapTransitionAuthorityService.h"

#include <cstdint>
#include <string>

namespace fable::automation::runtime
{
    class RuntimeConfiguration;
}

namespace fable::game
{
    class EntityService;
    class NpcService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::entity::presence
{
    class ThingPresenceObserver;
}

namespace fable::game::entity::persistence
{
    class SavedEntityMapBlobObserver;
    class ThingSaveProjectionHook;
}

namespace fable::game::world::travel
{
    class WorldTravelObserver;
}

namespace fable::game::npc::population
{
    class PopulationSimulationHook;
}

namespace fable::game::npc::village
{
    class VillageMembershipService;
}

namespace fable::game::npc::simulation
{
    class DummyVillagerService;
}

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver;
}

namespace fable::multiplayer
{
    // Coordinates transport and world lifecycle. Owner capture, remote native
    // presentation, and actor locomotion live in their dedicated subsystems.
    class MultiplayerSession final
    {
    public:
        MultiplayerSession() = default;
        ~MultiplayerSession();

        MultiplayerSession(const MultiplayerSession&) = delete;
        MultiplayerSession& operator=(const MultiplayerSession&) = delete;

        bool Initialize(
            const automation::runtime::RuntimeConfiguration& configuration,
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            game::creature::combat::CreatureCombatService& combat,
            game::creature::animation::CreatureAnimationService& animation,
            game::npc::village::VillageMembershipService& villages,
            game::npc::simulation::DummyVillagerService& dummyVillagers,
            const core::Diagnostics& diagnostics);
        bool AttachThingPresenceObserver(
            game::entity::presence::ThingPresenceObserver& observer);
        bool AttachSavedEntityMapBlobObserver(
            game::entity::persistence::SavedEntityMapBlobObserver& observer);
        bool AttachThingSaveProjectionHook(
            game::entity::persistence::ThingSaveProjectionHook& hook);
        bool AttachPopulationSimulationHook(
            game::npc::population::PopulationSimulationHook& hook);
        bool AttachCreatureActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver& observer);
        bool AttachAiBrainUpdateObserver(
            game::creature::ai::AiBrainUpdateObserver& observer);
        bool AttachWorldTravelObserver(
            game::world::travel::WorldTravelObserver& observer);
        bool OnWorldReady();
        // Returns true when the currently bound UE3 world started unloading.
        bool ProcessPresentationLifecycle();
        void DriveReplicatedMovement();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsWorldReady() const noexcept;
        [[nodiscard]] bool HasActiveRemotePresentation() const;
        bool TransferOwnedEntity(
            std::uint64_t entityUid,
            std::uint16_t destinationMapId,
            const game::Vector3& destinationPosition,
            float destinationFacing);

    private:
        bool ReconcileEntityLifecycle(
            const std::string& mapName,
            std::uint16_t mapId,
            bool publishLocalChanges = true);
        [[nodiscard]] bool IsOwnerRosterReady(
            const std::string& mapName) const noexcept;

        UdpPeer transport_;
        replication::LocalPlayerChannel localPlayerChannel_;
        replication::RemotePlayerChannels remotePlayerChannels_;
        replication::LocalHeroReplication localHero_;
        presentation::RemotePlayerRegistry remotePlayers_;
        entities::EntityPresenceReplication entityPresence_;
        entities::EntityLifecycleReplication entityLifecycle_;
        entities::EntityNetworkIdentityRegistry entityIdentities_;
        entities::EntityMaterializationService entityMaterialization_;
        persistence::HostWorldStateProjection hostWorldState_;
        persistence::SavedEntityMapBaselineService savedEntityMapBaseline_;
        persistence::SavedEntityConstructionGate savedEntityConstructionGate_;
        population::PopulationSimulationAuthority populationSimulation_;
        movement::EntityMovementReplication entityMovement_;
        replication::EntityActionReplication entityActions_;
        replication::EntityVitalsReplication entityVitals_;
        replication::EntityLowSimulationReplication entityLowSimulation_;
        replication::VillageMembershipReplication villageMembership_;
        authority::AuthorityReplication authority_;
        authority::EntitySimulationAuthority entitySimulation_;
        world::MapTransitionAuthorityService mapTransitionAuthority_;
        ReliableMessageDispatcher reliableMessages_;
        core::Diagnostics diagnostics_ = {};
        std::string departingEntityMap_;
        std::uint16_t departingEntityMapId_ = 0;
        std::uint16_t ignoredDepartingEntityMapId_ = 0;
        bool sourceMapFinalDrainRequired_ = false;
        bool enabled_ = false;
        std::size_t reportedRemotePlayerCount_ = 0;
    };
}
