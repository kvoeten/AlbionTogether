#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    // Replicates current Thing lifecycle state. The host directory is the
    // canonical world/save projection; guests can only submit fenced map-owner
    // observations and never assign generations or revisions themselves.
    class EntityLifecycleReplication final : public ReliableMessageSink
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            const core::Diagnostics& diagnostics);
        bool Reconcile(
            const LiveEntityRegistry& liveEntities,
            const std::vector<LiveEntityChange>& changes,
            bool baselineRequired,
            const std::string& localMap,
            std::uint16_t localMapId,
            bool ownerRosterReady);
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        bool HostAcceptMovement(
            const protocol::EntityMovementMessage& message) noexcept;
        bool SubmitVillageMembershipMutation(
            std::uint64_t entityUid,
            std::uint64_t villageUid);
        bool SubmitOwnedTransfer(
            std::uint64_t entityUid,
            std::uint16_t destinationMapId,
            const game::Vector3& destinationPosition,
            float destinationFacing);

        [[nodiscard]] const WorldEntityDirectory& Directory() const noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingMessageCapacity = 32768;

        bool ObserveLocalChange(
            const LiveEntityChange& change,
            const std::string& localMap,
            std::uint16_t localMapId,
            std::uint32_t mapEpoch);
        bool QueueLocalSnapshot(
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint16_t localMapId,
            std::uint32_t mapEpoch);
        bool CompleteLocalMapRoster(
            const std::string& localMap,
            std::uint32_t mapEpoch);
        bool HostAcceptTransfer(
            const protocol::EntityLifecycleMessage& intent,
            std::uint64_t sourceActorId,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool ResolveUnknownMapNames();
        bool HostReconcileMapAuthorities();
        [[nodiscard]] bool HostMayCompleteMapRoster(
            const protocol::EntityLifecycleMessage& message,
            std::uint64_t sourceActorId) const noexcept;
        bool FlushPendingTransfers();
        bool QueueBaseline();
        bool Queue(protocol::EntityLifecycleMessage message);
        bool PublishPending();
        [[nodiscard]] protocol::EntityLifecycleMessage LocalIntent(
            const LiveEntityRecord& record,
            bool present,
            const std::string& localMap,
            std::uint16_t localMapId,
            std::uint32_t mapEpoch) const;
        [[nodiscard]] std::uint32_t NextBaselineId() noexcept;

        WorldEntityDirectory directory_;
        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        std::uint32_t nextBaselineId_ = 0;
        std::uint32_t lastOwnedMapEpoch_ = 0;
        std::string lastOwnedMap_;
        bool lastOwnedRosterReady_ = false;
        std::deque<protocol::EntityLifecycleMessage> pending_;
        std::unordered_map<
            std::uint64_t,
            protocol::EntityLifecycleMessage> pendingTransfers_;
        std::unordered_map<
            std::string,
            protocol::EntityLifecycleMessage> mapSeedAllowances_;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
