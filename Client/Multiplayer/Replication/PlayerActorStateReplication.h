#pragma once

#include "Multiplayer/Protocol/PlayerActorStateMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/PlayerActorStatePublicationQueue.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace fable::core { struct Diagnostics; }
namespace fable::multiplayer { class UdpPeer; }

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;

    // Reliable structural lifecycle for player actors. PlayerState is the
    // materialized current view; only its transform is sent on the lossy lane.
    // This service owns the replace-in-place baseline consumed by actions,
    // vitals, and presentation.
    class PlayerActorStateReplication final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::PlayerActorState};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            std::uint32_t authorityEpoch,
            LocalHeroReplication& localHero,
            RemotePlayerChannels& remoteChannels,
            const core::Diagnostics& diagnostics);
        bool Process();
        bool HandleReliableMessage(const TransportMessage& message) override;

        // A caller can use these read-only gates before submitting action or
        // vitals state. The returned record is owned by this service.
        [[nodiscard]] const protocol::PlayerActorStateMessage* Lifecycle(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] const protocol::PlayerActorStateMessage* Lifecycle(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool IsLifecycleActive(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] bool IsLifecycleActive(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool IsLocalActiveAcknowledged() const noexcept;
        [[nodiscard]] std::uint32_t LocalActorGeneration() const noexcept;
        [[nodiscard]] std::uint32_t LocalAuthorityEpoch() const noexcept;

        // RetireLocal is intentionally explicit: a process shutdown must not
        // enqueue traffic after transport teardown has started.
        bool RetireLocal();
        void Shutdown() noexcept;

    private:
        bool Publish(protocol::PlayerActorStateMessage message);
        bool PublishPending();
        bool AcceptHost(
            protocol::PlayerActorStateMessage message,
            std::uint64_t sourceActorId,
            std::uint64_t sourceConnectionNonce);
        bool AcceptHostLocal(protocol::PlayerActorStateMessage message);
        bool AcceptAuthoritative(
            const protocol::PlayerActorStateMessage& message,
            std::uint64_t sourceConnectionNonce);
        bool QueueAuthoritative(
            const protocol::PlayerActorStateMessage& message);
        bool EnsureLocalConstruct(const PlayerState& state);
        bool ReconcileLocal(const PlayerState& state);
        [[nodiscard]] protocol::PlayerActorStateMessage MakeLocalMessage(
            const PlayerState& state,
            protocol::PlayerActorStateOperation operation);
        [[nodiscard]] static protocol::PlayerActorStateMessage MergeDelta(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& delta);
        [[nodiscard]] std::uint32_t NextGeneration() noexcept;
        [[nodiscard]] std::uint32_t NextRevision() noexcept;
        [[nodiscard]] static bool SameAppearance(
            const protocol::PlayerActorStateMessage& left,
            const PlayerState& right) noexcept;
        [[nodiscard]] static bool SameEquipment(
            const protocol::PlayerActorStateMessage& left,
            const PlayerState& right) noexcept;

        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        UdpPeer* transport_ = nullptr;
        LocalHeroReplication* localHero_ = nullptr;
        RemotePlayerChannels* remoteChannels_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint64_t, protocol::PlayerActorStateMessage>
            lifecycles_;
        std::unordered_map<std::uint64_t, std::uint64_t>
            lifecycleConnectionNonces_;
        PlayerActorStatePublicationQueue publicationQueue_;
        std::uint64_t knownPeerRevision_ = 0;
        std::uint32_t nextStructuralRevision_ = 1;
        std::uint32_t localMapEpoch_ = 1;
        std::uint32_t localIntentRevision_ = 1;
        std::uint32_t localActorGeneration_ = 0;
        std::uint32_t lastLocalGeneration_ = 0;
        std::uint32_t localAuthorityEpoch_ = 1;
        std::string localMapName_;
        std::uint16_t localMapId_ = 0;
        bool localActiveAcknowledged_ = false;
        bool localConstructSent_ = false;
        bool localRetired_ = false;
        std::uint32_t retiredGeneration_ = 0;
        std::uint32_t retiredMapEpoch_ = 0;
        std::uint64_t transportConnectionNonce_ = 0;
        bool initialized_ = false;
    };
}
