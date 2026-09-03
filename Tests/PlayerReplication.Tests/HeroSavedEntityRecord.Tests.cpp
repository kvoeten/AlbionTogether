#include "Game/Entity/Persistence/Serialization/HeroSavedEntityRecord.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        const auto* const source = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), source, source + sizeof(value));
    }

    void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
    {
        const auto* const source = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), source, source + sizeof(value));
    }

    void AppendName(std::vector<std::uint8_t>& bytes, const char* name)
    {
        const std::size_t length = std::strlen(name);
        bytes.insert(
            bytes.end(),
            reinterpret_cast<const std::uint8_t*>(name),
            reinterpret_cast<const std::uint8_t*>(name) + length + 1);
    }

    std::vector<std::uint8_t> MakeHero(
        const std::uint64_t uid,
        const std::uint32_t trailerMiddle = 336)
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
        AppendU32(result, trailerMiddle);
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

    void AppendBytes(
        std::vector<std::uint8_t>& destination,
        const std::vector<std::uint8_t>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    struct CellFrame final
    {
        const char* name = nullptr;
        std::vector<std::uint8_t> payload;
    };

    std::vector<std::uint8_t> MakeRecordStream(
        const std::vector<std::vector<std::uint8_t>>& records)
    {
        std::vector<std::uint8_t> result;
        for (const auto& record : records)
        {
            AppendU32(result, static_cast<std::uint32_t>(record.size()));
            AppendBytes(result, record);
        }
        AppendU32(result, 0);
        return result;
    }

    std::vector<std::uint8_t> MakeCell(
        const std::vector<CellFrame>& frames)
    {
        std::vector<std::uint8_t> result;
        AppendU32(result, static_cast<std::uint32_t>(frames.size()));
        for (const CellFrame& frame : frames)
        {
            AppendName(result, frame.name);
            AppendU32(
                result,
                static_cast<std::uint32_t>(frame.payload.size()));
            AppendBytes(result, frame.payload);
        }
        return result;
    }

    std::uint32_t ReadU32(
        const std::vector<std::uint8_t>& bytes,
        const std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }
}

