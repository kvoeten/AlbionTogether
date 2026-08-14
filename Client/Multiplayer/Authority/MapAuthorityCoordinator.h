#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace fable::multiplayer::authority
{
    struct MapAuthorityLease final
    {
        std::string mapName;
        std::uint64_t actorId = 0;
        std::uint32_t epoch = 0;
        bool localAuthority = false;
    };

    // Host-resolved map simulation ownership. Player actor ownership remains
    // with each client; this coordinator independently decides who simulates
    // NPCs and map-local gameplay when peers occupy different maps.
    class MapAuthorityCoordinator final
    {
    public:
        void Initialize(PeerRole localRole, const core::Diagnostics& diagnostics);
        void Reconcile(
            const PlayerState* localPlayer,
            const std::vector<replication::RemotePlayerSnapshot>& remotePlayers);
        [[nodiscard]] const MapAuthorityLease* Find(
            const std::string& mapName) const noexcept;
        void Clear() noexcept;

    private:
        PeerRole localRole_ = PeerRole::Guest;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::string, MapAuthorityLease> leases_;
    };
}
