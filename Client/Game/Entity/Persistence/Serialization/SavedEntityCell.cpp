#include "SavedEntityCell.h"

#include <algorithm>
#include <cstring>

namespace
{
    constexpr std::size_t CellPrefixBytes = sizeof(std::uint32_t);
    constexpr std::size_t MaximumCellBytes = 8 * 1024 * 1024;
    constexpr std::size_t MaximumFrameCount = 1024;
    constexpr std::size_t MaximumNameBytes = 128;

    bool CanRead(
        const std::size_t offset,
        const std::size_t count,
        const std::size_t size) noexcept
    {
        return offset <= size && count <= size - offset;
    }
}

namespace fable::game::entity::persistence::serialization
{
    bool ParseSavedEntityCell(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        std::vector<SavedEntityCellFrameView>& frames) noexcept
    {
        frames.clear();
        if (bytes == nullptr || byteCount < CellPrefixBytes ||
            byteCount > MaximumCellBytes)
        {
            return false;
        }

        std::uint32_t frameCount = 0;
        std::memcpy(&frameCount, bytes, sizeof(frameCount));
        if (frameCount > MaximumFrameCount)
        {
            return false;
        }

        try
        {
            frames.reserve(frameCount);
            std::size_t cursor = CellPrefixBytes;
            for (std::uint32_t index = 0; index < frameCount; ++index)
            {
                if (cursor >= byteCount)
                {
                    frames.clear();
                    return false;
                }
                const std::size_t availableNameBytes = (std::min)(
                    MaximumNameBytes + 1,
                    byteCount - cursor);
                const void* const terminator = std::memchr(
                    bytes + cursor,
                    0,
                    availableNameBytes);
                if (terminator == nullptr)
                {
                    frames.clear();
                    return false;
                }
                const auto* const nameEnd =
                    static_cast<const std::uint8_t*>(terminator);
                const std::size_t nameBytes = static_cast<std::size_t>(
                    nameEnd - (bytes + cursor));
                if (nameBytes == 0 || nameBytes > MaximumNameBytes)
                {
                    frames.clear();
                    return false;
                }

                const std::size_t lengthOffset = cursor + nameBytes + 1;
                if (!CanRead(
                        lengthOffset,
                        sizeof(std::uint32_t),
                        byteCount))
                {
                    frames.clear();
                    return false;
                }
                std::uint32_t payloadBytes = 0;
                std::memcpy(
                    &payloadBytes,
                    bytes + lengthOffset,
                    sizeof(payloadBytes));
                const std::size_t payloadOffset =
                    lengthOffset + sizeof(payloadBytes);
                if (!CanRead(payloadOffset, payloadBytes, byteCount))
                {
                    frames.clear();
                    return false;
                }

                frames.push_back({
                    cursor,
                    nameBytes,
                    lengthOffset,
                    payloadOffset,
                    payloadBytes});
                cursor = payloadOffset + payloadBytes;
            }
            if (cursor != byteCount)
            {
                frames.clear();
                return false;
            }
        }
        catch (...)
        {
            frames.clear();
            return false;
        }
        return true;
    }

    bool ParseSavedEntityCellRecords(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        std::vector<SavedEntityCellFrameView>& frames,
        std::vector<SavedEntityCellRecordView>& records) noexcept
    {
        records.clear();
        if (!ParseSavedEntityCell(bytes, byteCount, frames))
        {
            return false;
        }

        try
        {
            for (std::size_t frameIndex = 0;
                 frameIndex < frames.size();
                 ++frameIndex)
            {
                const SavedEntityCellFrameView& frame = frames[frameIndex];
                std::size_t cursor = frame.payloadOffset;
                const std::size_t payloadEnd =
                    frame.payloadOffset + frame.payloadBytes;
                bool terminated = false;
                while (cursor < payloadEnd)
                {
                    if (!CanRead(cursor, sizeof(std::uint32_t), payloadEnd))
                    {
                        records.clear();
                        frames.clear();
                        return false;
                    }
                    std::uint32_t recordBytes = 0;
                    std::memcpy(
                        &recordBytes,
                        bytes + cursor,
                        sizeof(recordBytes));
                    const std::size_t lengthOffset = cursor;
                    cursor += sizeof(recordBytes);
                    if (recordBytes == 0)
                    {
                        terminated = cursor == payloadEnd;
                        break;
                    }
                    if (!CanRead(cursor, recordBytes, payloadEnd))
                    {
                        records.clear();
                        frames.clear();
                        return false;
                    }
                    records.push_back({
                        frameIndex,
                        lengthOffset,
                        cursor,
                        recordBytes});
                    cursor += recordBytes;
                }
                if (!terminated)
                {
                    records.clear();
                    frames.clear();
                    return false;
                }
            }
        }
        catch (...)
        {
            records.clear();
            frames.clear();
            return false;
        }
        return true;
    }
}