int RunHeroSavedEntityRecordTests()
{
    using fable::game::entity::persistence::serialization::
        ExtractHeroSavedEntityRecord;
    using fable::game::entity::persistence::serialization::
        HeroSavedEntityRecord;
    using fable::game::entity::persistence::serialization::
        RemoveHeroSavedEntityRecords;
    using fable::game::entity::persistence::serialization::
        SpliceHeroIntoAuthoritativeCell;

    int failures = 0;
    const auto require = [&failures](const bool condition)
    {
        if (!condition)
        {
            ++failures;
        }
    };

    std::vector<std::uint8_t> guest = MakeHero(0x100000001ull);
    // Retail Hero entries retain an opaque trailer after END. Identification
    // may parse the component terminator, but extraction must preserve the
    // complete native length-prefixed record payload.
    const std::vector<std::uint8_t> guestTrailer = {
        'S', 'C', 'R', 'I', 'P', 'T', '_', 'N', 'A', 'M', 'E', '_',
        'H', 'E', 'R', 'O', 0, 0, 0, 0, 0, 1, 0, 0, 0};
    AppendBytes(guest, guestTrailer);
    const std::vector<std::uint8_t> host = MakeHero(0x200000002ull);
    const std::vector<std::uint8_t> guestCell = MakeCell({
        {"NULL", MakeRecordStream({guest})},
        {"Q_Guest", MakeRecordStream({{0x10, 0x20, 0x30}})}});
    HeroSavedEntityRecord extracted;
    require(ExtractHeroSavedEntityRecord(
        guestCell.data(), guestCell.size(), extracted));
    require(extracted.uid == 0x100000001ull);
    require(extracted.bytes == guest);
    require(extracted.sectionName == "NULL");

    // The middle pre-component dword is deliberately not a component count:
    // this record declares 336 but contains only two component frames.
    require(ExtractHeroSavedEntityRecord(
        guestCell.data(), guestCell.size(), extracted));

    const std::vector<std::uint8_t> before = {0x11, 0x22, 0x33};
    const std::vector<std::uint8_t> after = {0x44, 0x55};
    const std::vector<std::uint8_t> hostPayload = MakeRecordStream({
        before,
        host,
        after});
    const std::vector<std::uint8_t> questPayload = MakeRecordStream({
        {0x91, 0x92, 0x93}});
    const std::vector<std::uint8_t> hostCell = MakeCell({
        {"NULL", hostPayload},
        {"Q_Stable", questPayload}});

    std::vector<std::uint8_t> spliced;
    require(SpliceHeroIntoAuthoritativeCell(
        hostCell.data(), hostCell.size(), extracted, spliced));
    require(spliced.size() ==
        hostCell.size() - host.size() + guest.size());
    // The first frame is "NULL\0" followed by its native payload length.
    constexpr std::size_t firstLengthOffset = 4 + 5;
    constexpr std::size_t firstPayloadOffset = firstLengthOffset + 4;
    const std::vector<std::uint8_t> expectedSplicedPayload =
        MakeRecordStream({guest, before, after});
    require(ReadU32(spliced, firstLengthOffset) ==
        expectedSplicedPayload.size());
    require(std::memcmp(
        spliced.data() + firstPayloadOffset,
        expectedSplicedPayload.data(),
        expectedSplicedPayload.size()) == 0);
    HeroSavedEntityRecord splicedHero;
    require(ExtractHeroSavedEntityRecord(
        spliced.data(), spliced.size(), splicedHero));
    require(splicedHero.uid == extracted.uid);
    require(splicedHero.bytes == guest); // Includes the full opaque native trailer.

    const auto emptyCell = MakeCell({});
    require(SpliceHeroIntoAuthoritativeCell(
        emptyCell.data(), emptyCell.size(), extracted, spliced));
    require(spliced == MakeCell({{"NULL", MakeRecordStream({guest})}}));
    const auto questOnlyCell = MakeCell({{"Q_Stable", questPayload}});
    require(SpliceHeroIntoAuthoritativeCell(
        questOnlyCell.data(), questOnlyCell.size(), extracted, spliced));
    require(spliced == MakeCell({{"Q_Stable", questPayload},
        {"NULL", MakeRecordStream({guest})}}));
    auto invalidSection = extracted;
    invalidSection.sectionName = std::string("NULL\0extra", 10);
    require(!SpliceHeroIntoAuthoritativeCell(emptyCell.data(), emptyCell.size(),
        invalidSection, spliced));

    std::vector<std::uint8_t> withoutHero;
    require(RemoveHeroSavedEntityRecords(
        hostCell.data(), hostCell.size(), withoutHero));
    require(withoutHero.size() ==
        hostCell.size() - host.size() - sizeof(std::uint32_t));
    const std::vector<std::uint8_t> expectedWithoutHeroPayload =
        MakeRecordStream({before, after});
    require(ReadU32(withoutHero, firstLengthOffset) ==
        expectedWithoutHeroPayload.size());
    require(std::memcmp(
        withoutHero.data() + firstPayloadOffset,
        expectedWithoutHeroPayload.data(),
        expectedWithoutHeroPayload.size()) == 0);

    // Regression: the first crash retained a stale section length and the
    // second retained the Hero entry's own length. Both framing layers must
    // be rebuilt while the following section remains byte-for-byte stable.
    require(withoutHero.size() >= questPayload.size());
    require(std::memcmp(
        withoutHero.data() + withoutHero.size() - questPayload.size(),
        questPayload.data(),
        questPayload.size()) == 0);

    const std::vector<std::uint8_t> noHero = MakeCell({
        {"NULL", MakeRecordStream({{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}})}});
    require(RemoveHeroSavedEntityRecords(
        noHero.data(), noHero.size(), withoutHero));
    require(withoutHero == noHero);

    const std::vector<std::uint8_t> duplicatePayload = MakeRecordStream({
        guest,
        guest});
    const std::vector<std::uint8_t> duplicate = MakeCell({
        {"NULL", duplicatePayload}});
    require(!ExtractHeroSavedEntityRecord(
        duplicate.data(), duplicate.size(), extracted));
    return failures;
}
