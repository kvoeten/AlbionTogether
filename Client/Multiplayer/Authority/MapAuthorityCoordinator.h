#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/AuthorityMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fable::multiplayer::authority
{
    struct MapAuthorityLease final
    {
        std::string mapName;
        std::uint64_t actorId = 0;
        std::uint16_t mapId = 0;
        std::uint32_t epoch = 0;
        bool localAuthority = false;
    };

    // The host is the only lease resolver. Guests consume fenced grants and
    // never infer ownership independently from a possibly stale player roster.
    class MapAuthorityCoordinator final
    {
    public:
        void Initialize(
            PeerRole localRole,
            std::uint64_t localActorId,
            const core::Diagnostics& diagnostics);
        void HostReconcile(
            const PlayerState* localPlayer,
            const std::vector<replication::RemotePlayerSnapshot>& remotePlayers);
        bool HostRequest(
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint64_t actorId,
            std::uint32_t observedEpoch);
        bool HostPrepare(
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint64_t actorId,
            std::uint32_t observedEpoch);
        bool Apply(const protocol::AuthorityMessage& message);
        void QueueBaseline();
        bool TakePending(protocol::AuthorityMessage& message);
        void RestorePending(protocol::AuthorityMessage message);
        [[nodiscard]] const MapAuthorityLease* Find(
            std::uint16_t mapId) const noexcept;
        [[nodiscard]] const MapAuthorityLease* Find(
            const std::string& mapName) const noexcept;
        [[nodiscard]] std::vector<MapAuthorityLease> Snapshot() const;
        void Clear() noexcept;

    private:
        struct ActorOccupancy final
        {
            std::string mapName;
            std::uint16_t mapId = 0;
        };

        struct MapRequest final
        {
            std::string mapName;
            std::uint16_t mapId = 0;
            std::uint64_t order = 0;
            std::uint32_t observedEpoch = 0;
            bool activated = false;
        };

        void ObserveOccupancy(
            const PlayerState& player,
            std::unordered_set<std::uint64_t>& presentActors);
        bool HostRecordRequest(
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint64_t actorId,
            std::uint32_t observedEpoch,
            bool activated);
        void QueueGrant(const MapAuthorityLease& lease);
        void QueueRelease(
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint32_t epoch);
        void ReportChange(
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint64_t actorId,
            std::uint32_t epoch,
            const char* operation);

        PeerRole localRole_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint16_t, MapAuthorityLease> leases_;
        std::unordered_map<std::uint16_t, std::uint32_t> epochCounters_;
        std::unordered_map<std::uint64_t, ActorOccupancy> actorOccupancy_;
        std::unordered_map<std::uint64_t, MapRequest> actorRequests_;
        std::deque<protocol::AuthorityMessage> pending_;
        std::uint64_t nextRequestOrder_ = 0;
    };
}
