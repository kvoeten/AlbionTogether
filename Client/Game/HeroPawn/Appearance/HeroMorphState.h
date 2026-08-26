#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace fable::game::hero_pawn::appearance
{
    struct HeroBoneScale final
    {
        // Stable index in the SkeletalMesh reference skeleton. UE3 FName
        // indices are process-local and therefore must not cross the wire.
        std::uint16_t boneIndex = 0;
        float x = 1.0f;
        float y = 1.0f;
        float z = 1.0f;
    };

    struct HeroBoneScaleState final
    {
        // Fable's Hero skeleton currently materializes 91 mass-scaling slots.
        // The wire representation quantizes each vector to keep the complete
        // baseline within the bounded reliable-message limit. Transport may
        // split that baseline across conservative-size UDP datagrams.
        // The retail Hero currently materializes 91 entries.
        static constexpr std::size_t MaximumEntries = 120;

        bool valid = false;
        std::uint32_t count = 0;
        std::array<HeroBoneScale, MaximumEntries> entries = {};

        [[nodiscard]] bool IsSane() const noexcept
        {
            if (!valid || count > MaximumEntries)
            {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const HeroBoneScale& entry = entries[index];
                const auto saneScale = [](float value) noexcept
                {
                    return std::isfinite(value) && value >= 0.01f &&
                        value <= 16.0f;
                };
                if (entry.boneIndex >= 1'024 ||
                    !saneScale(entry.x) || !saneScale(entry.y) ||
                    !saneScale(entry.z))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool Equals(
            const HeroBoneScaleState& other) const noexcept
        {
            if (valid != other.valid || count != other.count)
            {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const HeroBoneScale& firstEntry = entries[index];
                const HeroBoneScale& secondEntry = other.entries[index];
                if (firstEntry.boneIndex != secondEntry.boneIndex ||
                    firstEntry.x != secondEntry.x ||
                    firstEntry.y != secondEntry.y ||
                    firstEntry.z != secondEntry.z)
                {
                    return false;
                }
            }
            return true;
        }
    };

    static_assert(
        sizeof(HeroBoneScale) == 16,
        "Unexpected replicated Hero bone-scale layout.");

    // The bounded, save-owned inputs consumed by Fable's Hero skeletal-morph
    // builder. This is current actor state, not a history of morph updates.
    struct HeroMorphState final
    {
        bool valid = false;
        bool child = false;
        float strength = 0.0f;
        float berserk = 0.0f;
        float will = 0.0f;
        float skill = 0.0f;
        float age = 0.0f;
        float alignment = 0.0f;
        float fatness = 0.0f;
        float auxiliary = 0.0f;

        [[nodiscard]] bool IsSane() const noexcept
        {
            const auto unitValue = [](float value) noexcept
            {
                return std::isfinite(value) && value >= -0.001f &&
                    value <= 1.001f;
            };
            return valid && unitValue(strength) && unitValue(berserk) &&
                unitValue(will) && unitValue(skill) && unitValue(age) &&
                unitValue(alignment) && unitValue(fatness) &&
                std::isfinite(auxiliary) && auxiliary >= -16.0f &&
                auxiliary <= 16.0f;
        }

        [[nodiscard]] bool Equals(const HeroMorphState& other) const noexcept
        {
            return valid == other.valid && child == other.child &&
                strength == other.strength && berserk == other.berserk &&
                will == other.will && skill == other.skill &&
                age == other.age && alignment == other.alignment &&
                fatness == other.fatness && auxiliary == other.auxiliary;
        }
    };
}
