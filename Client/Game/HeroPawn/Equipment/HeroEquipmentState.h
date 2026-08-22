#pragma once

#include "Game/Creature/Equipment/CreatureWeaponFamily.h"

#include <cstdint>

namespace fable::game::hero_pawn::equipment
{
    struct HeroWeaponDefinitions final
    {
        std::int32_t meleeDefinitionIndex = -1;
        std::int32_t rangedDefinitionIndex = -1;

        [[nodiscard]] bool IsSane() const noexcept
        {
            const auto saneDefinition = [](std::int32_t definitionIndex)
            {
                return definitionIndex == -1 ||
                    (definitionIndex > 0 && definitionIndex < 1'000'000);
            };
            return saneDefinition(meleeDefinitionIndex) &&
                saneDefinition(rangedDefinitionIndex);
        }

        [[nodiscard]] bool Equals(
            const HeroWeaponDefinitions& other) const noexcept
        {
            return meleeDefinitionIndex == other.meleeDefinitionIndex &&
                rangedDefinitionIndex == other.rangedDefinitionIndex;
        }

        [[nodiscard]] bool Supports(
            game::creature::equipment::CreatureWeaponFamily family) const
            noexcept
        {
            using game::creature::equipment::CreatureWeaponFamily;
            return family == CreatureWeaponFamily::None ||
                (family == CreatureWeaponFamily::Melee &&
                    meleeDefinitionIndex > 0) ||
                (family == CreatureWeaponFamily::Ranged &&
                    rangedDefinitionIndex > 0);
        }
    };

    // Current carried weapon presentation, independent from the player's
    // full inventory. -1 means that weapon class is unequipped.
    struct HeroEquipmentState final
    {
        bool valid = false;
        std::int32_t meleeDefinitionIndex = -1;
        std::int32_t rangedDefinitionIndex = -1;
        std::uint32_t meleeAttachmentSlot = 0;
        std::uint32_t rangedAttachmentSlot = 0;
        game::creature::equipment::CreatureWeaponFamily activeFamily =
            game::creature::equipment::CreatureWeaponFamily::None;

        [[nodiscard]] bool IsSane() const noexcept
        {
            const auto saneDefinition = [](std::int32_t definitionIndex)
            {
                return definitionIndex == -1 ||
                    (definitionIndex > 0 && definitionIndex < 1'000'000);
            };
            using game::creature::equipment::CreatureWeaponFamily;
            const bool saneFamily =
                activeFamily == CreatureWeaponFamily::None ||
                activeFamily == CreatureWeaponFamily::Melee ||
                activeFamily == CreatureWeaponFamily::Ranged;
            const bool availableFamily =
                activeFamily == CreatureWeaponFamily::None ||
                (activeFamily == CreatureWeaponFamily::Melee &&
                    meleeDefinitionIndex > 0) ||
                (activeFamily == CreatureWeaponFamily::Ranged &&
                    rangedDefinitionIndex > 0);
            const auto saneAttachment = [](std::int32_t definitionIndex,
                                           std::uint32_t attachmentSlot)
            {
                return definitionIndex == -1
                    ? attachmentSlot == 0
                    : attachmentSlot < 1'000'000;
            };
            return valid && saneDefinition(meleeDefinitionIndex) &&
                saneDefinition(rangedDefinitionIndex) && saneFamily &&
                availableFamily &&
                saneAttachment(
                    meleeDefinitionIndex, meleeAttachmentSlot) &&
                saneAttachment(
                    rangedDefinitionIndex, rangedAttachmentSlot);
        }

        [[nodiscard]] bool Equals(
            const HeroEquipmentState& other) const noexcept
        {
            return valid == other.valid &&
                meleeDefinitionIndex == other.meleeDefinitionIndex &&
                rangedDefinitionIndex == other.rangedDefinitionIndex &&
                meleeAttachmentSlot == other.meleeAttachmentSlot &&
                rangedAttachmentSlot == other.rangedAttachmentSlot &&
                activeFamily == other.activeFamily;
        }

        [[nodiscard]] HeroWeaponDefinitions WeaponDefinitions() const
            noexcept
        {
            return {meleeDefinitionIndex, rangedDefinitionIndex};
        }
    };
}
