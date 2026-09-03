#include "Game/Entity/Persistence/Serialization/ShopSavedEntityRecord.h"

#define private public
#include "Multiplayer/Persistence/LocalShopSaveBoundary.h"
#undef private

void BindSavedEntityTestCompression(
    fable::game::entity::persistence::native::SavedEntityCompression&);

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using fable::game::entity::persistence::serialization::
        ExtractShopSavedEntityRecords;
    using fable::game::entity::persistence::serialization::
        ShopSavedEntityRecord;
    using fable::game::entity::persistence::serialization::
        SpliceShopSavedEntityRecords;
    using fable::game::entity::persistence::SavedEntityMapBlobFormat;
    using fable::game::entity::persistence::SavedEntityMapBlobSnapshot;
    using fable::multiplayer::persistence::LocalShopSaveBoundary;
    using fable::multiplayer::persistence::SavedEntityCollectionRecord;

    std::uint64_t HashBytes(
        const std::vector<std::uint8_t>& bytes) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const std::uint8_t byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }


    void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (unsigned int i = 0; i != 4; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
            value >>= 8;
        }
    }

    void U64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
    {
        for (unsigned int i = 0; i != 8; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
            value >>= 8;
        }
    }

    void Text(std::vector<std::uint8_t>& bytes, const char* value)
    {
        bytes.insert(bytes.end(), value, value + std::strlen(value));
        bytes.push_back(0);
    }

    void Component(
        std::vector<std::uint8_t>& bytes,
        const char* name,
        const std::string& payload)
    {
        Text(bytes, name);
        U32(bytes, 0);
        bytes.push_back(0);
        U32(bytes, 0xFFFFFFFFu);
        U32(bytes, 0x2169u);
        U32(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        U32(bytes, 0);
    }

    std::vector<std::uint8_t> Cell(
        const std::uint64_t uid,
        const char* script,
        const char* definition,
        const std::string& shopPayload,
        const std::uint8_t tailByte)
    {
        std::vector<std::uint8_t> record;
        Text(record, script);
        U32(record, 0); // opaque header word
        U32(record, 4); // opaque header word
        U32(record, 0); // opaque header word
        U64(record, uid);
        U32(record, 0);
        Text(record, definition);
        U32(record, 0); // base field bytes
        U32(record, 0); // pre-component zero
        U32(record, 1); // opaque metadata
        U32(record, 0); // pre-component zero
        Component(record, "CTCOther", "host-reference");
        Component(record, "CTCShop", shopPayload);
        Component(record, "CTCOtherTail", "host-tail");
        Text(record, "END");
        U32(record, 0);
        // Retail records retain opaque native metadata after END + zero.
        record.insert(record.end(), 28, tailByte);

        std::vector<std::uint8_t> cell;
        U32(cell, 1);
        Text(cell, "NULL");
        U32(cell, static_cast<std::uint32_t>(record.size() + 8));
        U32(cell, static_cast<std::uint32_t>(record.size()));
        cell.insert(cell.end(), record.begin(), record.end());
        U32(cell, 0);
        return cell;
    }

    SavedEntityCollectionRecord CollectionRecord(
        const std::uint32_t mapId,
        const std::vector<std::uint8_t>& bytes)
    {
        SavedEntityCollectionRecord record;
        record.format = SavedEntityMapBlobFormat::Binary;
        record.mapId = static_cast<std::uint16_t>(mapId);
        record.metadata = static_cast<std::uint32_t>(bytes.size());
        record.hash = HashBytes(bytes);
        record.bytes = bytes;
        return record;
    }

    SavedEntityMapBlobSnapshot Snapshot(
        const std::uint32_t mapId,
        const std::vector<std::uint8_t>& bytes)
    {
        SavedEntityMapBlobSnapshot snapshot;
        snapshot.format = SavedEntityMapBlobFormat::Binary;
        snapshot.mapId = mapId;
        snapshot.bytes = bytes.data();
        snapshot.byteCount = bytes.size();
        snapshot.metadata = static_cast<std::uint32_t>(bytes.size());
        snapshot.hash = HashBytes(bytes);
        return snapshot;
    }

    bool HasBytes(
        const std::vector<std::uint8_t>& bytes,
        const char* text)
    {
        const auto* begin = bytes.data();
        const auto* end = begin + bytes.size();
        const auto* needle = reinterpret_cast<const std::uint8_t*>(text);
        const auto* needleEnd = needle + std::strlen(text);
        return std::search(begin, end, needle, needleEnd) != end;
    }

    bool HasByteRun(
        const std::vector<std::uint8_t>& bytes,
        const std::uint8_t value,
        const std::size_t count)
    {
        for (std::size_t offset = 0; offset + count <= bytes.size(); ++offset)
        {
            if (std::all_of(bytes.begin() + offset,
                    bytes.begin() + offset + count,
                    [value](const std::uint8_t byte) { return byte == value; }))
                return true;
        }
        return false;
    }

    std::vector<std::uint8_t> WithMalformedLateRecord(
        const std::vector<std::uint8_t>& source)
    {
        std::vector<std::uint8_t> result = source;
        std::vector<std::uint8_t> malformed;
        Text(malformed, "MalformedLateRecord");
        const std::size_t terminator = result.size() - sizeof(std::uint32_t);
        std::vector<std::uint8_t> malformedFramed;
        U32(malformedFramed,
            static_cast<std::uint32_t>(malformed.size()));
        malformedFramed.insert(malformedFramed.end(), malformed.begin(),
            malformed.end());
        result.insert(result.begin() + terminator, malformedFramed.begin(),
            malformedFramed.end());
        const std::size_t frameLengthOffset =
            sizeof(std::uint32_t) + std::strlen("NULL") + 1;
        std::uint32_t frameBytes = 0;
        std::memcpy(&frameBytes, result.data() + frameLengthOffset,
            sizeof(frameBytes));
        frameBytes += static_cast<std::uint32_t>(malformedFramed.size());
        std::memcpy(result.data() + frameLengthOffset, &frameBytes,
            sizeof(frameBytes));
        return result;
    }

    std::vector<std::uint8_t> OversizedComponent()
    {
        std::vector<std::uint8_t> result;
        Text(result, "CTCShop");
        U32(result, 0);
        result.push_back(0);
        U32(result, 0xFFFFFFFFu);
        U32(result, 0x2169u);
        U32(result, 512 * 1024 + 1);
        U32(result, 0);
        return result;
    }
}

