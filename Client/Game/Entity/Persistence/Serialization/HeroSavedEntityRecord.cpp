#include "HeroSavedEntityRecord.h"

#include "SavedEntityCell.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
    using fable::game::entity::persistence::serialization::
        HeroSavedEntityRecord;

    constexpr std::size_t CellPrefixBytes = 4;
    constexpr std::size_t HeroHeaderWords = 3;
    constexpr std::size_t HeroHeaderBytes = HeroHeaderWords * sizeof(
        std::uint32_t);
    constexpr std::size_t HeroUidBytes = sizeof(std::uint64_t);
    constexpr std::size_t HeroTrailerBytes = 12;
    constexpr std::size_t ComponentFixedBytes =
        sizeof(std::uint32_t) + sizeof(std::uint8_t) +
        sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t);
    constexpr std::size_t ComponentTailBytes = sizeof(std::uint32_t);
    constexpr std::size_t MaximumCellBytes = 8 * 1024 * 1024;
    constexpr std::size_t MaximumHeroRecordBytes = 2 * 1024 * 1024;
    constexpr std::size_t MaximumBaseBytes = 512 * 1024;
    constexpr std::size_t MaximumComponentBytes = 512 * 1024;
    constexpr std::size_t MaximumComponentCount = 1024;
    constexpr std::size_t MaximumNameBytes = 128;
    constexpr std::uint32_t ComponentSentinel = 0xFFFFFFFFu;
    constexpr std::uint32_t ComponentVersion = 0x2169u;

    struct ParsedRecord final
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t framedBegin = 0;
        std::size_t framedEnd = 0;
        std::size_t frameIndex = 0;
        std::uint64_t uid = 0;
    };

    bool CanRead(
        const std::size_t offset,
        const std::size_t count,
        const std::size_t size) noexcept
    {
        return offset <= size && count <= size - offset;
    }

    bool ReadU32(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        std::uint32_t& value) noexcept
    {
        if (bytes == nullptr || !CanRead(offset, sizeof(value), size))
        {
            return false;
        }
        std::memcpy(&value, bytes + offset, sizeof(value));
        return true;
    }

    bool ReadU64(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        std::uint64_t& value) noexcept
    {
        if (bytes == nullptr || !CanRead(offset, sizeof(value), size))
        {
            return false;
        }
        std::memcpy(&value, bytes + offset, sizeof(value));
        return true;
    }

    bool ReadExactName(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        const char* const expected,
        std::size_t& next) noexcept
    {
        if (bytes == nullptr || expected == nullptr || offset >= size)
        {
            return false;
        }
        const std::size_t expectedBytes = std::strlen(expected);
        if (expectedBytes > MaximumNameBytes ||
            !CanRead(offset, expectedBytes + 1, size) ||
            std::memcmp(bytes + offset, expected, expectedBytes) != 0 ||
            bytes[offset + expectedBytes] != 0)
        {
            return false;
        }
        next = offset + expectedBytes + 1;
        return true;
    }

    bool ReadAnyName(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        std::size_t& next) noexcept
    {
        if (bytes == nullptr || offset >= size)
        {
            return false;
        }
        const std::size_t remaining = std::min(
            MaximumNameBytes,
            size - offset);
        const void* const terminator = std::memchr(
            bytes + offset,
            0,
            remaining);
        if (terminator == nullptr)
        {
            return false;
        }
        const auto* const end = static_cast<const std::uint8_t*>(terminator);
        if (end == bytes + offset)
        {
            return false;
        }
        next = static_cast<std::size_t>(end - bytes) + 1;
        return true;
    }

    bool ParseHeroRecord(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        const std::size_t begin,
        ParsedRecord& result) noexcept
    {
        if (cell == nullptr || begin >= cellBytes ||
            cellBytes > MaximumCellBytes)
        {
            return false;
        }

        std::size_t offset = 0;
        if (!ReadExactName(
                cell,
                cellBytes,
                begin,
                "PlayerCreature",
                offset) ||
            !CanRead(offset, HeroHeaderBytes + HeroUidBytes + sizeof(
                std::uint32_t), cellBytes))
        {
            return false;
        }

        // These three words are the entity's spatial/header values. Their
        // meaning is version-specific; validating their complete presence is
        // intentional, while preserving them byte-for-byte.
        std::uint32_t header[HeroHeaderWords] = {};
        for (std::size_t i = 0; i < HeroHeaderWords; ++i)
        {
            if (!ReadU32(cell, cellBytes, offset + i * sizeof(std::uint32_t),
                    header[i]))
            {
                return false;
            }
        }
        (void)header;
        offset += HeroHeaderBytes;

        if (!ReadU64(cell, cellBytes, offset, result.uid) ||
            result.uid == 0)
        {
            return false;
        }
        offset += HeroUidBytes;

        std::uint32_t zero = 0;
        if (!ReadU32(cell, cellBytes, offset, zero) || zero != 0)
        {
            return false;
        }
        offset += sizeof(zero);

        if (!ReadExactName(
                cell,
                cellBytes,
                offset,
                "CREATURE_HERO",
                offset) ||
            !CanRead(offset, sizeof(std::uint32_t), cellBytes))
        {
            return false;
        }

        std::uint32_t baseBytes = 0;
        if (!ReadU32(cell, cellBytes, offset, baseBytes) ||
            baseBytes > MaximumBaseBytes)
        {
            return false;
        }
        offset += sizeof(baseBytes);
        if (!CanRead(offset, baseBytes + HeroTrailerBytes, cellBytes))
        {
            return false;
        }
        offset += baseBytes;

        // The trailer is preserved opaquely. Its first and last words are
        // validated native zero sentinels; the middle word is metadata (not a
        // component count), so component parsing ends only at END + zero.
        std::uint32_t trailer[3] = {};
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (!ReadU32(cell, cellBytes, offset + i * sizeof(std::uint32_t),
                    trailer[i]))
            {
                return false;
            }
        }
        if (trailer[0] != 0 || trailer[2] != 0)
        {
            return false;
        }
        offset += HeroTrailerBytes;

        std::size_t componentCount = 0;
        while (true)
        {
            std::size_t endName = offset;
            if (ReadExactName(cell, cellBytes, offset, "END", endName))
            {
                if (!CanRead(endName, sizeof(std::uint32_t), cellBytes))
                {
                    return false;
                }
                std::uint32_t endZero = 0;
                if (!ReadU32(cell, cellBytes, endName, endZero) ||
                    endZero != 0)
                {
                    return false;
                }
                result.begin = begin;
                result.end = endName + sizeof(endZero);
                return result.end > begin &&
                    result.end - begin <= MaximumHeroRecordBytes;
            }

            if (componentCount >= MaximumComponentCount ||
                !ReadAnyName(cell, cellBytes, offset, endName) ||
                !CanRead(endName, ComponentFixedBytes, cellBytes))
            {
                return false;
            }
            offset = endName;

            std::uint32_t pad = 0;
            std::uint32_t sentinel = 0;
            std::uint32_t version = 0;
            std::uint32_t dataBytes = 0;
            if (!ReadU32(cell, cellBytes, offset, pad) || pad != 0 ||
                cell[offset + sizeof(pad)] != 0 ||
                !ReadU32(cell, cellBytes, offset + sizeof(pad) +
                    sizeof(std::uint8_t), sentinel) ||
                sentinel != ComponentSentinel ||
                !ReadU32(cell, cellBytes, offset + sizeof(pad) +
                    sizeof(std::uint8_t) + sizeof(sentinel), version) ||
                version != ComponentVersion ||
                !ReadU32(cell, cellBytes, offset + sizeof(pad) +
                    sizeof(std::uint8_t) + sizeof(sentinel) +
                    sizeof(version), dataBytes) ||
                dataBytes > MaximumComponentBytes)
            {
                return false;
            }
            offset += ComponentFixedBytes;
            if (!CanRead(offset, dataBytes + ComponentTailBytes, cellBytes))
            {
                return false;
            }
            offset += dataBytes;
            std::uint32_t tail = 0;
            if (!ReadU32(cell, cellBytes, offset, tail) || tail != 0)
            {
                return false;
            }
            offset += sizeof(tail);
            ++componentCount;
            if (offset - begin > MaximumHeroRecordBytes)
            {
                return false;
            }
        }
    }

    bool FindExactlyOneHero(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        ParsedRecord& result,
        std::vector<fable::game::entity::persistence::serialization::
            SavedEntityCellFrameView>* const parsedFrames = nullptr) noexcept
    {
        using fable::game::entity::persistence::serialization::
            SavedEntityCellFrameView;
        using fable::game::entity::persistence::serialization::
            SavedEntityCellRecordView;
        using fable::game::entity::persistence::serialization::
            ParseSavedEntityCellRecords;

        std::vector<SavedEntityCellFrameView> frames;
        std::vector<SavedEntityCellRecordView> nativeRecords;
        if (!ParseSavedEntityCellRecords(
                cell, cellBytes, frames, nativeRecords))
        {
            return false;
        }
        std::size_t found = 0;
        for (const SavedEntityCellRecordView& nativeRecord : nativeRecords)
        {
            const std::size_t recordEnd =
                nativeRecord.recordOffset + nativeRecord.recordBytes;
            if (cell[nativeRecord.recordOffset] ==
                    static_cast<std::uint8_t>('P'))
            {
                ParsedRecord candidate;
                if (ParseHeroRecord(
                        cell,
                        recordEnd,
                        nativeRecord.recordOffset,
                        candidate) &&
                    candidate.end <= recordEnd)
                {
                    candidate.framedBegin = nativeRecord.lengthOffset;
                    candidate.framedEnd = recordEnd;
                    // Preserve the whole native entry, including the opaque
                    // post-component trailer used by current retail saves.
                    candidate.end = recordEnd;
                    candidate.frameIndex = nativeRecord.frameIndex;
                    result = candidate;
                    ++found;
                }
            }
        }
        if (found == 1 && parsedFrames != nullptr)
        {
            *parsedFrames = std::move(frames);
        }
        return found == 1;
    }

    bool CollectHeroRanges(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        std::vector<fable::game::entity::persistence::serialization::
            SavedEntityCellFrameView>& frames,
        std::vector<ParsedRecord>& records)
    {
        using fable::game::entity::persistence::serialization::
            SavedEntityCellRecordView;
        using fable::game::entity::persistence::serialization::
            ParseSavedEntityCellRecords;

        std::vector<SavedEntityCellRecordView> nativeRecords;
        if (!ParseSavedEntityCellRecords(
                cell, cellBytes, frames, nativeRecords))
        {
            return false;
        }
        records.clear();
        for (const SavedEntityCellRecordView& nativeRecord : nativeRecords)
        {
            const std::size_t recordEnd =
                nativeRecord.recordOffset + nativeRecord.recordBytes;
            if (cell[nativeRecord.recordOffset] ==
                    static_cast<std::uint8_t>('P'))
            {
                ParsedRecord candidate;
                if (ParseHeroRecord(
                        cell,
                        recordEnd,
                        nativeRecord.recordOffset,
                        candidate) &&
                    candidate.end <= recordEnd)
                {
                    candidate.framedBegin = nativeRecord.lengthOffset;
                    candidate.framedEnd = recordEnd;
                    candidate.end = recordEnd;
                    candidate.frameIndex = nativeRecord.frameIndex;
                    records.push_back(candidate);
                }
            }
        }
        return true;
    }

    bool RewriteWithoutHeroRecords(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        const std::vector<fable::game::entity::persistence::serialization::
            SavedEntityCellFrameView>& frames,
        const std::vector<ParsedRecord>& records,
        std::vector<std::uint8_t>& result)
    {
        if (cell == nullptr || cellBytes < CellPrefixBytes ||
            cellBytes > MaximumCellBytes)
        {
            return false;
        }
        std::size_t removedBytes = 0;
        for (const ParsedRecord& record : records)
        {
            if (record.framedBegin < CellPrefixBytes ||
                record.framedEnd <= record.framedBegin ||
                record.framedEnd > cellBytes ||
                record.end - record.begin > MaximumHeroRecordBytes ||
                removedBytes > std::numeric_limits<std::size_t>::max() -
                    (record.framedEnd - record.framedBegin))
            {
                return false;
            }
            removedBytes += record.framedEnd - record.framedBegin;
        }
        if (removedBytes > cellBytes - CellPrefixBytes)
        {
            return false;
        }
        try
        {
            result.clear();
            result.reserve(cellBytes - removedBytes);
            result.insert(result.end(), cell, cell + CellPrefixBytes);
            std::size_t recordIndex = 0;
            for (std::size_t frameIndex = 0;
                 frameIndex < frames.size();
                 ++frameIndex)
            {
                const auto& frame = frames[frameIndex];
                result.insert(
                    result.end(),
                    cell + frame.nameOffset,
                    cell + frame.lengthOffset);
                const std::size_t lengthOffset = result.size();
                result.resize(result.size() + sizeof(std::uint32_t));
                const std::size_t payloadBegin = result.size();
                std::size_t source = frame.payloadOffset;
                const std::size_t payloadEnd =
                    frame.payloadOffset + frame.payloadBytes;
                while (recordIndex < records.size() &&
                    records[recordIndex].frameIndex == frameIndex)
                {
                    const ParsedRecord& record = records[recordIndex++];
                    if (record.framedBegin < source ||
                        record.framedEnd > payloadEnd)
                    {
                        return false;
                    }
                    result.insert(
                        result.end(),
                        cell + source,
                        cell + record.framedBegin);
                    source = record.framedEnd;
                }
                result.insert(result.end(), cell + source, cell + payloadEnd);
                const std::size_t payloadBytes = result.size() - payloadBegin;
                if (payloadBytes >
                    (std::numeric_limits<std::uint32_t>::max)())
                {
                    return false;
                }
                const auto encodedLength = static_cast<std::uint32_t>(
                    payloadBytes);
                std::memcpy(
                    result.data() + lengthOffset,
                    &encodedLength,
                    sizeof(encodedLength));
            }
            if (recordIndex != records.size())
            {
                return false;
            }
        }
        catch (...)
        {
            result.clear();
            return false;
        }
        return result.size() == cellBytes - removedBytes;
    }

    bool HasValidHeroRecord(
        const std::uint8_t* const cell,
        const std::size_t cellBytes) noexcept
    {
        std::vector<ParsedRecord> records;
        std::vector<fable::game::entity::persistence::serialization::
            SavedEntityCellFrameView> frames;
        try
        {
            if (!CollectHeroRanges(cell, cellBytes, frames, records))
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
        return !records.empty();
    }
}

