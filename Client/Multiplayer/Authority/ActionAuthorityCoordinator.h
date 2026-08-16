#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Authority/MapAuthorityCoordinator.h"
#include "Multiplayer/Protocol/AuthorityMessage.h"
#include "Multiplayer/Protocol/EntityActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace fable::multiplayer::authority
{
    struct EntityAuthorityKey final
    {
        std::uint64_t thingUid = 0;
        std::uint32_t generation = 0;

        [[nodiscard]] bool operator==(
            const EntityAuthorityKey& other) const noexcept
        {
            return thingUid == other.thingUid &&
                generation == other.generation;
        }
    };

    struct EntityAuthorityKeyHash final
    {
        [[nodiscard]] std::size_t operator()(
            const EntityAuthorityKey& key) const noexcept
        {
            const std::uint64_t mixed = key.thingUid ^
                (static_cast<std::uint64_t>(key.generation) << 32);
            return static_cast<std::size_t>(mixed ^ (mixed >> 33));
        }
    };

    struct ActionAuthorityLease final
    {
        EntityAuthorityKey entity = {};
        protocol::ActionLeaseKind kind = protocol::ActionLeaseKind::None;
        std::uint64_t actorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t actionEpoch = 0;
        std::uint64_t grantedAt = 0;
        std::uint64_t lastActivityAt = 0;
        std::string mapName;
        bool localAuthority = false;
    };

    // Host-resolved, per-live-incarnation action leases. Only active leases
    // are retained; a session-wide epoch serial fences stale packets without
    // accumulating per-entity tombstones.
    class ActionAuthorityCoordinator final
    {
    public:
        void Initialize(
            PeerRole localRole,
            std::uint64_t localActorId,
            const core::Diagnostics& diagnostics);
        bool HostAcquire(
            const protocol::EntityActionMessage& intent,
            std::uint64_t sourceActorId,
            const MapAuthorityLease& mapLease,
            ActionAuthorityLease& grantedLease);
        bool HostRelease(
            const EntityAuthorityKey& entity,
            std::uint64_t requestingActorId,
            std::uint32_t actionEpoch);
        bool HostTouch(
            const EntityAuthorityKey& entity,
            std::uint64_t actorId,
            std::uint32_t actionEpoch) noexcept;
        void HostFenceAgainstMaps(
            const MapAuthorityCoordinator& maps,
            const std::unordered_map<std::uint64_t, std::string>& actorMaps);
        bool Apply(const protocol::AuthorityMessage& message);
        void QueueBaseline();
        bool TakePending(protocol::AuthorityMessage& message);
        void RestorePending(protocol::AuthorityMessage message);
        [[nodiscard]] const ActionAuthorityLease* Find(
            const EntityAuthorityKey& entity) const noexcept;
        [[nodiscard]] bool HasOwnedOverride(
            const std::string& mapName,
            std::uint64_t actorId,
            std::uint32_t mapEpoch) const noexcept;
        void Clear() noexcept;

    private:
        static constexpr std::uint64_t PrimaryAttackerMinimumHoldMilliseconds =
            1'000;
        static constexpr std::uint64_t PrimaryAttackerIdleHandoffMilliseconds =
            750;
        static protocol::ActionLeaseKind LeaseKindFor(
            const protocol::EntityActionMessage& intent) noexcept;
        static unsigned int PriorityFor(
            protocol::ActionLeaseKind kind) noexcept;
        std::uint32_t NextEpoch() noexcept;
        void QueueGrant(const ActionAuthorityLease& lease);
        void QueueRelease(const ActionAuthorityLease& lease);
        void ReportChange(
            const ActionAuthorityLease& lease,
            const char* operation);

        PeerRole localRole_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint32_t nextEpoch_ = 0;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<
            EntityAuthorityKey,
            ActionAuthorityLease,
            EntityAuthorityKeyHash> leases_;
        std::deque<protocol::AuthorityMessage> pending_;
    };
}
