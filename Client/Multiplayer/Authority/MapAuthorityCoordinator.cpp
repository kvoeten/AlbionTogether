#include "MapAuthorityCoordinator.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace fable::multiplayer::authority
{
    void MapAuthorityCoordinator::Initialize(
        PeerRole localRole,
        const core::Diagnostics& diagnostics)
    {
        Clear();
        localRole_ = localRole;
        diagnostics_ = diagnostics;
    }

    void MapAuthorityCoordinator::Reconcile(
        const PlayerState* localPlayer,
        const std::vector<replication::RemotePlayerSnapshot>& remotePlayers)
    {
        std::unordered_map<std::string, std::vector<const PlayerState*>> players;
        if (localPlayer != nullptr && !localPlayer->mapName.empty())
        {
            players[localPlayer->mapName].push_back(localPlayer);
        }
        for (const auto& remote : remotePlayers)
        {
            if (!remote.state.mapName.empty())
            {
                players[remote.state.mapName].push_back(&remote.state);
            }
        }

        std::unordered_map<std::string, MapAuthorityLease> next;
        for (auto& [mapName, occupants] : players)
        {
            // A present host owns its map. Otherwise the lowest stable actor
            // ID supplies deterministic delegated authority for that map.
            const PlayerState* selected = nullptr;
            for (const PlayerState* occupant : occupants)
            {
                if (occupant->role == PeerRole::Host)
                {
                    selected = occupant;
                    break;
                }
                if (selected == nullptr || occupant->actorId < selected->actorId)
                {
                    selected = occupant;
                }
            }
            if (selected == nullptr)
            {
                continue;
            }
            MapAuthorityLease lease;
            lease.mapName = mapName;
            lease.actorId = selected->actorId;
            lease.localAuthority = localPlayer != nullptr &&
                selected->actorId == localPlayer->actorId;
            const auto previous = leases_.find(mapName);
            lease.epoch = previous == leases_.end()
                ? 1
                : previous->second.epoch +
                    (previous->second.actorId == lease.actorId ? 0 : 1);
            next.emplace(mapName, lease);
            if (previous == leases_.end() ||
                previous->second.actorId != lease.actorId)
            {
                char detail[224] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "map=%s authority_actor_id=%llu epoch=%u local=%s resolver=%s",
                    mapName.c_str(),
                    static_cast<unsigned long long>(lease.actorId),
                    lease.epoch, lease.localAuthority ? "true" : "false",
                    localRole_ == PeerRole::Host ? "host" : "replicated-host");
                diagnostics_.Event("MultiplayerMapAuthorityChanged", detail);
            }
        }
        leases_ = std::move(next);
    }

    const MapAuthorityLease* MapAuthorityCoordinator::Find(
        const std::string& mapName) const noexcept
    {
        const auto iterator = leases_.find(mapName);
        return iterator == leases_.end() ? nullptr : &iterator->second;
    }

    void MapAuthorityCoordinator::Clear() noexcept
    {
        leases_.clear();
    }
}
