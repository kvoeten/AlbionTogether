#include "MapAuthorityCoordinator.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fable::multiplayer::authority
{
    void MapAuthorityCoordinator::Initialize(
        PeerRole localRole,
        std::uint64_t localActorId,
        const core::Diagnostics& diagnostics)
    {
        Clear();
        localRole_ = localRole;
        localActorId_ = localActorId;
        diagnostics_ = diagnostics;
    }

    void MapAuthorityCoordinator::HostReconcile(
        const PlayerState* localPlayer,
        const std::vector<replication::RemotePlayerSnapshot>& remotePlayers)
    {
        if (localRole_ != PeerRole::Host || localPlayer == nullptr)
        {
            return;
        }

        std::unordered_map<std::string, std::vector<const PlayerState*>> players;
        std::unordered_set<std::uint64_t> presentActors;
        if (!localPlayer->mapName.empty())
        {
            ObserveOccupancy(*localPlayer, presentActors);
            players[localPlayer->mapName].push_back(localPlayer);
        }
        std::vector<const replication::RemotePlayerSnapshot*> orderedRemotes;
        orderedRemotes.reserve(remotePlayers.size());
        for (const auto& remote : remotePlayers)
        {
            orderedRemotes.push_back(&remote);
        }
        std::sort(
            orderedRemotes.begin(),
            orderedRemotes.end(),
            [](const replication::RemotePlayerSnapshot* left,
                const replication::RemotePlayerSnapshot* right)
            {
                return left->state.actorId < right->state.actorId;
            });
        for (const auto* remote : orderedRemotes)
        {
            if (remote != nullptr && !remote->state.mapName.empty())
            {
                ObserveOccupancy(
                    remote->state,
                    presentActors);
                players[remote->state.mapName].push_back(&remote->state);
            }
        }
        for (auto iterator = actorOccupancy_.begin();
             iterator != actorOccupancy_.end();)
        {
            if (presentActors.find(iterator->first) == presentActors.end())
            {
                iterator = actorOccupancy_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        for (auto iterator = actorRequests_.begin();
             iterator != actorRequests_.end();)
        {
            if (presentActors.find(iterator->first) == presentActors.end())
            {
                iterator = actorRequests_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        std::unordered_map<std::string, MapAuthorityLease> next;
        // Player-state map updates and pre-load Prepare reservations are not
        // sufficient to revoke an old lease. Retain the source until the actor
        // both occupies a different native map ID and activates that exact
        // reservation with Request. A disconnected actor is absent and is
        // never retained.
        for (const auto& [mapName, previous] : leases_)
        {
            const auto occupancy = actorOccupancy_.find(previous.actorId);
            const auto request = actorRequests_.find(previous.actorId);
            if (occupancy == actorOccupancy_.end())
            {
                continue;
            }
            const bool stillOccupiesSource =
                occupancy->second.mapName == mapName &&
                occupancy->second.mapId == previous.mapId;
            const bool destinationActivated =
                request != actorRequests_.end() &&
                request->second.activated &&
                request->second.mapId == occupancy->second.mapId &&
                occupancy->second.mapId != previous.mapId;
            if (stillOccupiesSource || !destinationActivated)
            {
                next.emplace(mapName, previous);
            }
        }
        for (auto& [mapName, occupants] : players)
        {
            if (next.find(mapName) != next.end())
            {
                continue;
            }
            const PlayerState* selected = nullptr;
            const auto previous = leases_.find(mapName);
            if (previous != leases_.end())
            {
                const auto retained = std::find_if(
                    occupants.begin(),
                    occupants.end(),
                    [&previous](const PlayerState* occupant)
                    {
                        return occupant != nullptr &&
                            occupant->actorId == previous->second.actorId;
                    });
                if (retained != occupants.end())
                {
                    selected = *retained;
                }
            }
            if (selected == nullptr)
            {
                std::uint64_t selectedOrder = 0;
                for (const PlayerState* occupant : occupants)
                {
                    if (occupant == nullptr)
                    {
                        continue;
                    }
                    const auto candidate = actorRequests_.find(
                        occupant->actorId);
                    if (candidate == actorRequests_.end() ||
                        !candidate->second.activated ||
                        candidate->second.mapId != occupant->mapId)
                    {
                        continue;
                    }
                    if (selected == nullptr ||
                        candidate->second.order < selectedOrder ||
                        (candidate->second.order == selectedOrder &&
                            occupant->actorId < selected->actorId))
                    {
                        selected = occupant;
                        selectedOrder = candidate->second.order;
                    }
                }
            }
            if (selected == nullptr)
            {
                continue;
            }

            const bool changed = previous == leases_.end() ||
                previous->second.actorId != selected->actorId;
            const auto selectedRequest = actorRequests_.find(
                selected->actorId);
            if (changed && (selectedRequest == actorRequests_.end() ||
                    !selectedRequest->second.activated ||
                    selectedRequest->second.mapId != selected->mapId))
            {
                continue;
            }
            MapAuthorityLease lease;
            lease.mapName = mapName;
            lease.actorId = selected->actorId;
            lease.mapId = changed
                ? selectedRequest->second.mapId
                : previous->second.mapId;
            lease.epoch = changed
                ? ++epochCounters_[mapName]
                : previous->second.epoch;
            lease.localAuthority = lease.actorId == localActorId_;
            next.emplace(mapName, lease);
            if (changed)
            {
                QueueGrant(lease);
                ReportChange(
                    mapName,
                    lease.actorId,
                    lease.epoch,
                    "grant");
            }
        }

        for (const auto& [mapName, previous] : leases_)
        {
            if (next.find(mapName) != next.end())
            {
                continue;
            }
            const std::uint32_t epoch = ++epochCounters_[mapName];
            QueueRelease(mapName, previous.mapId, epoch);
            ReportChange(mapName, 0, epoch, "release");
        }
        leases_ = std::move(next);
    }

    bool MapAuthorityCoordinator::HostRequest(
        const std::string& mapName,
        std::uint16_t mapId,
        std::uint64_t actorId,
        std::uint32_t observedEpoch)
    {
        return HostRecordRequest(
            mapName,
            mapId,
            actorId,
            observedEpoch,
            true);
    }

    bool MapAuthorityCoordinator::HostPrepare(
        const std::string& mapName,
        std::uint16_t mapId,
        std::uint64_t actorId,
        std::uint32_t observedEpoch)
    {
        return HostRecordRequest(
            mapName,
            mapId,
            actorId,
            observedEpoch,
            false);
    }

    bool MapAuthorityCoordinator::HostRecordRequest(
        const std::string& mapName,
        std::uint16_t mapId,
        std::uint64_t actorId,
        std::uint32_t observedEpoch,
        bool activated)
    {
        if (localRole_ != PeerRole::Host || mapName.empty() || mapId == 0 ||
            actorId == 0)
        {
            return false;
        }
        const auto epoch = epochCounters_.find(mapName);
        if (epoch != epochCounters_.end() && observedEpoch > epoch->second)
        {
            return false;
        }
        const auto existing = actorRequests_.find(actorId);
        if (existing != actorRequests_.end() &&
            existing->second.mapId == mapId)
        {
            existing->second.mapName = mapName;
            existing->second.observedEpoch = (std::max)(
                existing->second.observedEpoch,
                observedEpoch);
            existing->second.activated =
                existing->second.activated || activated;
            return true;
        }

        ++nextRequestOrder_;
        if (nextRequestOrder_ == 0)
        {
            ++nextRequestOrder_;
        }
        MapRequest request;
        request.mapName = mapName;
        request.mapId = mapId;
        request.order = nextRequestOrder_;
        request.observedEpoch = observedEpoch;
        request.activated = activated;
        actorRequests_[actorId] = std::move(request);

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "phase=%s map=%s map_id=%u requester_actor_id=%llu observed_epoch=%u order=%llu",
            activated ? "activate" : "prepare",
            mapName.c_str(),
            static_cast<unsigned int>(mapId),
            static_cast<unsigned long long>(actorId),
            observedEpoch,
            static_cast<unsigned long long>(nextRequestOrder_));
        diagnostics_.Event("MultiplayerMapAuthorityRequested", detail);
        return true;
    }

    void MapAuthorityCoordinator::ObserveOccupancy(
        const PlayerState& player,
        std::unordered_set<std::uint64_t>& presentActors)
    {
        if (player.actorId == 0 || player.mapName.empty())
        {
            return;
        }
        presentActors.insert(player.actorId);
        ActorOccupancy& occupancy = actorOccupancy_[player.actorId];
        occupancy.mapName = player.mapName;
        occupancy.mapId = player.mapId;
    }

    bool MapAuthorityCoordinator::Apply(
        const protocol::AuthorityMessage& message)
    {
        if (message.scope != protocol::AuthorityScope::MapSimulation ||
            message.mapName.empty() || message.mapEpoch == 0)
        {
            return false;
        }
        std::uint32_t& lastEpoch = epochCounters_[message.mapName];
        if (message.mapEpoch <= lastEpoch)
        {
            return false;
        }
        lastEpoch = message.mapEpoch;
        if (message.operation == protocol::AuthorityOperation::Release)
        {
            leases_.erase(message.mapName);
            ReportChange(message.mapName, 0, message.mapEpoch, "release");
            return true;
        }
        if (message.operation != protocol::AuthorityOperation::Grant ||
            message.ownerActorId == 0)
        {
            return false;
        }
        MapAuthorityLease lease;
        lease.mapName = message.mapName;
        lease.actorId = message.ownerActorId;
        lease.mapId = message.mapId;
        lease.epoch = message.mapEpoch;
        lease.localAuthority = lease.actorId == localActorId_;
        leases_[lease.mapName] = lease;
        ReportChange(
            lease.mapName,
            lease.actorId,
            lease.epoch,
            "grant");
        return true;
    }

    void MapAuthorityCoordinator::QueueBaseline()
    {
        if (localRole_ != PeerRole::Host)
        {
            return;
        }
        for (const auto& [mapName, lease] : leases_)
        {
            (void)mapName;
            QueueGrant(lease);
        }
    }

    bool MapAuthorityCoordinator::TakePending(
        protocol::AuthorityMessage& message)
    {
        if (pending_.empty())
        {
            return false;
        }
        message = std::move(pending_.front());
        pending_.pop_front();
        return true;
    }

    void MapAuthorityCoordinator::RestorePending(
        protocol::AuthorityMessage message)
    {
        pending_.push_front(std::move(message));
    }

    const MapAuthorityLease* MapAuthorityCoordinator::Find(
        const std::string& mapName) const noexcept
    {
        const auto iterator = leases_.find(mapName);
        return iterator == leases_.end() ? nullptr : &iterator->second;
    }

    std::vector<MapAuthorityLease> MapAuthorityCoordinator::Snapshot() const
    {
        std::vector<MapAuthorityLease> result;
        result.reserve(leases_.size());
        for (const auto& entry : leases_)
        {
            result.push_back(entry.second);
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const MapAuthorityLease& left,
                const MapAuthorityLease& right)
            {
                return left.mapName < right.mapName;
            });
        return result;
    }

    void MapAuthorityCoordinator::QueueGrant(
        const MapAuthorityLease& lease)
    {
        protocol::AuthorityMessage message;
        message.operation = protocol::AuthorityOperation::Grant;
        message.scope = protocol::AuthorityScope::MapSimulation;
        message.ownerActorId = lease.actorId;
        message.mapId = lease.mapId;
        message.mapEpoch = lease.epoch;
        message.mapName = lease.mapName;
        pending_.push_back(std::move(message));
    }

    void MapAuthorityCoordinator::QueueRelease(
        const std::string& mapName,
        std::uint16_t mapId,
        std::uint32_t epoch)
    {
        protocol::AuthorityMessage message;
        message.operation = protocol::AuthorityOperation::Release;
        message.scope = protocol::AuthorityScope::MapSimulation;
        message.mapId = mapId;
        message.mapEpoch = epoch;
        message.mapName = mapName;
        pending_.push_back(std::move(message));
    }

    void MapAuthorityCoordinator::ReportChange(
        const std::string& mapName,
        std::uint64_t actorId,
        std::uint32_t epoch,
        const char* operation)
    {
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "operation=%s map=%s authority_actor_id=%llu epoch=%u local=%s resolver=%s",
            operation,
            mapName.c_str(),
            static_cast<unsigned long long>(actorId),
            epoch,
            actorId != 0 && actorId == localActorId_ ? "true" : "false",
            localRole_ == PeerRole::Host ? "host" : "host-message");
        diagnostics_.Event("MultiplayerMapAuthorityChanged", detail);
    }

    void MapAuthorityCoordinator::Clear() noexcept
    {
        leases_.clear();
        epochCounters_.clear();
        actorOccupancy_.clear();
        actorRequests_.clear();
        pending_.clear();
        nextRequestOrder_ = 0;
        localActorId_ = 0;
    }
}
