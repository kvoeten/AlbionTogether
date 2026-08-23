#pragma once

#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fable::multiplayer::transport_codec
{
    inline constexpr std::size_t PeerHelloChallengeBytes =
        sizeof(std::uint64_t) * 2;

#pragma pack(push, 1)
    struct PeerHelloChallengeWire final
    {
        std::uint64_t guestNonce = 0;
        std::uint64_t challenge = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(PeerHelloChallengeWire) == PeerHelloChallengeBytes);
    static_assert(std::is_trivially_copyable_v<PeerHelloChallengeWire>);

    [[nodiscard]] bool EncodePeerHelloChallenge(
        std::uint64_t guestNonce,
        std::uint64_t challenge,
        std::array<std::uint8_t, PeerHelloChallengeBytes>& bytes) noexcept;
    [[nodiscard]] bool DecodePeerHelloChallenge(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        std::array<std::uint8_t, PeerHelloChallengeBytes>& output) noexcept;

    [[nodiscard]] bool IsReliablePacketType(
        protocol::PacketType type) noexcept;
    [[nodiscard]] bool IsUnreliablePacketType(
        protocol::PacketType type) noexcept;

    [[nodiscard]] bool EncodePlayerPacket(
        const PlayerState& state,
        std::uint64_t sourceActorId,
        std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept;
    [[nodiscard]] bool EncodeUnreliablePacket(
        const TransportMessage& message,
        std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept;
    [[nodiscard]] bool EncodeReliablePacket(
        const TransportMessage& message,
        std::uint64_t sourceActorId,
        std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept;
    [[nodiscard]] bool EncodeAcknowledgement(
        std::uint64_t sourceActorId,
        std::uint64_t connectionNonce,
        ReliableStreamId streamId,
        std::uint64_t streamIncarnation,
        std::uint32_t acknowledgedSequence,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept;
    [[nodiscard]] bool EncodePeerHello(
        std::uint64_t sourceActorId,
        std::uint64_t connectionNonce,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept;
}
