#include "SavedEntityMapBaselineMessage.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
    constexpr std::uint32_t MaximumMapRecords = 4'096;
    constexpr std::uint32_t MaximumBlobBytes = 8 * 1024 * 1024;
    constexpr std::uint64_t EmptyHash = 14695981039346656037ull;

#pragma pack(push, 1)
    struct WireSavedEntityMapBaselineHeader final
    {
        std::uint8_t operation = 0;
        std::uint8_t format = 0;
        std::uint8_t present = 0;
        std::uint8_t collection = 0;
        std::uint16_t mapId = 0;
        std::uint16_t collectionRecordCount = 0;
        std::uint64_t transferId = 0;
        std::uint64_t baselineRevision = 0;
        std::uint32_t metadata = 0;
        std::uint32_t totalBytes = 0;
        std::uint32_t offset = 0;
        std::uint16_t chunkBytes = 0;
        std::uint16_t collectionRecordIndex = 0;
        std::uint64_t hash = 0;
    };
#pragma pack(pop)

    static_assert(
        std::is_trivially_copyable_v<WireSavedEntityMapBaselineHeader>);
    static_assert(
        sizeof(WireSavedEntityMapBaselineHeader) ==
            fable::multiplayer::protocol::
                SavedEntityMapBaselineHeaderBytes);

    bool IsSane(
        const fable::multiplayer::protocol::
            SavedEntityMapBaselineMessage& message) noexcept
    {
        using namespace fable::multiplayer::protocol;
        using Format = fable::game::entity::persistence::
            SavedEntityMapBlobFormat;
        const bool isCollectionBegin = message.operation ==
            SavedEntityMapBaselineOperation::CollectionBegin;
        const bool isCollectionCommit = message.operation ==
            SavedEntityMapBaselineOperation::CollectionCommit;
        const bool isMapOperation =
            message.operation == SavedEntityMapBaselineOperation::Begin ||
            message.operation == SavedEntityMapBaselineOperation::Chunk ||
            message.operation == SavedEntityMapBaselineOperation::Commit;
        if ((!isMapOperation && !isCollectionBegin && !isCollectionCommit) ||
            (message.collection != (isCollectionBegin || isCollectionCommit) &&
                !isMapOperation) ||
            (!message.collection &&
                (message.collectionRecordCount != 0 ||
                    message.collectionRecordIndex != 0)) ||
            (message.collection &&
                ((isCollectionCommit && message.collectionRecordIndex >
                        message.collectionRecordCount) ||
                    (!isCollectionBegin && !isCollectionCommit &&
                        message.collectionRecordIndex >=
                            message.collectionRecordCount))) ||
            (message.collection && !isCollectionBegin &&
                message.collectionRecordCount == 0) ||
            (message.collection && message.collectionRecordCount >
                MaximumMapRecords) ||
            (isCollectionBegin && message.collectionRecordIndex != 0) ||
            (isCollectionCommit && message.collectionRecordIndex !=
                message.collectionRecordCount) ||
            (isMapOperation && !message.collection && message.mapId == 0) ||
            (message.collection && (isCollectionBegin || isCollectionCommit) &&
                message.mapId != 0) ||
            ((isCollectionBegin || isCollectionCommit) &&
                (message.format != Format::Binary || !message.present ||
                    message.metadata != 0 || message.totalBytes != 0 ||
                    message.offset != 0 || message.hash != EmptyHash ||
                    message.chunkSize != 0)) ||
            (isMapOperation && message.collection && message.mapId == 0) ||
            (message.format != Format::Binary &&
                message.format != Format::Text) ||
            ((!message.collection || isMapOperation) &&
                message.mapId >= MaximumMapRecords) ||
            message.transferId == 0 || message.baselineRevision == 0 ||
            message.totalBytes > MaximumBlobBytes ||
            message.chunkSize > MaximumSavedEntityMapChunkBytes() ||
            message.chunkSize >
                (std::numeric_limits<std::uint16_t>::max)() ||
            (message.chunkSize != 0 && message.chunk == nullptr))
        {
            return false;
        }
        if (!message.present &&
            (message.format != Format::Binary || message.metadata != 0 ||
                message.totalBytes != 0 || message.hash != EmptyHash))
        {
            return false;
        }
        if (isCollectionBegin || isCollectionCommit)
        {
            return true;
        }
        if (message.operation == SavedEntityMapBaselineOperation::Begin)
        {
            return message.offset == 0 && message.chunkSize == 0;
        }
        if (message.operation == SavedEntityMapBaselineOperation::Commit)
        {
            return message.offset == message.totalBytes &&
                message.chunkSize == 0;
        }
        return message.present && message.totalBytes != 0 &&
            message.chunkSize != 0 && message.offset < message.totalBytes &&
            message.chunkSize <= message.totalBytes - message.offset;
    }
}

namespace fable::multiplayer::protocol
{
    std::size_t MaximumSavedEntityMapChunkBytes() noexcept
    {
        return MaximumPayloadBytes() - SavedEntityMapBaselineHeaderBytes;
    }

    bool EncodeSavedEntityMapBaselineMessage(
        const SavedEntityMapBaselineMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        const std::size_t totalSize =
            sizeof(WireSavedEntityMapBaselineHeader) + message.chunkSize;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < totalSize)
        {
            return false;
        }
        WireSavedEntityMapBaselineHeader wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.format = static_cast<std::uint8_t>(message.format);
        wire.present = message.present ? 1u : 0u;
        wire.collection = message.collection ? 1u : 0u;
        wire.mapId = message.mapId;
        wire.collectionRecordCount = message.collectionRecordCount;
        wire.transferId = message.transferId;
        wire.baselineRevision = message.baselineRevision;
        wire.metadata = message.metadata;
        wire.totalBytes = message.totalBytes;
        wire.offset = message.offset;
        wire.chunkBytes = static_cast<std::uint16_t>(message.chunkSize);
        wire.collectionRecordIndex = message.collectionRecordIndex;
        wire.hash = message.hash;
        std::memcpy(destination, &wire, sizeof(wire));
        if (message.chunkSize != 0)
        {
            std::memcpy(
                destination + sizeof(wire),
                message.chunk,
                message.chunkSize);
        }
        encodedSize = totalSize;
        return true;
    }

    bool DecodeSavedEntityMapBaselineMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        SavedEntityMapBaselineMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount < sizeof(WireSavedEntityMapBaselineHeader))
        {
            return false;
        }
        WireSavedEntityMapBaselineHeader wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.collection > 1 ||
            wire.present > 1 ||
            byteCount != sizeof(wire) + wire.chunkBytes)
        {
            return false;
        }
        message.operation =
            static_cast<SavedEntityMapBaselineOperation>(wire.operation);
        message.format = static_cast<
            game::entity::persistence::SavedEntityMapBlobFormat>(
                wire.format);
        message.present = wire.present != 0;
        message.collection = wire.collection != 0;
        message.mapId = wire.mapId;
        message.collectionRecordCount = wire.collectionRecordCount;
        message.collectionRecordIndex = wire.collectionRecordIndex;
        message.transferId = wire.transferId;
        message.baselineRevision = wire.baselineRevision;
        message.metadata = wire.metadata;
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