int RunLocalShopEconomyTests()
{
    int failures = 0;
    const auto expect = [&failures](const bool value, const char* description)
    {
        if (!value)
        {
            std::fprintf(stderr, "Local shop economy: %s\n", description);
            ++failures;
        }
    };

    constexpr std::uint32_t mapId = 339;
    constexpr std::uint64_t merchantUid = 0x1122334455667788ull;
    const auto host = Cell(
        merchantUid, "MerchantScript", "BUILDING_GUILD_SHOP_01", "host", 0xA5);
    const auto local = Cell(
        merchantUid, "MerchantScript", "BUILDING_GUILD_SHOP_01",
        "local-private-stock-with-a-different-size", 0xB6);

    std::vector<ShopSavedEntityRecord> captured;
    expect(
        ExtractShopSavedEntityRecords(
            local.data(), local.size(), mapId, "campaign-A/save-2", captured),
        "current CTCShop frame was not extracted");
    expect(captured.size() == 1, "capture did not return exactly one merchant");
    if (captured.size() == 1)
    {
        expect(captured[0].identity.mapId == mapId, "map identity was not retained");
        expect(captured[0].identity.uid == merchantUid, "UID identity was not retained");
        expect(captured[0].identity.scope == "campaign-A/save-2", "scope was not retained");
        expect(captured[0].identity.recordType == "MerchantScript",
            "record/class identity was not retained");
        expect(captured[0].identity.definitionName == "BUILDING_GUILD_SHOP_01",
            "definition identity was not retained");
        expect(captured[0].componentBytes.size() > 0, "shop frame was empty");
    }

    std::vector<std::uint8_t> overlaid;
    expect(
        SpliceShopSavedEntityRecords(
            host.data(), host.size(), mapId, "campaign-A/save-2", captured,
            overlaid),
        "host CTCShop splice failed");
    expect(overlaid != host, "matching private stock made no change");
    expect(HasBytes(overlaid, "local-private-stock-with-a-different-size"),
        "private CTCShop bytes were not installed");
    expect(HasBytes(overlaid, "host-reference"),
        "host non-shop component was changed");
    expect(HasBytes(overlaid, "host-tail"),
        "host trailing component was changed");
    expect(HasByteRun(overlaid, 0xA5, 28),
        "opaque host record trailer was changed");
    expect(!HasByteRun(overlaid, 0xB6, 28),
        "opaque guest record trailer was copied");

    std::vector<ShopSavedEntityRecord> overlaidRecords;
    expect(
        ExtractShopSavedEntityRecords(
            overlaid.data(), overlaid.size(), mapId, "campaign-A/save-2",
            overlaidRecords),
        "spliced outer/entity lengths did not round-trip");
    expect(overlaidRecords.size() == 1 &&
            overlaidRecords[0].componentBytes == captured[0].componentBytes,
        "spliced CTCShop frame did not round-trip");

    const auto otherHost = Cell(
        merchantUid + 1, "MerchantScript", "BUILDING_GUILD_SHOP_01", "other", 0xA5);
    std::vector<std::uint8_t> untouched;
    expect(
        SpliceShopSavedEntityRecords(
            otherHost.data(), otherHost.size(), mapId, "campaign-A/save-2",
            captured, untouched),
        "nonmatching host splice rejected the cell");
    expect(untouched == otherHost, "local-only merchant was inserted into host");

    std::vector<std::uint8_t> wrongScope;
    expect(
        !SpliceShopSavedEntityRecords(
            host.data(), host.size(), mapId, "campaign-B/save-2", captured,
            wrongScope),
        "foreign selected-save batch was accepted");
    expect(wrongScope.empty(), "scope rejection exposed projected private stock");

    const auto malformedLate = WithMalformedLateRecord(local);
    std::vector<ShopSavedEntityRecord> failedCapture{
        captured.empty() ? ShopSavedEntityRecord{} : captured.front()};
    expect(
        !ExtractShopSavedEntityRecords(
            malformedLate.data(), malformedLate.size(), mapId,
            "campaign-A/save-2", failedCapture),
        "late malformed entity was accepted");
    expect(failedCapture.empty(),
        "failed capture exposed partially extracted merchants");

    std::vector<std::uint8_t> failedOverlay{0xA5};
    expect(
        !SpliceShopSavedEntityRecords(
            malformedLate.data(), malformedLate.size(), mapId,
            "campaign-A/save-2", captured, failedOverlay),
        "late malformed host entity was accepted");
    expect(failedOverlay.empty(),
        "failed splice exposed a partial rewritten cell");

    if (!captured.empty())
    {
        const auto validComponent = captured.front().componentBytes;
        captured.front().componentBytes = OversizedComponent();
        std::vector<std::uint8_t> oversizedOutput{0xA5};
        expect(
            !SpliceShopSavedEntityRecords(
                host.data(), host.size(), mapId, "campaign-A/save-2",
                captured, oversizedOutput),
            "oversized shop replacement was accepted");
        expect(oversizedOutput.empty(),
            "oversized replacement exposed partial output");
        captured.front().componentBytes = validComponent;
    }

    std::vector<std::uint8_t> oversizedHost(8 * 1024 * 1024 + 1, 0);
    std::vector<std::uint8_t> oversizedHostOutput{0xA5};
    expect(
        !SpliceShopSavedEntityRecords(
            oversizedHost.data(), oversizedHost.size(), mapId,
            "campaign-A/save-2", captured, oversizedHostOutput),
        "oversized host cell was accepted");
    expect(oversizedHostOutput.empty(),
        "oversized host rejection exposed output");

    // Exercise the production save boundary with the same identity codecs
    // used by SavedEntityMapBaseline.Tests.cpp. Compression remains a fake
    // copy here; the boundary policy and native-cell splice are real.
    LocalShopSaveBoundary boundary;
    BindSavedEntityTestCompression(boundary.compression_);
    const auto initialRevision = boundary.Revision();
    boundary.BeginGuestCollection();
    const auto firstCollectionRevision = boundary.Revision();
    expect(firstCollectionRevision != initialRevision,
        "selected-save begin did not advance the collection revision");

    const auto localChanged = Cell(
        merchantUid, "MerchantScript", "BUILDING_GUILD_SHOP_01",
        "local-changed-stock", 0xB7);
    const auto localInitial = Cell(
        merchantUid, "MerchantScript", "BUILDING_GUILD_SHOP_01",
        "local-initial-stock", 0xB6);
    const auto localMapTwo = Cell(
        merchantUid + 1, "MerchantScript", "BUILDING_GUILD_SHOP_01",
        "local-map-two-stock", 0xB8);
    const auto initialSnapshot = Snapshot(mapId, localInitial);
    boundary.ObserveGuestRecord(initialSnapshot);
    const auto afterFirstCapture = boundary.Revision();
    expect(afterFirstCapture != firstCollectionRevision,
        "initial native shop cell was not captured");
    const auto localSnapshot = Snapshot(mapId, localChanged);
    boundary.ObserveGuestRecord(localSnapshot);
    const auto afterChangedCapture = boundary.Revision();
    expect(afterChangedCapture != afterFirstCapture,
        "changed native shop cell did not replace its saved payload");
    boundary.ObserveGuestRecord(localSnapshot);
    expect(boundary.Revision() == afterChangedCapture,
        "identical native shop capture bumped revision");
    boundary.ObserveGuestRecord(Snapshot(mapId + 1, localMapTwo));
    const auto afterSecondMap = boundary.Revision();
    expect(afterSecondMap != afterChangedCapture,
        "independent map shop capture did not advance revision");

    const auto hostMapOne = Cell(
        merchantUid, "MerchantScript", "BUILDING_GUILD_SHOP_01", "host-one", 0xA5);
    const auto hostMapTwo = Cell(
        merchantUid + 1, "MerchantScript", "BUILDING_GUILD_SHOP_01", "host-two", 0xA4);
    auto projectedMapOne = CollectionRecord(mapId, hostMapOne);
    auto projectedMapTwo = CollectionRecord(mapId + 1, hostMapTwo);
    expect(boundary.RewriteHostRecord(projectedMapOne),
        "production map-one host projection failed");
    expect(boundary.RewriteHostRecord(projectedMapTwo),
        "production map-two host projection failed");
    expect(HasBytes(projectedMapOne.bytes, "local-changed-stock") &&
            HasBytes(projectedMapOne.bytes, "host-reference") &&
            HasBytes(projectedMapOne.bytes, "host-tail") &&
            HasByteRun(projectedMapOne.bytes, 0xA5, 28),
        "map-one projection did not preserve host-owned bytes");
    expect(HasBytes(projectedMapTwo.bytes, "local-map-two-stock") &&
            HasByteRun(projectedMapTwo.bytes, 0xA4, 28),
        "map-two projection was not independent");

    // A malformed later observation must not discard the last valid map-one
    // payload, while an absent local merchant leaves the host cell intact.
    const auto malformedCapture = WithMalformedLateRecord(localChanged);
    boundary.ObserveGuestRecord(Snapshot(mapId, malformedCapture));
    auto retainedMapOne = CollectionRecord(mapId, hostMapOne);
    expect(boundary.RewriteHostRecord(retainedMapOne),
        "projection after malformed capture failed");
    expect(HasBytes(retainedMapOne.bytes, "local-changed-stock"),
        "malformed capture discarded previous valid shop payload");
    auto missingMap = CollectionRecord(mapId + 2, hostMapOne);
    const auto missingMapBefore = missingMap.bytes;
    expect(boundary.RewriteHostRecord(missingMap),
        "missing local merchant rejected host preservation");
    expect(missingMap.bytes == missingMapBefore,
        "missing local merchant changed host bytes");

    auto invalidProjection = CollectionRecord(mapId, malformedCapture);
    const auto invalidProjectionBefore = invalidProjection.bytes;
    expect(!boundary.RewriteHostRecord(invalidProjection),
        "malformed host projection was accepted");
    expect(invalidProjection.bytes == invalidProjectionBefore,
        "invalid projection changed the input record");

    // Starting another selected save fences the previous cache. Capture the
    // new save explicitly before allowing its private stock to project.
    boundary.BeginGuestCollection();
    auto isolatedHost = CollectionRecord(mapId, hostMapOne);
    const auto isolatedHostBefore = isolatedHost.bytes;
    expect(boundary.RewriteHostRecord(isolatedHost),
        "empty selected-save cache rejected host preservation");
    expect(isolatedHost.bytes == isolatedHostBefore,
        "previous save shop payload crossed the selected-save fence");
    boundary.ObserveGuestRecord(Snapshot(mapId, localChanged));
    expect(boundary.RewriteHostRecord(isolatedHost),
        "new selected-save shop capture did not project");
    expect(HasBytes(isolatedHost.bytes, "local-changed-stock"),
        "new selected-save shop payload was not projected");

    return failures;
}
