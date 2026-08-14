#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::appearance
{
    // Current definition-backed presentation modifiers owned by
    // CTCHeroAttachableAppearanceModifiers. These are bounded actor state,
    // not a history of equipment or hairstyle changes.
    struct HeroAppearanceModifierState final
    {
        static constexpr std::size_t MaximumEntries = 16;

        bool valid = false;
        std::uint32_t count = 0;
        std::array<std::int32_t, MaximumEntries> definitionIndices = {};

        [[nodiscard]] bool IsSane() const noexcept
        {
            if (!valid || count > MaximumEntries)
            {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::int32_t definitionIndex = definitionIndices[index];
                if (definitionIndex <= 0 || definitionIndex >= 1'000'000)
                {
                    return false;
                }
                for (std::size_t earlier = 0; earlier < index; ++earlier)
                {
                    if (definitionIndices[earlier] == definitionIndex)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] bool Contains(std::int32_t definitionIndex) const noexcept
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (definitionIndices[index] == definitionIndex)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool Equals(
            const HeroAppearanceModifierState& other) const noexcept
        {
            if (valid != other.valid || count != other.count)
            {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                if (definitionIndices[index] != other.definitionIndices[index])
                {
                    return false;
                }
            }
            return true;
        }
    };
}
