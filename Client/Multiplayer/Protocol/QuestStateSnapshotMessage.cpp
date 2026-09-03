#include "QuestStateSnapshotMessage.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireQuestStateSnapshotHeader final
    {
        std::uint8_t operation = 0;
        std::uint8_t reserved[3] = {};
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

    static_assert(std::is_trivially_copyable_v<WireQuestStateSnapshotHeader>);
    static_assert(sizeof(WireQuestStateSnapshotHeader) ==
        fable::multiplayer::protocol::QuestStateSnapshotHeaderBytes);

    bool IsSane(
        const fable::multiplayer::protocol::QuestStateSnapshotMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        if ((message.operation != QuestStateSnapshotOperation::Begin &&
                message.operation != QuestStateSnapshotOperation::Chunk &&
                message.operation != QuestStateSnapshotOperation::Commit) ||
            message.authorityEpoch == 0 || message.sessionRevision == 0 ||
            message.snapshotRevision == 0 || message.transferId == 0 ||
            message.totalBytes > MaximumQuestStateSnapshotBytes ||
            message.chunkSize > MaximumQuestStateSnapshotChunkBytes() ||
            message.chunkSize > (std::numeric_limits<std::uint16_t>::max)() ||
            (message.chunkSize != 0 && message.chunk == nullptr))
        {
            return false;
        }
        if (message.operation == QuestStateSnapshotOperation::Begin)
        {
            return message.offset == 0 && message.chunkSize == 0;
        }
        if (message.operation == QuestStateSnapshotOperation::Commit)
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
    std::size_t MaximumQuestStateSnapshotChunkBytes() noexcept
    {
        return MaximumPayloadBytes() - QuestStateSnapshotHeaderBytes;
    }

    bool EncodeQuestStateSnapshotMessage(
        const QuestStateSnapshotMessage& message,
        std::uint8_t* destination,
        const std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        const std::size_t totalSize =
            sizeof(WireQuestStateSnapshotHeader) + message.chunkSize;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < totalSize)
        {
            return false;
        }
        WireQuestStateSnapshotHeader wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
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

    bool DecodeQuestStateSnapshotMessage(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        QuestStateSnapshotMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount < sizeof(WireQuestStateSnapshotHeader))
        {
            return false;
        }
        WireQuestStateSnapshotHeader wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.reserved[0] != 0 || wire.reserved[1] != 0 ||
            wire.reserved[2] != 0 || wire.reserved2 != 0 ||
            byteCount != sizeof(wire) + wire.chunkBytes)
        {
            return false;
        }
        message.operation = static_cast<QuestStateSnapshotOperation>(
            wire.operation);
        message.authorityEpoch = wire.authorityEpoch;
        message.sessionRevision = wire.sessionRevision;
        message.snapshotRevision = wire.snapshotRevision;
        message.transferId = wire.transferId;
        message.totalBytes = wire.totalBytes;
        message.offset = wire.offset;
        message.hash = wire.hash;
        message.chunk = wire.chunkBytes != 0
            ? bytes + sizeof(wire)
            : nullptr;
        message.chunkSize = wire.chunkBytes;
        return IsSane(message);
    }
}
