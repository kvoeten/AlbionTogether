#include "PacketEnvelope.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
    constexpr std::uint32_t kProtocolMagic = 0x504D5446;
    constexpr std::uint16_t kProtocolVersion = 40;

#pragma pack(push, 1)
    struct WirePacketHeader final
    {
        std::uint32_t magic = kProtocolMagic;
        std::uint16_t version = kProtocolVersion;
        std::uint16_t size = 0;
        std::uint8_t type = 0;
        std::uint8_t flags = 0;
        std::uint16_t payloadSize = 0;
        std::uint64_t sourceActorId = 0;
        std::uint64_t connectionNonce = 0;
        std::uint64_t streamId = 0;
        std::uint8_t streamKind = 0;
        std::uint8_t reserved[3] = {};
        std::uint64_t streamIncarnation = 0;
        std::uint32_t sequence = 0;
    };
#pragma pack(pop)

    static_assert(
        sizeof(WirePacketHeader) ==
        fable::multiplayer::protocol::PacketHeaderBytes);
    static_assert(std::is_trivially_copyable_v<WirePacketHeader>);

    bool IsKnownType(fable::multiplayer::protocol::PacketType type) noexcept
    {
        using fable::multiplayer::protocol::PacketType;
        return type >= PacketType::PlayerMovement &&
            type < PacketType::Count;
    }
}

namespace fable::multiplayer::protocol
{
    std::size_t MaximumPayloadBytes() noexcept
    {
        return MaximumDatagramBytes - PacketHeaderBytes;
    }

    bool EncodePacket(
        const PacketEnvelope& envelope,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        const std::size_t packetSize = sizeof(WirePacketHeader) + payloadSize;
        const bool orderedPacket =
            (envelope.flags & packet_flag::Reliable) != 0 ||
            envelope.type == PacketType::Acknowledgement;
        const bool zeroSubjectStream = envelope.streamKind == 0 ||
            envelope.streamKind == 3;
        if (!IsKnownType(envelope.type) || envelope.sourceActorId == 0 ||
            (envelope.flags & ~packet_flag::All) != 0 ||
            envelope.connectionNonce == 0 ||
            envelope.streamKind > 3 ||
            (zeroSubjectStream && envelope.streamId != 0) ||
            (!zeroSubjectStream && envelope.streamId == 0) ||
            (orderedPacket && envelope.streamIncarnation == 0) ||
            (!orderedPacket &&
                (envelope.streamKind != 0 || envelope.streamId != 0 ||
                    envelope.streamIncarnation != 0)) ||
            (payloadSize != 0 && payload == nullptr) ||
            destination == nullptr || packetSize > MaximumDatagramBytes ||
            packetSize > destinationCapacity ||
            packetSize > std::numeric_limits<std::uint16_t>::max())
        {
            return false;
        }

        WirePacketHeader header;
        header.size = static_cast<std::uint16_t>(packetSize);
        header.type = static_cast<std::uint8_t>(envelope.type);
        header.flags = envelope.flags;
        header.payloadSize = static_cast<std::uint16_t>(payloadSize);
        header.sourceActorId = envelope.sourceActorId;
        header.connectionNonce = envelope.connectionNonce;
        header.streamId = envelope.streamId;
        header.streamKind = envelope.streamKind;
        header.streamIncarnation = envelope.streamIncarnation;
        header.sequence = envelope.sequence;
        std::memcpy(destination, &header, sizeof(header));
        if (payloadSize != 0)
        {
            std::memcpy(destination + sizeof(header), payload, payloadSize);
        }
        encodedSize = packetSize;
        return true;
    }

    bool DecodePacket(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PacketView& packet) noexcept
    {
        packet = {};
        if (bytes == nullptr || byteCount < sizeof(WirePacketHeader) ||
            byteCount > MaximumDatagramBytes)
        {
            return false;
        }

        WirePacketHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        const PacketType type = static_cast<PacketType>(header.type);
        const bool orderedPacket =
            (header.flags & packet_flag::Reliable) != 0 ||
            type == PacketType::Acknowledgement;
        const bool zeroSubjectStream = header.streamKind == 0 ||
            header.streamKind == 3;
        if (header.magic != kProtocolMagic ||
            header.version != kProtocolVersion ||
            header.size != byteCount ||
            header.payloadSize != byteCount - sizeof(WirePacketHeader) ||
            !IsKnownType(type) || header.sourceActorId == 0 ||
            header.connectionNonce == 0 ||
            header.streamKind > 3 ||
            (zeroSubjectStream && header.streamId != 0) ||
            (!zeroSubjectStream && header.streamId == 0) ||
            (orderedPacket && header.streamIncarnation == 0) ||
            (!orderedPacket &&
                (header.streamKind != 0 || header.streamId != 0 ||
                    header.streamIncarnation != 0)) ||
            header.reserved[0] != 0 || header.reserved[1] != 0 ||
            header.reserved[2] != 0 ||
            (header.flags & ~packet_flag::All) != 0)
        {
            return false;
        }

        packet.envelope.type = type;
        packet.envelope.flags = header.flags;
        packet.envelope.sourceActorId = header.sourceActorId;
        packet.envelope.connectionNonce = header.connectionNonce;
        packet.envelope.streamId = header.streamId;
        packet.envelope.streamKind = header.streamKind;
        packet.envelope.streamIncarnation = header.streamIncarnation;
        packet.envelope.sequence = header.sequence;
        packet.payload = bytes + sizeof(WirePacketHeader);
        packet.payloadSize = header.payloadSize;
        return true;
    }
}
