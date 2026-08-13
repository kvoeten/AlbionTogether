#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::appearance
{
    // One semantic equipped definition per CTCInventoryClothing category.
    // This is current actor state, not a copy of the player's inventory.
    struct HeroClothingState final
    {
        static constexpr std::size_t SlotCount = 6;

        bool valid = false;
        std::array<std::int32_t, SlotCount> definitionIndices = {
            -1, -1, -1, -1, -1, -1};

        [[nodiscard]] bool IsSane() const noexcept
        {
            if (!valid)
            {
                return false;
            }
            for (const std::int32_t definitionIndex : definitionIndices)
            {
                if (definitionIndex != -1 &&
                    (definitionIndex <= 0 || definitionIndex >= 1'000'000))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool Equals(
            const HeroClothingState& other) const noexcept
        {
            return valid == other.valid &&
                definitionIndices == other.definitionIndices;
        }
    };
}
