#include "PeerDatagramCodec.h"

#include "Multiplayer/Protocol/PlayerMovementCodec.h"

#include <cstring>

namespace fable::multiplayer::transport_codec
{
    bool EncodePeerHelloChallenge(
        const std::uint64_t guestNonce,
        const std::uint64_t challenge,
        std::array<std::uint8_t, PeerHelloChallengeBytes>& bytes) noexcept
    {
        if (guestNonce == 0 || challenge == 0)
        {
            return false;
        }
        PeerHelloChallengeWire wire;
        wire.guestNonce = guestNonce;
        wire.challenge = challenge;
        std::memcpy(bytes.data(), &wire, sizeof(wire));
        return true;
    }

    bool DecodePeerHelloChallenge(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        std::array<std::uint8_t, PeerHelloChallengeBytes>& output) noexcept
    {
        if (bytes == nullptr || byteCount != sizeof(PeerHelloChallengeWire))
        {
            return false;
        }
        PeerHelloChallengeWire wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.guestNonce == 0 || wire.challenge == 0)
        {
            return false;
        }
        std::memcpy(output.data(), &wire, sizeof(wire));
        return true;
    }

    bool IsReliablePacketType(const protocol::PacketType type) noexcept
    {
        using protocol::PacketType;
        return type == PacketType::Authority ||
            type == PacketType::EntityLifecycle ||
            type == PacketType::EntityAction ||
            type == PacketType::EntityVitals ||
            type == PacketType::EntityLowSimulation ||
            type == PacketType::PlayerAction ||
            type == PacketType::PopulationState ||
            type == PacketType::SavedEntityMapBaseline ||
            type == PacketType::PlayerActorState;
    }

    bool IsUnreliablePacketType(const protocol::PacketType type) noexcept
    {
        return type == protocol::PacketType::EntityMovement;
    }

    bool EncodePlayerPacket(
        const PlayerState& state,
        const std::uint64_t sourceActorId,
        const std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        if (state.changedProperties != player_property::Movement)
        {
            return false;
        }
        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        protocol::PlayerMovementMessage movement;
        movement.actorId = state.actorId;
        movement.authorityEpoch = state.authorityEpoch;
        movement.actorGeneration = state.actorGeneration;
        movement.mapEpoch = state.mapEpoch;
        movement.sequence = state.sequence;
        movement.mapId = state.mapId;
        movement.moving = state.moving;
        movement.position = state.position;
        movement.velocity = state.velocity;
        movement.facing = state.facing;
        movement.angularVelocity = state.angularVelocity;
        if (!protocol::EncodePlayerMovementMessage(
                movement,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize))
        {
            return false;
        }
        protocol::PacketEnvelope envelope;
        envelope.type = protocol::PacketType::PlayerMovement;
        envelope.sourceActorId = sourceActorId;
        envelope.connectionNonce = connectionNonce;
        return protocol::EncodePacket(
            envelope,
            payload.data(),
            payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodeUnreliablePacket(
        const TransportMessage& message,
        const std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        protocol::PacketEnvelope envelope;
        envelope.type = message.type;
        envelope.sourceActorId = message.sourceActorId;
        envelope.connectionNonce = connectionNonce;
        return protocol::EncodePacket(
            envelope,
            message.payload.data(),
            message.payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodeReliablePacket(
        const TransportMessage& message,
        const std::uint64_t sourceActorId,
        const std::uint64_t connectionNonce,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        protocol::PacketEnvelope envelope;
        envelope.type = message.type;
        envelope.flags = protocol::packet_flag::Reliable;
        envelope.sourceActorId = sourceActorId;
        envelope.connectionNonce = connectionNonce;
        envelope.streamId = message.streamId.subject;
        envelope.streamKind = static_cast<std::uint8_t>(
            message.streamId.kind);
        envelope.streamIncarnation = message.streamIncarnation;
        envelope.sequence = message.sequence;
        return protocol::EncodePacket(
            envelope,
            message.payload.data(),
            message.payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodeAcknowledgement(
        const std::uint64_t sourceActorId,
        const std::uint64_t connectionNonce,
        const ReliableStreamId streamId,
        const std::uint64_t streamIncarnation,
        const std::uint32_t acknowledgedSequence,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        protocol::PacketEnvelope envelope;
        envelope.type = protocol::PacketType::Acknowledgement;
        envelope.sourceActorId = sourceActorId;
        envelope.connectionNonce = connectionNonce;
        envelope.streamId = streamId.subject;
        envelope.streamKind = static_cast<std::uint8_t>(streamId.kind);
        envelope.streamIncarnation = streamIncarnation;
        envelope.sequence = acknowledgedSequence;
        return protocol::EncodePacket(
            envelope,
            nullptr,
            0,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodePeerHello(
        const std::uint64_t sourceActorId,
        const std::uint64_t connectionNonce,
        const std::uint8_t* payload,
        const std::size_t payloadSize,
        std::array<std::uint8_t, protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        protocol::PacketEnvelope envelope;
        envelope.type = protocol::PacketType::PeerHello;
        envelope.sourceActorId = sourceActorId;
        envelope.connectionNonce = connectionNonce;
        return protocol::EncodePacket(
            envelope,
            payload,
            payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }
}
