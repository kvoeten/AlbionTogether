#pragma once

namespace fable::multiplayer::replication
{
    class EntityVitalsReplication;
    class PlayerActionReplication;
    class RemotePlayerChannels;
}

namespace fable::multiplayer
{
    // Couples reliable actor retirement/replacement to every bounded queue
    // that may still reference the previous native incarnation.
    class RemotePlayerLifecycleInvalidation final
    {
    public:
        static void Apply(
            replication::RemotePlayerChannels& channels,
            replication::PlayerActionReplication& actions,
            replication::EntityVitalsReplication& vitals) noexcept;
    };
}
