#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence
{
    enum class SavedEntityMapBlobFormat : std::uint8_t
    {
        Text = 1,
        Binary = 2,
    };

    enum class SavedEntityMapCollectionPhase : std::uint8_t
    {
        Begin = 1,
        Complete = 2,
        Failed = 3,
    };

    // Read-only view of one populated CSavedEntities per-map record. The
    // payload remains owned by the game and is valid only for the duration of
    // the observer callback.
    struct SavedEntityMapBlobSnapshot final
    {
        SavedEntityMapBlobFormat format = SavedEntityMapBlobFormat::Binary;
        std::uint32_t mapId = 0;
        const std::uint8_t* bytes = nullptr;
        std::size_t byteCount = 0;
        std::uint32_t metadata = 0;
        std::uint64_t hash = 0;
    };

    struct SavedEntityMapCollectionEvent final
    {
        SavedEntityMapBlobFormat format = SavedEntityMapBlobFormat::Binary;
        SavedEntityMapCollectionPhase phase =
            SavedEntityMapCollectionPhase::Begin;
        void* savedEntities = nullptr;
        std::size_t recordCount = 0;
    };
}
