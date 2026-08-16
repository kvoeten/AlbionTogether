#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerMutationEvent.h"
#include "Multiplayer/Protocol/EntityLowSimulationMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace fable::game::npc::simulation
{
    class DummyVillagerService;
}

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
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class LiveEntityRegistry;
}

namespace fable::multiplayer::replication
{
    // Replicates the bounded mutable portion of CTCDummyVillager. The host
    // revisions every accepted map-owner mutation, and one latest value is
    // retained per canonical entity generation across native map lifetimes.
    class EntityLowSimulationReplication final : public ReliableMessageSink
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            game::npc::simulation::DummyVillagerService& dummyVillagers,
            const core::Diagnostics& diagnostics);
        bool Process(const entities::LiveEntityRegistry& liveEntities);
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void Shutdown() noexcept;

    private:
        struct AppliedState final
        {
            void* thing = nullptr;
            std::uint32_t revision = 0;
        };

        struct AuthoredBaseline final
        {
            void* thing = nullptr;
            std::uint32_t generation = 0;
            std::uint32_t mapEpoch = 0;
            std::uint64_t publisherActorId = 0;
        };

        using HostProjectionTable = std::unordered_map<
            std::uint64_t,
            game::npc::simulation::DummyVillagerState>;

        static constexpr std::size_t EventCapacity = 1024;
        static constexpr std::size_t MessageCapacity = 1024;
        static constexpr std::uint64_t AuthorityGraceMilliseconds = 1'500;

        static void CaptureMutation(
            void* context,
            const game::npc::simulation::DummyVillagerMutationEvent& event);
        static bool ResolveHostProjection(
            void* context,
            std::uint64_t thingUid,
            game::npc::simulation::DummyVillagerState& state);
        void Enqueue(
            const game::npc::simulation::DummyVillagerMutationEvent& event)
            noexcept;
        bool Author(
            const game::npc::simulation::DummyVillagerMutationEvent& event,
            const entities::LiveEntityRegistry& liveEntities,
            std::uint64_t now,
            bool& deferred);
        bool PublishBaselines(
            const entities::LiveEntityRegistry& liveEntities);
        bool HostAccept(
            protocol::EntityLowSimulationMessage message,
            std::uint64_t sourceActorId);
        bool AcceptAuthoritative(
            const protocol::EntityLowSimulationMessage& message);
        bool Publish(protocol::EntityLowSimulationMessage message);
        bool PublishPending();
        bool PublishPeerBaseline();
        void ApplyLatest(const entities::LiveEntityRegistry& liveEntities);
        void RefreshHostProjectionSnapshot();
        [[nodiscard]] std::uint32_t NextHostRevision(
            std::uint64_t entityUid) noexcept;
        [[nodiscard]] std::uint32_t NextLocalRevision() noexcept;
        static bool IsNewer(
            std::uint32_t candidate,
            std::uint32_t current) noexcept;

        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::npc::simulation::DummyVillagerService* dummyVillagers_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::mutex eventMutex_;
        std::deque<game::npc::simulation::DummyVillagerMutationEvent> events_;
        std::deque<game::npc::simulation::DummyVillagerMutationEvent>
            deferred_;
        std::deque<protocol::EntityLowSimulationMessage> pending_;
        std::unordered_map<
            std::uint64_t,
            protocol::EntityLowSimulationMessage> latest_;
        std::unordered_map<std::uint64_t, std::uint32_t> hostRevisions_;
        std::unordered_map<std::uint64_t, AppliedState> applied_;
        std::unordered_map<std::uint64_t, AuthoredBaseline>
            authoredBaselines_;
        // The native serializer may run on Fable's file-writer thread. It
        // must never traverse the session-owned lifecycle, identity, or
        // latest-state maps directly.
        std::shared_ptr<const HostProjectionTable> hostProjectionSnapshot_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        unsigned int reportedDroppedEvents_ = 0;
        std::uint32_t nextLocalRevision_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
