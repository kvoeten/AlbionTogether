#include "Multiplayer/Protocol/SavedEntityMapBaselineMessage.h"

#define private public
#include "Multiplayer/Persistence/GuestHeroSaveBoundary.h"
#undef private

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace
{
    constexpr std::uint64_t EmptyHash = 14695981039346656037ull;

    std::uint64_t HashBytes(
        const std::uint8_t* bytes,
        const std::size_t count) noexcept
    {
        std::uint64_t hash = EmptyHash;
        for (std::size_t index = 0; index < count; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool SameHeaderFields(
        const fable::multiplayer::protocol::SavedEntityMapBaselineMessage& left,
        const fable::multiplayer::protocol::SavedEntityMapBaselineMessage& right)
    {
        return left.operation == right.operation &&
            left.format == right.format && left.present == right.present &&
            left.mapId == right.mapId && left.transferId == right.transferId &&
            left.baselineRevision == right.baselineRevision &&
            left.metadata == right.metadata &&
            left.totalBytes == right.totalBytes && left.offset == right.offset &&
            left.hash == right.hash && left.chunkSize == right.chunkSize;
    }

    // The boundary is deliberately tested without loading the game module.
    // These identity codecs model the native zlib contract closely enough to
    // exercise the source-record policy while keeping this test deterministic.
    struct FakeZStream final
    {
        const std::uint8_t* nextIn = nullptr;
        unsigned int availableIn = 0;
        unsigned long totalIn = 0;
        std::uint8_t* nextOut = nullptr;
        unsigned int availableOut = 0;
        unsigned long totalOut = 0;
        const char* message = nullptr;
        void* state = nullptr;
        void* allocate = nullptr;
        void* release = nullptr;
        void* opaque = nullptr;
        int dataType = 0;
        unsigned long adler = 0;
        unsigned long reserved = 0;
    };

    int __cdecl FakeInflateInit(void*, const char*, int)
    {
        return 0;
    }

    int __cdecl FakeInflate(void* const rawStream, int)
    {
        auto* const stream = static_cast<FakeZStream*>(rawStream);
        const unsigned int inputBytes = stream->availableIn;
        if (stream->nextIn == nullptr || stream->nextOut == nullptr ||
            inputBytes > stream->availableOut)
        {
            return -5;
        }
        std::memcpy(stream->nextOut, stream->nextIn, inputBytes);
        stream->availableIn = 0;
        stream->availableOut -= inputBytes;
        stream->totalIn = inputBytes;
        stream->totalOut = inputBytes;
        return 1;
    }

    int __cdecl FakeInflateEnd(void*)
    {
        return 0;
    }

    unsigned long __cdecl FakeCompressBound(const unsigned long bytes)
    {
        return bytes + 64;
    }

    int __cdecl FakeCompress2(
        std::uint8_t* const output,
        unsigned long* const outputBytes,
        const std::uint8_t* const input,
        const unsigned long inputBytes,
        int)
    {
        if (output == nullptr || outputBytes == nullptr || input == nullptr ||
            *outputBytes < inputBytes)
        {
            return -5;
        }
        std::memcpy(output, input, inputBytes);
        *outputBytes = inputBytes;
        return 0;
    }

    void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        const auto* const source = reinterpret_cast<const std::uint8_t*>(
            &value);
        bytes.insert(bytes.end(), source, source + sizeof(value));
    }

    void AppendU64(std::vector<std::uint8_t>& bytes, const std::uint64_t value)
    {
        const auto* const source = reinterpret_cast<const std::uint8_t*>(
            &value);
        bytes.insert(bytes.end(), source, source + sizeof(value));
    }

    void AppendName(
        std::vector<std::uint8_t>& bytes,
        const char* const name)
    {
        const std::size_t length = std::strlen(name);
        bytes.insert(
            bytes.end(),
            reinterpret_cast<const std::uint8_t*>(name),
            reinterpret_cast<const std::uint8_t*>(name) + length + 1);
    }

    std::vector<std::uint8_t> MakeHero(const std::uint64_t uid)
    {
        std::vector<std::uint8_t> result;
        AppendName(result, "PlayerCreature");
        AppendU32(result, 1);
        AppendU32(result, 2);
        AppendU32(result, 3);
        AppendU64(result, uid);
        AppendU32(result, 0);
        AppendName(result, "CREATURE_HERO");
        AppendU32(result, 4);
        result.insert(result.end(), {0xA1, 0xB2, 0xC3, 0xD4});
        AppendU32(result, 0);
        AppendU32(result, 336);
        AppendU32(result, 0);
        AppendName(result, "CTCHeroStats");
        AppendU32(result, 0);
        result.push_back(0);
        AppendU32(result, 0xFFFFFFFFu);
        AppendU32(result, 0x2169u);
        AppendU32(result, 4);
        AppendU32(result, 0x12345678u);
        AppendU32(result, 0);
        AppendName(result, "CTCInventory");
        AppendU32(result, 0);
        result.push_back(0);
        AppendU32(result, 0xFFFFFFFFu);
        AppendU32(result, 0x2169u);
        AppendU32(result, 3);
        result.insert(result.end(), {0x01, 0x02, 0x03});
        AppendU32(result, 0);
        AppendName(result, "END");
        AppendU32(result, 0);
        return result;
    }

    std::vector<std::uint8_t> MakeCell(
        const char* const sectionName,
        const std::vector<std::vector<std::uint8_t>>& records)
    {
        std::vector<std::uint8_t> result;
        AppendU32(result, 1);
        AppendName(result, sectionName);
        std::size_t payloadBytes = sizeof(std::uint32_t);
        for (const auto& record : records)
        {
            payloadBytes += sizeof(std::uint32_t) + record.size();
        }
        AppendU32(result, static_cast<std::uint32_t>(payloadBytes));
        for (const auto& record : records)
        {
            AppendU32(result, static_cast<std::uint32_t>(record.size()));
            result.insert(result.end(), record.begin(), record.end());
        }
        AppendU32(result, 0);
        return result;
    }

    bool ContainsHero(
        const fable::multiplayer::persistence::SavedEntityCollectionRecord& record)
    {
        using fable::game::entity::persistence::serialization::
            ExtractHeroSavedEntityRecord;
        using fable::game::entity::persistence::serialization::
            HeroSavedEntityRecord;
        if (record.bytes.size() < 4 || record.metadata < 4)
        {
            return false;
        }
        HeroSavedEntityRecord hero;
        return ExtractHeroSavedEntityRecord(
            record.bytes.data(),
            record.bytes.size(),
            hero);
    }
}

int RunSavedEntityMapBaselineTests()
{
    using namespace fable::multiplayer::protocol;
    int failures = 0;
    const auto check = [&failures](const bool condition)
    {
        if (!condition)
        {
            ++failures;
        }
    };

    const std::array<std::uint8_t, 5> bytes = {1, 7, 3, 9, 2};
    SavedEntityMapBaselineMessage begin;
    begin.operation = SavedEntityMapBaselineOperation::Begin;
    begin.format = fable::game::entity::persistence::
        SavedEntityMapBlobFormat::Binary;
    begin.present = true;
    begin.mapId = 17;
    begin.transferId = 41;
    begin.baselineRevision = 93;
    begin.metadata = 12;
    begin.totalBytes = static_cast<std::uint32_t>(bytes.size());
    begin.hash = HashBytes(bytes.data(), bytes.size());

    std::array<std::uint8_t, 1'200> encoded = {};
    std::size_t encodedSize = 0;
    check(EncodeSavedEntityMapBaselineMessage(
        begin, encoded.data(), encoded.size(), encodedSize));
    SavedEntityMapBaselineMessage decoded;
    check(DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));
    check(SameHeaderFields(begin, decoded));
    check(decoded.chunk == nullptr);

    SavedEntityMapBaselineMessage chunk = begin;
    chunk.operation = SavedEntityMapBaselineOperation::Chunk;
    chunk.chunk = bytes.data();
    chunk.chunkSize = bytes.size();
    check(EncodeSavedEntityMapBaselineMessage(
        chunk, encoded.data(), encoded.size(), encodedSize));
    check(DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));
    check(SameHeaderFields(chunk, decoded));
    check(decoded.chunk != nullptr &&
        std::memcmp(decoded.chunk, bytes.data(), bytes.size()) == 0);

    SavedEntityMapBaselineMessage commit = begin;
    commit.operation = SavedEntityMapBaselineOperation::Commit;
    commit.offset = commit.totalBytes;
    check(EncodeSavedEntityMapBaselineMessage(
        commit, encoded.data(), encoded.size(), encodedSize));
    check(DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));
    check(SameHeaderFields(commit, decoded));

    // An absent host record is an explicit empty authoritative map record,
    // never permission to retain the guest's local map.
    SavedEntityMapBaselineMessage absent = {};
    absent.operation = SavedEntityMapBaselineOperation::Begin;
    absent.mapId = 17;
    absent.transferId = 42;
    absent.baselineRevision = 94;
    absent.hash = EmptyHash;
    check(EncodeSavedEntityMapBaselineMessage(
        absent, encoded.data(), encoded.size(), encodedSize));
    check(DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));
    check(SameHeaderFields(absent, decoded));

    SavedEntityMapBaselineMessage invalidAbsent = absent;
    invalidAbsent.metadata = 1;
    check(!EncodeSavedEntityMapBaselineMessage(
        invalidAbsent, encoded.data(), encoded.size(), encodedSize));
    check(EncodeSavedEntityMapBaselineMessage(
        absent, encoded.data(), encoded.size(), encodedSize));
    encoded[3] = 1;
    check(!DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));

    // Collection fences carry a single capture revision and an exact
    // populated-record count; only the commit may release guest construction.
    SavedEntityMapBaselineMessage collectionBegin = {};
    collectionBegin.operation = SavedEntityMapBaselineOperation::CollectionBegin;
    collectionBegin.format = fable::game::entity::persistence::
        SavedEntityMapBlobFormat::Binary;
    collectionBegin.present = true;
    collectionBegin.collection = true;
    collectionBegin.collectionRecordCount = 2;
    collectionBegin.transferId = 51;
    collectionBegin.baselineRevision = 100;
    collectionBegin.hash = EmptyHash;
    check(EncodeSavedEntityMapBaselineMessage(
        collectionBegin, encoded.data(), encoded.size(), encodedSize));
    check(DecodeSavedEntityMapBaselineMessage(
        encoded.data(), encodedSize, decoded));
    check(decoded.collection && decoded.collectionRecordCount == 2 &&
        decoded.collectionRecordIndex == 0);
    SavedEntityMapBaselineMessage collectionRecord = begin;
    collectionRecord.collection = true;
    collectionRecord.collectionRecordCount = 2;
    collectionRecord.collectionRecordIndex = 0;
    collectionRecord.transferId = 52;
    collectionRecord.baselineRevision = 100;
    check(EncodeSavedEntityMapBaselineMessage(
        collectionRecord, encoded.data(), encoded.size(), encodedSize));
    SavedEntityMapBaselineMessage collectionCommit = collectionBegin;
    collectionCommit.operation = SavedEntityMapBaselineOperation::CollectionCommit;
    collectionCommit.collectionRecordIndex = 2;
    check(EncodeSavedEntityMapBaselineMessage(
        collectionCommit, encoded.data(), encoded.size(), encodedSize));
    collectionCommit.collectionRecordIndex = 1;
    check(!EncodeSavedEntityMapBaselineMessage(
        collectionCommit, encoded.data(), encoded.size(), encodedSize));

    using fable::game::entity::persistence::SavedEntityMapBlobFormat;
    using fable::game::entity::persistence::native::SavedEntityCompression;
    using fable::multiplayer::persistence::GuestHeroSaveBoundary;
    using fable::multiplayer::persistence::SavedEntityCollectionRecord;

    GuestHeroSaveBoundary boundary;
    boundary.compression_.inflateInit_ =
        reinterpret_cast<SavedEntityCompression::InflateInitPointer>(
            &FakeInflateInit);
    boundary.compression_.inflate_ =
        reinterpret_cast<SavedEntityCompression::InflatePointer>(&FakeInflate);
    boundary.compression_.inflateEnd_ =
        reinterpret_cast<SavedEntityCompression::InflateEndPointer>(
            &FakeInflateEnd);
    boundary.compression_.compress2_ =
        reinterpret_cast<SavedEntityCompression::Compress2Pointer>(
            &FakeCompress2);
    boundary.compression_.compressBound_ =
        reinterpret_cast<SavedEntityCompression::CompressBoundPointer>(
            &FakeCompressBound);
    auto guestHero = MakeHero(0x100000001ull);
    AppendName(guestHero, "SCRIPT_NAME_HERO"); // Opaque post-END trailer is Hero-owned.
    const std::vector<std::uint8_t> guestNpc = {'G', 'U', 'E', 'S', 'T', '_', 'N', 'P', 'C'};

    SavedEntityCollectionRecord guestSourceRecord;
    guestSourceRecord.format = SavedEntityMapBlobFormat::Binary;
    guestSourceRecord.mapId = 17;
    guestSourceRecord.bytes = MakeCell("NULL", {guestHero, guestNpc});
    guestSourceRecord.metadata = static_cast<std::uint32_t>(
        guestSourceRecord.bytes.size());
    guestSourceRecord.hash = HashBytes(
        guestSourceRecord.bytes.data(), guestSourceRecord.bytes.size());
    fable::game::entity::persistence::SavedEntityMapBlobSnapshot guestSnapshot;
    guestSnapshot.format = guestSourceRecord.format;
    guestSnapshot.mapId = guestSourceRecord.mapId;
    guestSnapshot.bytes = guestSourceRecord.bytes.data();
    guestSnapshot.byteCount = guestSourceRecord.bytes.size();
    guestSnapshot.metadata = guestSourceRecord.metadata;
    guestSnapshot.hash = guestSourceRecord.hash;
    boundary.BeginGuestCollection();
    boundary.ObserveGuestRecord(guestSnapshot);
    check(boundary.CompleteGuestCollection(true));
    check(boundary.hero_.bytes == guestHero);

    // Give the host's copy of the same map a visibly different native cell.
    // The initial rewrite must preserve host NPCs and only import the Hero.
    const std::array<std::uint8_t, 4> hostPrefix = {0xA0, 0xB0, 0xC0, 0xD0};
    const std::vector<std::uint8_t> hostPrefixRecord(
        hostPrefix.begin(), hostPrefix.end());
    const std::vector<std::uint8_t> hostHero = MakeHero(0x200000002ull);
    SavedEntityCollectionRecord hostSourceRecord;
    hostSourceRecord.format = SavedEntityMapBlobFormat::Binary;
    hostSourceRecord.mapId = boundary.sourceMapId_;
    hostSourceRecord.bytes = MakeCell(
        "NULL", {hostPrefixRecord, hostHero});
    hostSourceRecord.metadata = static_cast<std::uint32_t>(
        hostSourceRecord.bytes.size());
    hostSourceRecord.hash = HashBytes(
        hostSourceRecord.bytes.data(), hostSourceRecord.bytes.size());
    std::map<std::uint16_t, SavedEntityCollectionRecord> records;
    records.emplace(hostSourceRecord.mapId, hostSourceRecord);

    // This is the normal ready/WorldReady policy: the initial guest Hero
    // remains present, even when the host collection is prepared again.
    check(boundary.RewriteHostCollection(1, records, true));
    auto sourceRecord = records.find(17);
    check(records.size() == 1 && sourceRecord != records.end() &&
        ContainsHero(sourceRecord->second) &&
        sourceRecord->second.bytes == MakeCell("NULL", {guestHero, hostPrefixRecord}));
    check(boundary.RewriteHostCollection(1, records, true));
    sourceRecord = records.find(17);
    check(records.size() == 1 && sourceRecord != records.end() &&
        ContainsHero(sourceRecord->second) &&
        sourceRecord->second.bytes == MakeCell("NULL", {guestHero, hostPrefixRecord}));

    // Only the explicit departure completion switches the source-scoped
    // boundary off; the same host record then has its Hero frame removed.
    check(boundary.RewriteHostCollection(1, records, false));
    sourceRecord = records.find(17);
    check(records.size() == 1 && sourceRecord != records.end() &&
        !ContainsHero(sourceRecord->second) &&
        sourceRecord->second.bytes.size() ==
            4 + 5 + 4 + 4 + hostPrefix.size() + 4 &&
        std::equal(
            hostPrefix.begin(), hostPrefix.end(),
            sourceRecord->second.bytes.end() - sizeof(std::uint32_t) -
                hostPrefix.size()));

    // No host cell means a Hero-only bootstrap container, not permission to
    // bring in the guest NPC/save contents. Retire removes that container.
    records.clear();
    check(boundary.RewriteHostCollection(2, records, true));
    check(records.size() == 1 && records.at(17).guestHeroBootstrapOnly &&
        records.at(17).bytes == MakeCell("NULL", {guestHero}));
    check(boundary.RewriteHostCollection(2, records, true));
    check(records.at(17).guestHeroBootstrapOnly &&
        records.at(17).bytes == MakeCell("NULL", {guestHero}));
    check(boundary.RewriteHostCollection(2, records, false));
    check(records.empty());

    // A real host update arriving during bootstrap replaces provenance and
    // must survive departure. Never restore a cached older host source cell.
    check(boundary.RewriteHostCollection(3, records, true));
    auto updatedHost = hostSourceRecord;
    const std::vector<std::uint8_t> newHostNpc = {0x51, 0x52, 0x53};
    updatedHost.bytes = MakeCell("NULL", {newHostNpc, hostHero});
    updatedHost.metadata = static_cast<std::uint32_t>(updatedHost.bytes.size());
    updatedHost.hash = HashBytes(updatedHost.bytes.data(), updatedHost.bytes.size());
    updatedHost.revision = 4;
    records[17] = updatedHost;
    check(boundary.RewriteHostCollection(4, records, true));
    check(!records.at(17).guestHeroBootstrapOnly &&
        records.at(17).bytes == MakeCell("NULL", {guestHero, newHostNpc}));
    check(boundary.RewriteHostCollection(4, records, false));
    check(records.size() == 1 && records.at(17).revision == 4 &&
        records.at(17).bytes == MakeCell("NULL", {newHostNpc}));

    return failures;
}
