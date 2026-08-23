#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/CombatHealthMutationEvent.h"
#include "Multiplayer/Authority/ActionAuthorityCoordinator.h"
#include "Multiplayer/Protocol/EntityVitalsMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
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

namespace fable::multiplayer::presentation
{
    class RemotePlayerRegistry;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;

    // Reliable, mutation-driven health replication. Only the latest revision
    // for each player or current entity generation is retained; dormant
    // entities keep that one value across native map incarnations.
    class EntityVitalsReplication final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::EntityVitals};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            RemotePlayerChannels& remotePlayers,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics);
        bool Process(
            const LocalHeroReplication& localHero,
            const entities::LiveEntityRegistry& liveEntities,
            presentation::RemotePlayerRegistry& remotePlayers);
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void RetirePlayer(std::uint64_t actorId) noexcept;
        void ClearRemotePlayers() noexcept;
        void Shutdown() noexcept;

    private:
        struct AppliedEntity final
        {
            void* creature = nullptr;
            std::uint32_t revision = 0;
        };

        struct AuthoredEntityBaseline final
        {
            void* creature = nullptr;
            std::uint32_t generation = 0;
            std::uint32_t mapEpoch = 0;
            std::uint64_t publisherActorId = 0;
        };

        static constexpr std::size_t EventCapacity = 1024;
        static constexpr std::size_t MessageCapacity = 1024;
        static constexpr std::uint64_t AuthorityGraceMilliseconds = 1'500;

        static void CaptureMutation(
            void* context,
            const game::creature::combat::CombatHealthMutationEvent& event);
        void Enqueue(
            const game::creature::combat::CombatHealthMutationEvent& event)
            noexcept;
        bool AuthorPlayer(
            const game::creature::combat::CombatHealthMutationEvent& event);
        bool AuthorEntity(
            const game::creature::combat::CombatHealthMutationEvent& event,
            const entities::LiveEntityRegistry& liveEntities,
            std::uint64_t now,
            bool& deferred);
        bool PublishBaselines(
            const LocalHeroReplication& localHero,
            const entities::LiveEntityRegistry& liveEntities);
        bool HostAccept(
            protocol::EntityVitalsMessage message,
            std::uint64_t sourceActorId,
            std::uint64_t sourceConnectionNonce,
            const entities::LiveEntityRegistry* liveEntities);
        bool AcceptAuthoritative(
            const protocol::EntityVitalsMessage& message,
            std::uint64_t sourceConnectionNonce);
        bool Publish(protocol::EntityVitalsMessage message);
        bool PublishPending();
        bool PublishPeerBaseline();
        void ApplyLatest(
            const entities::LiveEntityRegistry& liveEntities,
            presentation::RemotePlayerRegistry& remotePlayers);
        [[nodiscard]] std::uint32_t NextHostRevision(
            const protocol::EntityVitalsMessage& message) noexcept;
        [[nodiscard]] std::uint32_t NextLocalRevision() noexcept;
        static bool IsNewer(
            std::uint32_t candidate,
            std::uint32_t current) noexcept;

        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        RemotePlayerChannels* remotePlayerChannels_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        const LocalHeroReplication* processingLocalHero_ = nullptr;
        const entities::LiveEntityRegistry* processingLiveEntities_ = nullptr;
        std::mutex eventMutex_;
        std::deque<game::creature::combat::CombatHealthMutationEvent> events_;
        std::deque<game::creature::combat::CombatHealthMutationEvent> deferred_;
        std::deque<protocol::EntityVitalsMessage> pending_;
        std::unordered_map<std::uint64_t, protocol::EntityVitalsMessage>
            latestPlayers_;
        std::unordered_map<std::uint64_t, std::uint64_t>
            latestPlayerConnectionNonces_;
        std::unordered_map<std::uint64_t, protocol::EntityVitalsMessage>
            latestEntities_;
        std::unordered_map<std::uint64_t, std::uint32_t> hostPlayerRevisions_;
        std::unordered_map<std::uint64_t, std::uint32_t> hostEntityRevisions_;
        std::unordered_map<std::uint64_t, AppliedEntity> appliedEntities_;
        std::unordered_map<std::uint64_t, AuthoredEntityBaseline>
            authoredEntityBaselines_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        unsigned int reportedDroppedEvents_ = 0;
        void* authoredPlayerCreature_ = nullptr;
        std::uint32_t nextLocalRevision_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
