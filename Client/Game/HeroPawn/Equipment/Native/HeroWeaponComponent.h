#pragma once

#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"

namespace fable::game::native
{
    class GameInterfaceAccess;
    struct ScriptThing;
}

namespace fable::game::hero_pawn::equipment::native
{
    struct HeroWeaponInspection final
    {
        void* component = nullptr;
        void* carryingComponent = nullptr;
        void* meleeWeapon = nullptr;
        void* rangedWeapon = nullptr;
        std::int32_t meleeDefinitionIndex = -2;
        std::int32_t rangedDefinitionIndex = -2;
        std::uint32_t meleeAttachmentSlot = 0;
        std::uint32_t rangedAttachmentSlot = 0;
        std::uint32_t functionSignatureMask = 0;
        game::creature::equipment::CreatureWeaponFamily activeFamily =
            game::creature::equipment::CreatureWeaponFamily::None;
        bool functionsResolved = false;
        bool readable = false;
    };

    class HeroWeaponComponent final
    {
    public:
        static constexpr std::uintptr_t ExpectedVtableRva = 0x02B04D84;

        [[nodiscard]] static bool Capture(
            void* nativeThing,
            HeroEquipmentState& state) noexcept;
        [[nodiscard]] static bool Apply(
            void* nativeThing,
            const HeroEquipmentState& state) noexcept;
        // Materializes/removes the Hero inventory-weapon Things without
        // requiring the asynchronous sheathe/unsheathe action to have already
        // reached its final carry slots. Presentation ownership remains with
        // RequestActiveFamily and the retail Hero action stack.
        [[nodiscard]] static bool ApplyDefinitions(
            void* nativeThing,
            const HeroEquipmentState& state) noexcept;
        [[nodiscard]] static bool Inspect(
            void* nativeThing,
            HeroWeaponInspection& inspection) noexcept;
        // Requests the retail Hero sheathe/unsheathe action. Unlike Apply,
        // this changes presentation state rather than inventory ownership.
        [[nodiscard]] static bool RequestActiveFamily(
            game::native::GameInterfaceAccess& interfaceAccess,
            const game::native::ScriptThing& hero,
            game::creature::equipment::CreatureWeaponFamily family) noexcept;
    };
}
