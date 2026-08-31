#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace fable::game::entity::persistence
{
    class SavedEntityMapBlobObserver;
    struct SavedEntityMapCollectionEvent;
}

namespace fable::multiplayer
{
    class ReliableMessageDispatcher;
    class UdpPeer;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::replication
{
    class RemotePlayerChannels;
    class PlayerActionReplication;
}

namespace fable::multiplayer::persistence
{
    // Holds the native CSavedEntities post-load boundary on a guest. Only the
    // transport/control lane is pumped while held; world simulation and actor
    // construction do not advance until the host's exact map baseline has
    // been installed into the newly loaded native collection.
    class SavedEntityConstructionGate final
    {
    public:
        void Initialize(
            PeerRole role,
            UdpPeer& transport,
            ReliableMessageDispatcher& reliableMessages,
            replication::RemotePlayerChannels& remotePlayers,
            replication::PlayerActionReplication& playerActions,
            authority::AuthorityReplication& authority,
            const core::Diagnostics& diagnostics) noexcept;
        bool Attach(
            game::entity::persistence::SavedEntityMapBlobObserver& observer)
            noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::uint64_t MaximumHoldMilliseconds = 30'000;

        static void AwaitPostLoad(
            void* context,
            const game::entity::persistence::SavedEntityMapCollectionEvent&
                event) noexcept;
        void AwaitAuthoritativeMap();
        bool PumpControlLane();
        bool ResolveHostMap(
            std::string& mapName,
            std::uint16_t& mapId) const;
        void Report(
            const char* event,
            const std::string& mapName,
            std::uint16_t mapId,
            const char* reason) const noexcept;

        UdpPeer* transport_ = nullptr;
        ReliableMessageDispatcher* reliableMessages_ = nullptr;
        replication::RemotePlayerChannels* remotePlayers_ = nullptr;
        replication::PlayerActionReplication* playerActions_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        game::entity::persistence::SavedEntityMapBlobObserver* observer_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::atomic_bool waiting_{false};
        std::atomic_bool stopping_{false};
    };
}