namespace fable::game::entity::persistence::serialization
{
    bool ExtractHeroSavedEntityRecord(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        HeroSavedEntityRecord& result)
    {
        result.Clear();
        if (cell == nullptr || cellBytes == 0 || cellBytes > MaximumCellBytes)
        {
            return false;
        }

        ParsedRecord parsed;
        std::vector<SavedEntityCellFrameView> frames;
        if (!FindExactlyOneHero(cell, cellBytes, parsed, &frames))
        {
            return false;
        }
        try
        {
            result.bytes.assign(
                cell + parsed.begin,
                cell + parsed.end);
            const SavedEntityCellFrameView& frame = frames[parsed.frameIndex];
            result.sectionName.assign(
                reinterpret_cast<const char*>(cell + frame.nameOffset),
                frame.nameBytes);
            result.uid = parsed.uid;
        }
        catch (...)
        {
            result.Clear();
            return false;
        }
        return !result.bytes.empty();
    }

    bool SpliceHeroIntoAuthoritativeCell(
        const std::uint8_t* const authoritativeCell,
        const std::size_t authoritativeCellBytes,
        const HeroSavedEntityRecord& guestHero,
        std::vector<std::uint8_t>& result)
    {
        result.clear();
        if (authoritativeCell == nullptr ||
            authoritativeCellBytes < CellPrefixBytes ||
            authoritativeCellBytes > MaximumCellBytes ||
            guestHero.bytes.empty() ||
            guestHero.bytes.size() > MaximumHeroRecordBytes ||
            guestHero.sectionName.empty() ||
            guestHero.sectionName.size() > MaximumNameBytes ||
            guestHero.sectionName.find('\0') != std::string::npos)
        {
            return false;
        }

        ParsedRecord guestParsed;
        if (!ParseHeroRecord(
                guestHero.bytes.data(),
                guestHero.bytes.size(),
                0,
                guestParsed) ||
            guestParsed.begin != 0 || guestParsed.end > guestHero.bytes.size() ||
            guestParsed.uid != guestHero.uid)
        {
            return false;
        }

        std::vector<ParsedRecord> hostHeroes;
        std::vector<SavedEntityCellFrameView> hostFrames;
        std::size_t expectedFrameIndex = 0;
        try
        {
            if (!CollectHeroRanges(
                    authoritativeCell,
                    authoritativeCellBytes,
                    hostFrames,
                    hostHeroes))
            {
                return false;
            }
            std::vector<std::uint8_t> hostWithoutHero;
            if (!RewriteWithoutHeroRecords(
                    authoritativeCell,
                    authoritativeCellBytes,
                    hostFrames,
                    hostHeroes,
                    hostWithoutHero))
            {
                return false;
            }
            std::vector<SavedEntityCellFrameView> cleanFrames;
            if (!ParseSavedEntityCell(
                    hostWithoutHero.data(),
                    hostWithoutHero.size(),
                    cleanFrames))
            {
                return false;
            }
            auto target = std::find_if(
                cleanFrames.begin(),
                cleanFrames.end(),
                [&](const SavedEntityCellFrameView& frame)
                {
                    return frame.nameBytes == guestHero.sectionName.size() &&
                        std::memcmp(
                            hostWithoutHero.data() + frame.nameOffset,
                            guestHero.sectionName.data(),
                            frame.nameBytes) == 0;
                });
            if (target == cleanFrames.end())
            {
                // An unvisited host map may have no cell/NULL section yet.
                // Add only the guest Hero's container; never copy the guest
                // source map's other sections, NPCs or quest-owned entities.
                constexpr std::size_t WordBytes = sizeof(std::uint32_t);
                const std::size_t extra = guestHero.sectionName.size() + 1 +
                    WordBytes + WordBytes;
                if (extra > MaximumCellBytes - hostWithoutHero.size()) return false;
                const auto oldSize = hostWithoutHero.size();
                hostWithoutHero.resize(oldSize + extra, 0);
                std::memcpy(hostWithoutHero.data() + oldSize,
                    guestHero.sectionName.c_str(), guestHero.sectionName.size() + 1);
                const std::uint32_t emptyStreamBytes = WordBytes;
                std::memcpy(hostWithoutHero.data() + oldSize +
                    guestHero.sectionName.size() + 1, &emptyStreamBytes, WordBytes);
                const auto frameCount = static_cast<std::uint32_t>(cleanFrames.size() + 1);
                std::memcpy(hostWithoutHero.data(), &frameCount, WordBytes);
                if (!ParseSavedEntityCell(hostWithoutHero.data(),
                        hostWithoutHero.size(), cleanFrames)) return false;
                target = cleanFrames.end() - 1;
            }
            const std::size_t retainedBytes = hostWithoutHero.size();
            constexpr std::size_t RecordLengthBytes = sizeof(std::uint32_t);
            if (guestHero.bytes.size() >
                    std::numeric_limits<std::size_t>::max() -
                        RecordLengthBytes ||
                guestHero.bytes.size() + RecordLengthBytes >
                    std::numeric_limits<std::size_t>::max() - retainedBytes ||
                retainedBytes + guestHero.bytes.size() + RecordLengthBytes >
                    MaximumCellBytes ||
                guestHero.bytes.size() >
                    (std::numeric_limits<std::uint32_t>::max)())
            {
                return false;
            }
            const std::size_t targetIndex = static_cast<std::size_t>(
                target - cleanFrames.begin());
            expectedFrameIndex = targetIndex;
            result.insert(
                result.end(),
                hostWithoutHero.data(),
                hostWithoutHero.data() + target->payloadOffset);
            const auto encodedRecordLength = static_cast<std::uint32_t>(
                guestHero.bytes.size());
            const auto* const encodedRecordLengthBytes =
                reinterpret_cast<const std::uint8_t*>(&encodedRecordLength);
            result.insert(
                result.end(),
                encodedRecordLengthBytes,
                encodedRecordLengthBytes + sizeof(encodedRecordLength));
            result.insert(
                result.end(),
                guestHero.bytes.begin(),
                guestHero.bytes.end());
            result.insert(
                result.end(),
                hostWithoutHero.data() + target->payloadOffset,
                hostWithoutHero.data() + hostWithoutHero.size());
            const std::size_t shiftedLengthOffset =
                cleanFrames[targetIndex].lengthOffset;
            const std::size_t payloadBytes =
                cleanFrames[targetIndex].payloadBytes + RecordLengthBytes +
                guestHero.bytes.size();
            const auto encodedLength = static_cast<std::uint32_t>(payloadBytes);
            std::memcpy(
                result.data() + shiftedLengthOffset,
                &encodedLength,
                sizeof(encodedLength));
        }
        catch (...)
        {
            result.clear();
            return false;
        }

        ParsedRecord finalHero;
        std::vector<SavedEntityCellFrameView> finalFrames;
        const bool valid = FindExactlyOneHero(
            result.data(),
            result.size(),
            finalHero,
            &finalFrames) &&
            finalHero.frameIndex == expectedFrameIndex &&
            finalHero.framedBegin ==
                finalFrames[expectedFrameIndex].payloadOffset &&
            finalHero.uid == guestHero.uid;
        if (!valid)
        {
            result.clear();
        }
        return valid;
    }

    bool RemoveHeroSavedEntityRecords(
        const std::uint8_t* const cell,
        const std::size_t cellBytes,
        std::vector<std::uint8_t>& result)
    {
        result.clear();
        if (cell == nullptr || cellBytes < CellPrefixBytes ||
            cellBytes > MaximumCellBytes)
        {
            return false;
        }
        try
        {
            std::vector<ParsedRecord> records;
            std::vector<SavedEntityCellFrameView> frames;
            if (!CollectHeroRanges(cell, cellBytes, frames, records) ||
                !RewriteWithoutHeroRecords(
                    cell,
                    cellBytes,
                    frames,
                    records,
                    result))
            {
                result.clear();
                return false;
            }
        }
        catch (...)
        {
            result.clear();
            return false;
        }
        return !HasValidHeroRecord(result.data(), result.size());
    }
}
