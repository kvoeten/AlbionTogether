#include "ShopSavedEntityRecord.h"

#include "SavedEntityCell.h"

#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace fable::game::entity::persistence::serialization;

    constexpr std::size_t MaximumCellBytes = 8 * 1024 * 1024;
    constexpr std::size_t MaximumEntityBytes = 2 * 1024 * 1024;
    constexpr std::size_t MaximumBaseBytes = 512 * 1024;
    constexpr std::size_t MaximumComponentBytes = 512 * 1024;
    constexpr std::size_t MaximumNameBytes = 128;
    constexpr std::size_t MaximumComponentCount = 1024;
    constexpr std::size_t MaximumRecords = 4096;
    constexpr std::size_t MaximumScopeBytes = 128;
    constexpr std::uint32_t ComponentSentinel = 0xFFFFFFFFu;
    constexpr std::uint32_t ComponentVersion = 0x2169u;

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
            return false;
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
            return false;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return true;
    }

    bool ReadName(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        std::string& value,
        std::size_t& next)
    {
        value.clear();
        next = offset;
        if (bytes == nullptr || offset >= size)
            return false;

        const std::size_t available = size - offset;
        const std::size_t limit = available < MaximumNameBytes + 1
            ? available
            : MaximumNameBytes + 1;
        std::size_t nameBytes = 0;
        while (nameBytes < limit && bytes[offset + nameBytes] != 0)
        {
            const std::uint8_t character = bytes[offset + nameBytes];
            if (character < 0x20 || character > 0x7E)
                return false;
            ++nameBytes;
        }
        if (nameBytes == 0 || nameBytes >= limit)
            return false;

        value.assign(
            reinterpret_cast<const char*>(bytes + offset), nameBytes);
        next = offset + nameBytes + 1;
        return true;
    }

    struct ComponentRange final
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::string name;
    };

    bool ParseComponent(
        const std::uint8_t* const bytes,
        const std::size_t size,
        const std::size_t offset,
        ComponentRange& result)
    {
        result = {};
        std::size_t cursor = offset;
        if (!ReadName(bytes, size, cursor, result.name, cursor) ||
            !CanRead(cursor, sizeof(std::uint32_t) + sizeof(std::uint8_t) +
                sizeof(std::uint32_t) * 3, size))
            return false;

        std::uint32_t pad = 0;
        std::uint32_t sentinel = 0;
        std::uint32_t version = 0;
        std::uint32_t dataBytes = 0;
        if (!ReadU32(bytes, size, cursor, pad) || pad != 0 ||
            !ReadU32(bytes, size, cursor + sizeof(std::uint32_t) +
                sizeof(std::uint8_t), sentinel) ||
            sentinel != ComponentSentinel ||
            !ReadU32(bytes, size, cursor + sizeof(std::uint32_t) +
                sizeof(std::uint8_t) + sizeof(std::uint32_t), version) ||
            version != ComponentVersion ||
            !ReadU32(bytes, size, cursor + sizeof(std::uint32_t) +
                sizeof(std::uint8_t) + sizeof(std::uint32_t) * 2,
                dataBytes) || dataBytes > MaximumComponentBytes)
            return false;

        constexpr std::size_t FixedAfterName =
            sizeof(std::uint32_t) + sizeof(std::uint8_t) +
            sizeof(std::uint32_t) * 3;
        const std::size_t dataOffset = cursor + FixedAfterName;
        if (!CanRead(dataOffset, dataBytes + sizeof(std::uint32_t), size))
            return false;

        std::uint32_t separator = 0;
        const std::size_t separatorOffset = dataOffset + dataBytes;
        if (!ReadU32(bytes, size, separatorOffset, separator) || separator != 0)
            return false;

        result.begin = offset;
        result.end = separatorOffset + sizeof(separator);
        return true;
    }

    struct ParsedEntity final
    {
        std::string recordType;
        std::string definitionName;
        std::uint64_t uid = 0;
        ComponentRange shop;
        bool hasShop = false;
    };

    bool ParseEntity(
        const std::uint8_t* const record,
        const std::size_t recordBytes,
        ParsedEntity& result)
    {
        result = {};
        if (record == nullptr || recordBytes == 0 ||
            recordBytes > MaximumEntityBytes)
            return false;

        std::size_t cursor = 0;
        if (!ReadName(record, recordBytes, cursor, result.recordType, cursor) ||
            !CanRead(cursor, sizeof(std::uint32_t) * 3 + sizeof(std::uint64_t) +
                sizeof(std::uint32_t), recordBytes))
            return false;

        // Current Anniversary entity header: three opaque words, UID, zero,
        // definition name, base length/base bytes, then zero/metadata/zero.
        cursor += sizeof(std::uint32_t) * 3;
        if (!ReadU64(record, recordBytes, cursor, result.uid) ||
            result.uid == 0)
            return false;
        cursor += sizeof(std::uint64_t);

        std::uint32_t zero = 0;
        if (!ReadU32(record, recordBytes, cursor, zero) || zero != 0)
            return false;
        cursor += sizeof(zero);

        if (!ReadName(record, recordBytes, cursor, result.definitionName, cursor) ||
            !CanRead(cursor, sizeof(std::uint32_t), recordBytes))
            return false;

        std::uint32_t baseBytes = 0;
        if (!ReadU32(record, recordBytes, cursor, baseBytes) ||
            baseBytes > MaximumBaseBytes)
            return false;
        cursor += sizeof(baseBytes);
        if (!CanRead(cursor, baseBytes + sizeof(std::uint32_t) * 3,
                recordBytes))
            return false;
        cursor += baseBytes;

        std::uint32_t trailer[3] = {};
        for (std::size_t index = 0; index != 3; ++index)
        {
            if (!ReadU32(record, recordBytes,
                    cursor + index * sizeof(std::uint32_t), trailer[index]))
                return false;
        }
        if (trailer[0] != 0 || trailer[2] != 0)
            return false;
        cursor += sizeof(trailer);

        for (std::size_t componentCount = 0;; ++componentCount)
        {
            if (componentCount >= MaximumComponentCount)
                return false;

            std::string componentName;
            std::size_t componentNameEnd = cursor;
            if (!ReadName(record, recordBytes, cursor, componentName,
                    componentNameEnd))
                return false;
            if (componentName == "END")
            {
                std::uint32_t endZero = 0;
                if (!ReadU32(record, recordBytes, componentNameEnd, endZero) ||
                    endZero != 0 ||
                    componentNameEnd + sizeof(endZero) > recordBytes)
                    return false;
                // Retail records may carry an opaque native tail after the
                // component terminator. It is intentionally not interpreted
                // or copied from the guest; the enclosing record length keeps
                // it host-owned during splice.
                return true;
            }

            ComponentRange component;
            if (!ParseComponent(record, recordBytes, cursor, component))
                return false;
            if (component.name == "CTCShop")
            {
                if (result.hasShop)
                    return false;
                result.shop = std::move(component);
                result.hasShop = true;
            }
            cursor = component.end;
        }
    }

    ShopSavedEntityIdentity IdentityFor(
        const ParsedEntity& entity,
        const std::uint32_t mapId,
        const std::string_view scope)
    {
        ShopSavedEntityIdentity identity;
        identity.mapId = mapId;
        identity.uid = entity.uid;
        identity.scope.assign(scope.data(), scope.size());
        identity.recordType = entity.recordType;
        identity.definitionName = entity.definitionName;
        return identity;
    }

    bool ExtractInternal(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        const std::uint32_t mapId,
        const std::string_view scope,
        std::vector<ShopSavedEntityRecord>& result) noexcept
    {
        result.clear();
        std::vector<ShopSavedEntityRecord> parsedResult;
        std::vector<SavedEntityCellFrameView> frames;
        std::vector<SavedEntityCellRecordView> records;
        if (!ParseSavedEntityCellRecords(bytes, byteCount, frames, records))
            return false;

        try
        {
            std::map<ShopSavedEntityIdentity, bool> seen;
            for (const SavedEntityCellRecordView& record : records)
            {
                ParsedEntity entity;
                if (!ParseEntity(bytes + record.recordOffset,
                        record.recordBytes, entity))
                    return false;
                if (!entity.hasShop)
                    continue;

                ShopSavedEntityRecord shop;
                shop.identity = IdentityFor(entity, mapId, scope);
                shop.componentBytes.assign(
                    bytes + record.recordOffset + entity.shop.begin,
                    bytes + record.recordOffset + entity.shop.end);
                if (parsedResult.size() >= MaximumRecords ||
                    !seen.emplace(shop.identity, true).second)
                    return false;
                parsedResult.push_back(std::move(shop));
            }
            result = std::move(parsedResult);
            return true;
        }
        catch (...)
        {
            result.clear();
            return false;
        }
    }

    bool IsValidLocalRecord(
        const ShopSavedEntityRecord& record,
        const std::uint32_t mapId,
        const std::string_view scope)
    {
        if (record.identity.mapId != mapId || record.identity.uid == 0 ||
            record.identity.scope != scope || record.identity.recordType.empty() ||
            record.identity.definitionName.empty() || record.componentBytes.empty())
            return false;

        ComponentRange component;
        return ParseComponent(record.componentBytes.data(),
            record.componentBytes.size(), 0, component) &&
            component.name == "CTCShop" &&
            component.end == record.componentBytes.size();
    }
}

