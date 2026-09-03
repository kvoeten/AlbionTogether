#include "WorldSectionSnapshotMessage.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireWorldSectionSnapshotHeader final
    {
        std::uint8_t operation = 0;
        std::uint8_t section = 0;
        std::uint16_t reserved = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t sessionRevision = 0;
        std::uint64_t snapshotRevision = 0;
        std::uint64_t transferId = 0;
        std::uint32_t totalBytes = 0;
        std::uint32_t offset = 0;
        std::uint16_t chunkBytes = 0;
        std::uint16_t reserved2 = 0;
        std::uint64_t hash = 0;
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<
        WireWorldSectionSnapshotHeader>);
    static_assert(sizeof(WireWorldSectionSnapshotHeader) ==
        fable::multiplayer::protocol::WorldSectionSnapshotHeaderBytes);

    bool IsValidSection(
        const fable::multiplayer::protocol::WorldSection section) noexcept
    {
        using fable::multiplayer::protocol::WorldSection;
        return section == WorldSection::Regions ||
            section == WorldSection::Factions;
    }

    bool IsSane(
        const fable::multiplayer::protocol::WorldSectionSnapshotMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        if (!IsValidSection(message.section) ||
            (message.operation != WorldSectionSnapshotOperation::Begin &&
                message.operation != WorldSectionSnapshotOperation::Chunk &&
                message.operation != WorldSectionSnapshotOperation::Commit) ||
            message.authorityEpoch == 0 || message.sessionRevision == 0 ||
            message.snapshotRevision == 0 || message.transferId == 0 ||
            message.totalBytes == 0 ||
            message.totalBytes > MaximumWorldSectionSnapshotBytes ||
            message.chunkSize > MaximumWorldSectionSnapshotChunkBytes() ||
            message.chunkSize > (std::numeric_limits<std::uint16_t>::max)() ||
            (message.chunkSize != 0 && message.chunk == nullptr))
        {
            return false;
        }
        if (message.operation == WorldSectionSnapshotOperation::Begin)
        {
            return message.offset == 0 && message.chunkSize == 0;
        }
        if (message.operation == WorldSectionSnapshotOperation::Commit)
        {
            return message.offset == message.totalBytes &&
                message.chunkSize == 0;
        }
        return message.totalBytes != 0 && message.chunkSize != 0 &&
            message.offset < message.totalBytes &&
            message.chunkSize <= message.totalBytes - message.offset;
    }
}

namespace fable::multiplayer::protocol
{
    std::size_t MaximumWorldSectionSnapshotChunkBytes() noexcept
    {
        return MaximumPayloadBytes() - WorldSectionSnapshotHeaderBytes;
    }

    bool EncodeWorldSectionSnapshotMessage(
        const WorldSectionSnapshotMessage& message,
        std::uint8_t* const destination,
        const std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        const std::size_t totalSize =
            sizeof(WireWorldSectionSnapshotHeader) + message.chunkSize;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < totalSize)
        {
            return false;
        }
        WireWorldSectionSnapshotHeader wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.section = static_cast<std::uint8_t>(message.section);
        wire.authorityEpoch = message.authorityEpoch;
        wire.sessionRevision = message.sessionRevision;
        wire.snapshotRevision = message.snapshotRevision;
        wire.transferId = message.transferId;
        wire.totalBytes = message.totalBytes;
        wire.offset = message.offset;
        wire.chunkBytes = static_cast<std::uint16_t>(message.chunkSize);
        wire.hash = message.hash;
        std::memcpy(destination, &wire, sizeof(wire));
        if (message.chunkSize != 0)
        {
            std::memcpy(destination + sizeof(wire), message.chunk,
                message.chunkSize);
        }
        encodedSize = totalSize;
        return true;
    }

    bool DecodeWorldSectionSnapshotMessage(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        WorldSectionSnapshotMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount < sizeof(WireWorldSectionSnapshotHeader))
        {
            return false;
        }
        WireWorldSectionSnapshotHeader wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.reserved != 0 || wire.reserved2 != 0 ||
            byteCount != sizeof(wire) + wire.chunkBytes)
        {
            return false;
        }
        message.operation = static_cast<WorldSectionSnapshotOperation>(
            wire.operation);
        message.section = static_cast<WorldSection>(wire.section);
        message.authorityEpoch = wire.authorityEpoch;
        message.sessionRevision = wire.sessionRevision;
        message.snapshotRevision = wire.snapshotRevision;
        message.transferId = wire.transferId;
        message.totalBytes = wire.totalBytes;
        message.offset = wire.offset;
        message.hash = wire.hash;
        message.chunk = wire.chunkBytes == 0
            ? nullptr
            : bytes + sizeof(wire);
        message.chunkSize = wire.chunkBytes;
        return IsSane(message);
    }
}
