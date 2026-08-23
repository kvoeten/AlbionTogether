#pragma once

// The session is an adapter around these bounded runtime contexts.  Keeping
// the ownership groups here makes lifecycle dependencies visible without
// forcing every new replicated feature into MultiplayerSession itself.

#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Authority/EntitySimulationAuthority.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityMaterializationService.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Movement/EntityMovementReplication.h"
#include "Multiplayer/Population/PopulationSimulationAuthority.h"
#include "Multiplayer/Persistence/HostWorldStateProjection.h"
#include "Multiplayer/Persistence/SavedEntityConstructionGate.h"
#include "Multiplayer/Persistence/SavedEntityMapBaselineService.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/EntityActionReplication.h"
#include "Multiplayer/Replication/EntityLowSimulationReplication.h"
#include "Multiplayer/Replication/EntityVitalsReplication.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/LocalPlayerChannel.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"
#include "Multiplayer/Replication/PlayerActorStateReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Replication/VillageMembershipReplication.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Multiplayer/Transport/UdpPeer.h"
#include "Multiplayer/World/MapTransitionAuthorityService.h"

namespace fable::multiplayer
{
    struct MultiplayerTransportContext final
    {
        UdpPeer transport;
        replication::LocalPlayerChannel localPlayerChannel;
        replication::RemotePlayerChannels remotePlayerChannels;
        ReliableMessageDispatcher reliableMessages;
    };

    struct MultiplayerPlayerContext final
    {
        combat::PlayerCombatantDirectory playerCombatants;
        replication::LocalHeroReplication localHero;
        replication::PlayerActorStateReplication actorState;
        presentation::RemotePlayerRegistry remotePlayers;
    };

    struct MultiplayerWorldContext final
    {
        authority::AuthorityReplication authority;
        authority::EntitySimulationAuthority entitySimulation;
        world::MapTransitionAuthorityService mapTransitionAuthority;
        persistence::HostWorldStateProjection hostWorldState;
        persistence::SavedEntityMapBaselineService savedEntityMapBaseline;
        persistence::SavedEntityConstructionGate savedEntityConstructionGate;
        population::PopulationSimulationAuthority populationSimulation;
        replication::VillageMembershipReplication villageMembership;
    };

    struct MultiplayerEntityContext final
    {
        entities::EntityPresenceReplication entityPresence;
        entities::EntityLifecycleReplication entityLifecycle;
        entities::EntityNetworkIdentityRegistry entityIdentities;
        entities::EntityMaterializationService entityMaterialization;
        movement::EntityMovementReplication entityMovement;
        replication::EntityLowSimulationReplication entityLowSimulation;
    };

    struct MultiplayerActionContext final
    {
        replication::EntityActionReplication entityActions;
        replication::PlayerActionReplication playerActions;
        replication::EntityVitalsReplication entityVitals;
    };

    struct MultiplayerSessionContexts final
    {
        MultiplayerTransportContext transport;
        MultiplayerPlayerContext players;
        MultiplayerWorldContext world;
        MultiplayerEntityContext entities;
        MultiplayerActionContext actions;
    };
}
