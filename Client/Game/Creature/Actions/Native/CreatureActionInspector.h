#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::actions::native
{
    // Read-only, fail-closed view over the current Fable action ABI. Native
    // pointers never escape as network state; callers receive semantic names,
    // IDs, or validated related objects only for the duration of observation.
    class CreatureActionInspector final
    {
    public:
        static bool DescribeType(
            void* action,
            char* name,
            std::size_t capacity) noexcept;
        [[nodiscard]] static std::uint32_t AnimationId(
            void* action) noexcept;
        [[nodiscard]] static void* AttackTarget(
            HMODULE gameModule,
            void* action,
            const char* actionType) noexcept;
        [[nodiscard]] static bool MayCarryAnimation(
            const char* actionType) noexcept;
        [[nodiscard]] static void* Owner(void* action) noexcept;
        [[nodiscard]] static void* ActiveAction(void* creature) noexcept;
        [[nodiscard]] static bool IsFinished(void* action) noexcept;
    };
}
