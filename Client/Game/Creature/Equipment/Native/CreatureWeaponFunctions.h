#pragma once

#include "Game/Creature/Equipment/CreatureWeaponFamily.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::equipment::native
{
    struct CreatureWeaponInspection final
    {
        void* carryingComponent = nullptr;
        void* meleeWeapon = nullptr;
        void* rangedWeapon = nullptr;
        void* meleeGraphic = nullptr;
        void* rangedGraphic = nullptr;
        std::uint32_t meleeAttachmentSlot = 0;
        std::uint32_t rangedAttachmentSlot = 0;
        std::uint32_t meleeStowedSlot = 0;
        std::uint32_t rangedStowedSlot = 0;
        std::uint32_t functionSignatureMask = 0;
        bool functionsResolved = false;
        bool meleePresent = false;
        bool rangedPresent = false;
        bool meleeStowed = false;
        bool rangedStowed = false;
    };

    struct CreatureCarryingEntry final
    {
        void* thing = nullptr;
        void* graphic = nullptr;
        std::int32_t definitionIndex = -1;
        std::uint32_t attachmentSlot = 0;
    };

    struct CreatureCarryingInspection final
    {
        static constexpr std::size_t Capacity = 32;
        std::array<CreatureCarryingEntry, Capacity> entries = {};
        std::size_t count = 0;
        bool truncated = false;
    };

    // Uses the retail AI weapon path: CTCCarrying owns a materialized weapon
    // Thing, while the replicated Hero state supplies the exact final carry
    // slots observed after a draw/stow mutation. This deliberately does not
    // require CTCHeroInventoryWeapons.
    class CreatureWeaponFunctions final
    {
    public:
        // True only when CTCCarrying retains the requested loadout and places
        // both weapon Things in their owner-observed attachment slots.
        [[nodiscard]] static bool ApplyLoadout(
            void* creature,
            std::int32_t meleeDefinitionIndex,
            std::int32_t rangedDefinitionIndex,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            CreatureWeaponFamily activeFamily,
            CreatureWeaponInspection* inspection = nullptr) noexcept;

        [[nodiscard]] static bool Inspect(
            void* creature,
            std::int32_t meleeDefinitionIndex,
            std::int32_t rangedDefinitionIndex,
            CreatureWeaponInspection& inspection) noexcept;
        [[nodiscard]] static bool InspectCarrying(
            void* creature,
            CreatureCarryingInspection& inspection) noexcept;
        // Removes template/default hand or back weapons that are not part of
        // the replicated owner's exact melee/ranged loadout.
        [[nodiscard]] static bool PruneUnexpectedWeapons(
            void* creature,
            std::int32_t allowedMeleeDefinitionIndex,
            std::uint32_t allowedMeleeAttachmentSlot,
            std::int32_t allowedRangedDefinitionIndex,
            std::uint32_t allowedRangedAttachmentSlot,
            std::size_t& removedCount) noexcept;
    };
}
