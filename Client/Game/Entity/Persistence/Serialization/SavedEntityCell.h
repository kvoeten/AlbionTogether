#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable::game::entity::persistence::serialization
{
    // Inflated saved-entity cells are a bounded table of named payloads:
    // [u32 count] ([nul-terminated name] [u32 payload bytes] [payload])...
    // Views contain offsets only and never outlive the source byte buffer.
    struct SavedEntityCellFrameView final
    {
        std::size_t nameOffset = 0;
        std::size_t nameBytes = 0;
        std::size_t lengthOffset = 0;
        std::size_t payloadOffset = 0;
        std::size_t payloadBytes = 0;
    };

    struct SavedEntityCellRecordView final
    {
        std::size_t frameIndex = 0;
        std::size_t lengthOffset = 0;
        std::size_t recordOffset = 0;
        std::size_t recordBytes = 0;
    };

    bool ParseSavedEntityCell(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        std::vector<SavedEntityCellFrameView>& frames) noexcept;

    // Parses the length-prefixed entity stream inside every section. Each
    // section ends with a zero-length record; no trailing bytes are accepted.
    bool ParseSavedEntityCellRecords(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        std::vector<SavedEntityCellFrameView>& frames,
        std::vector<SavedEntityCellRecordView>& records) noexcept;
}
