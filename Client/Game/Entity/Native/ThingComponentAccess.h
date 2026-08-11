#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::native
{
    enum class ThingComponentType : std::int32_t
    {
        PhysicsNavigator = 0x02,
        CreatureNavigation = 0x07,
        Targeting = 0x08,
        ScriptedControl = 0x1F,
        CreatureModeManager = 0x31,
        Look = 0x43,
        AnimationComplex = 0x5A,
        ActivationTrigger = 0x66,
    };

    struct ThingComponentEntry final
    {
        std::int32_t type = 0;
        void* instance = nullptr;
    };

    struct ThingComponentRange final
    {
        const ThingComponentEntry* begin = nullptr;
        const ThingComponentEntry* end = nullptr;
        std::size_t count = 0;
    };

    class ThingComponentAccess final
    {
    public:
        static constexpr std::size_t ComponentRangeOffset = 0x44;

        [[nodiscard]] static bool ReadRange(
            void* nativeThing,
            ThingComponentRange& range) noexcept;

        [[nodiscard]] static void* Find(
            void* nativeThing,
            ThingComponentType type) noexcept;

        [[nodiscard]] static bool Has(
            void* nativeThing,
            ThingComponentType type) noexcept;
    };

    static_assert(
        sizeof(ThingComponentEntry) == 8,
        "Unexpected Fable component-entry layout.");
}
