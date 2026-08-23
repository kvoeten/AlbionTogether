#include "RemotePlayerLifecycleInvalidation.h"

#include "Multiplayer/Replication/EntityVitalsReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <vector>

namespace fable::multiplayer
{
    void RemotePlayerLifecycleInvalidation::Apply(
        replication::RemotePlayerChannels& channels,
        replication::PlayerActionReplication& actions,
        replication::EntityVitalsReplication& vitals) noexcept
    {
        std::vector<std::uint64_t> invalidatedActors;
        bool allActors = false;
        channels.ConsumeInvalidations(invalidatedActors, allActors);
        if (allActors)
        {
            actions.InvalidateAllRemote();
            vitals.ClearRemotePlayers();
            return;
        }
        for (const std::uint64_t actorId : invalidatedActors)
        {
            actions.InvalidateActor(actorId);
            vitals.RetirePlayer(actorId);
        }
    }
}