namespace fable::game::entity::persistence::serialization
{
    bool ExtractShopSavedEntityRecords(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        const std::uint32_t mapId,
        const std::string_view scope,
        std::vector<ShopSavedEntityRecord>& result) noexcept
    {
        if (bytes == nullptr || byteCount == 0 || byteCount > MaximumCellBytes ||
            mapId == 0 || scope.empty() || scope.size() > MaximumScopeBytes)
        {
            result.clear();
            return false;
        }
        return ExtractInternal(bytes, byteCount, mapId, scope, result);
    }

    bool SpliceShopSavedEntityRecords(
        const std::uint8_t* const hostBytes,
        const std::size_t hostByteCount,
        const std::uint32_t mapId,
        const std::string_view scope,
        const std::vector<ShopSavedEntityRecord>& localRecords,
        std::vector<std::uint8_t>& result) noexcept
    {
        result.clear();
        if (hostBytes == nullptr || hostByteCount == 0 ||
            hostByteCount > MaximumCellBytes || mapId == 0 || scope.empty() ||
            scope.size() > MaximumScopeBytes || localRecords.size() > MaximumRecords)
            return false;

        try
        {
            std::map<ShopSavedEntityIdentity, const ShopSavedEntityRecord*> local;
            for (const ShopSavedEntityRecord& record : localRecords)
            {
                if (!IsValidLocalRecord(record, mapId, scope) ||
                    !local.emplace(record.identity, &record).second)
                    return false;
            }

            std::vector<SavedEntityCellFrameView> frames;
            std::vector<SavedEntityCellRecordView> records;
            if (!ParseSavedEntityCellRecords(hostBytes, hostByteCount,
                    frames, records))
                return false;

            struct Replacement final
            {
                SavedEntityCellRecordView record;
                ComponentRange component;
                const ShopSavedEntityRecord* local = nullptr;
            };
            std::map<std::size_t, Replacement> replacements;
            for (const SavedEntityCellRecordView& record : records)
            {
                ParsedEntity entity;
                if (!ParseEntity(hostBytes + record.recordOffset,
                        record.recordBytes, entity))
                    return false;
                if (!entity.hasShop)
                    continue;

                const ShopSavedEntityIdentity identity =
                    IdentityFor(entity, mapId, scope);
                const auto found = local.find(identity);
                if (found == local.end())
                    continue;
                replacements.emplace(record.lengthOffset,
                    Replacement{record, entity.shop, found->second});
            }

            std::size_t rewrittenBytes = hostByteCount;
            for (const auto& entry : replacements)
            {
                const Replacement& replacement = entry.second;
                const std::size_t oldComponentBytes =
                    replacement.component.end - replacement.component.begin;
                const std::size_t newComponentBytes =
                    replacement.local->componentBytes.size();
                if (newComponentBytes >= oldComponentBytes)
                {
                    const std::size_t delta = newComponentBytes -
                        oldComponentBytes;
                    if (delta > MaximumCellBytes ||
                        rewrittenBytes > MaximumCellBytes - delta)
                        return false;
                    rewrittenBytes += delta;
                }
                else
                {
                    const std::size_t delta = oldComponentBytes -
                        newComponentBytes;
                    if (rewrittenBytes < delta)
                        return false;
                    rewrittenBytes -= delta;
                }
            }

            std::vector<std::uint8_t> rewritten;
            rewritten.reserve(rewrittenBytes);
            std::size_t sourceCursor = 0;
            if (!frames.empty())
            {
                rewritten.insert(rewritten.end(), hostBytes,
                    hostBytes + frames.front().nameOffset);
                sourceCursor = frames.front().nameOffset;
            }
            for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
            {
                const SavedEntityCellFrameView& frame = frames[frameIndex];
                if (sourceCursor != frame.nameOffset)
                    return false;
                rewritten.insert(rewritten.end(), hostBytes + frame.nameOffset,
                    hostBytes + frame.lengthOffset);
                const std::size_t outputLengthOffset = rewritten.size();
                rewritten.resize(rewritten.size() + sizeof(std::uint32_t));
                const std::size_t outputPayloadOffset = rewritten.size();

                std::size_t payloadSource = frame.payloadOffset;
                const std::size_t payloadEnd = frame.payloadOffset +
                    frame.payloadBytes;
                for (const auto& entry : replacements)
                {
                    const Replacement& replacement = entry.second;
                    if (replacement.record.frameIndex != frameIndex)
                        continue;
                    const std::size_t recordEnd = replacement.record.recordOffset +
                        replacement.record.recordBytes;
                    const std::size_t componentBegin = replacement.record.recordOffset +
                        replacement.component.begin;
                    const std::size_t componentEnd = replacement.record.recordOffset +
                        replacement.component.end;
                    if (replacement.record.lengthOffset < payloadSource ||
                        recordEnd > payloadEnd || componentBegin < replacement.record.recordOffset ||
                        componentEnd > recordEnd)
                        return false;

                    rewritten.insert(rewritten.end(), hostBytes + payloadSource,
                        hostBytes + replacement.record.lengthOffset);
                    const std::size_t newRecordBytes = replacement.record.recordBytes -
                        (componentEnd - componentBegin) +
                        replacement.local->componentBytes.size();
                    if (newRecordBytes > MaximumEntityBytes ||
                        newRecordBytes > (std::numeric_limits<std::uint32_t>::max)())
                        return false;
                    const std::uint32_t encodedRecordBytes =
                        static_cast<std::uint32_t>(newRecordBytes);
                    const auto* encoded = reinterpret_cast<const std::uint8_t*>(
                        &encodedRecordBytes);
                    rewritten.insert(rewritten.end(), encoded,
                        encoded + sizeof(encodedRecordBytes));
                    rewritten.insert(rewritten.end(), hostBytes + replacement.record.recordOffset,
                        hostBytes + componentBegin);
                    rewritten.insert(rewritten.end(), replacement.local->componentBytes.begin(),
                        replacement.local->componentBytes.end());
                    rewritten.insert(rewritten.end(), hostBytes + componentEnd,
                        hostBytes + recordEnd);
                    payloadSource = recordEnd;
                }
                rewritten.insert(rewritten.end(), hostBytes + payloadSource,
                    hostBytes + payloadEnd);
                const std::size_t newPayloadBytes = rewritten.size() - outputPayloadOffset;
                if (newPayloadBytes > (std::numeric_limits<std::uint32_t>::max)())
                    return false;
                const std::uint32_t encodedPayloadBytes =
                    static_cast<std::uint32_t>(newPayloadBytes);
                std::memcpy(rewritten.data() + outputLengthOffset,
                    &encodedPayloadBytes, sizeof(encodedPayloadBytes));
                sourceCursor = payloadEnd;
            }
            rewritten.insert(rewritten.end(), hostBytes + sourceCursor,
                hostBytes + hostByteCount);
            if (rewritten.size() != rewrittenBytes)
                return false;
            result = std::move(rewritten);
            return true;
        }
        catch (...)
        {
            result.clear();
            return false;
        }
    }
}
