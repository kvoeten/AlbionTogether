#pragma once

#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    enum class SavedEntityMapBaselineOperation : std::uint8_t
    {
        Begin = 1,
        Chunk = 2,
        Commit = 3,
        CollectionBegin = 4,
        CollectionCommit = 5,
    };

    struct SavedEntityMapBaselineMessage final
    {
        SavedEntityMapBaselineOperation operation =
            SavedEntityMapBaselineOperation::Begin;
        game::entity::persistence::SavedEntityMapBlobFormat format =
            game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        bool present = false;
        bool collection = false;
        std::uint16_t mapId = 0;
        std::uint16_t collectionRecordCount = 0;
        std::uint16_t collectionRecordIndex = 0;
        std::uint64_t transferId = 0;
        std::uint64_t baselineRevision = 0;
        std::uint32_t metadata = 0;
        std::uint32_t totalBytes = 0;
        std::uint32_t offset = 0;
        std::uint64_t hash = 0;
        const std::uint8_t* chunk = nullptr;
        std::size_t chunkSize = 0;
    };

    inline constexpr std::size_t SavedEntityMapBaselineHeaderBytes = 48;

    [[nodiscard]] std::size_t MaximumSavedEntityMapChunkBytes() noexcept;
    bool EncodeSavedEntityMapBaselineMessage(
        const SavedEntityMapBaselineMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeSavedEntityMapBaselineMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        SavedEntityMapBaselineMessage& message) noexcept;
}
