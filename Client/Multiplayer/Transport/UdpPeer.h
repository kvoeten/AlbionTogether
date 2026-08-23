#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fable::multiplayer
{
    class UdpPeer final
    {
    public:
        UdpPeer();
        ~UdpPeer();

        UdpPeer(const UdpPeer&) = delete;
        UdpPeer& operator=(const UdpPeer&) = delete;

        bool StartHost(
            std::uint16_t port,
            std::uint64_t localActorId,
            const core::Diagnostics& diagnostics);
        bool StartGuest(
            const std::string& address,
            std::uint16_t port,
            std::uint64_t localActorId,
            const core::Diagnostics& diagnostics);
        bool Submit(const PlayerState& localUpdate);
        bool TryConsume(PlayerState& remoteUpdate);
        // Reliable ordering is independent per stream. Control is stream 0;
        // actor/entity streams are supplied by the owning replication layer.
        bool SubmitReliable(
            ReliableStreamId streamId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize);
        bool TryConsumeReliable(TransportMessage& message);
        bool SubmitUnreliable(
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize);
        // Host-only validated relay preserving the authoritative source actor.
        bool RelayUnreliable(
            std::uint64_t sourceActorId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize);
        bool TryConsumeUnreliable(TransportMessage& message);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsStarted() const noexcept;
        [[nodiscard]] bool HasPeer() const noexcept;
        [[nodiscard]] bool HasFailed() const noexcept;
        [[nodiscard]] std::size_t ConnectedPeerCount() const noexcept;
        // Changes whenever the host's connected endpoint/actor set changes.
        // Consumers use this instead of peer counts so a reconnect replacing
        // one peer with another still receives a fresh bounded baseline.
        [[nodiscard]] std::uint64_t PeerSetRevision() const noexcept;
        // Changes for every local transport start. Guests use this to
        // re-open their reliable actor baseline after reconnecting.
        [[nodiscard]] std::uint64_t ConnectionNonce() const noexcept;
        [[nodiscard]] std::vector<std::uint64_t> ConnectedActorIds() const;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> implementation_;
    };
}
