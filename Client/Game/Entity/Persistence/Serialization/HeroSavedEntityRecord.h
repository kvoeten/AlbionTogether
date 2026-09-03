#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fable::game::entity::persistence::serialization
{
    // Owned, pointer-free copy of the native saved Hero entity. The bytes are
    // kept opaque so this boundary cannot accidentally reinterpret historical
    // component fields or rewrite references owned by the game.
    struct HeroSavedEntityRecord final
    {
        std::vector<std::uint8_t> bytes;
        std::string sectionName;
        std::uint64_t uid = 0;

        void Clear() noexcept
        {
            bytes.clear();
            sectionName.clear();
            uid = 0;
        }
    };

    // Finds the one structurally valid Anniversary Hero record in a native
    // inflated cell and copies it without changing its bytes.
    bool ExtractHeroSavedEntityRecord(
        const std::uint8_t* cell,
        std::size_t cellBytes,
        HeroSavedEntityRecord& result);

    // Removes every structurally valid Hero record from a host cell. Cells
    // without a Hero are copied byte-for-byte, which supports host baselines
    // whose source map differs from the guest's source map.
    bool RemoveHeroSavedEntityRecords(
        const std::uint8_t* cell,
        std::size_t cellBytes,
        std::vector<std::uint8_t>& result);

    // Builds a host-world cell by removing structurally valid host Hero
    // records and inserting the exact guest record as a length-prefixed entry
    // in its original section. Every non-Hero record retains its order/value.
    bool SpliceHeroIntoAuthoritativeCell(
        const std::uint8_t* authoritativeCell,
        std::size_t authoritativeCellBytes,
        const HeroSavedEntityRecord& guestHero,
        std::vector<std::uint8_t>& result);
}
