#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    // Native CTCTargetingAI access for AI-controlled remote Hero pawns. Its
    // script-target override is deliberately separate from CTCTargetingHero's
    // selected/candidate target state.
    struct AiTargetingComponent final
    {
        using GetTargetPointer = void* (__thiscall*)(void*);
        using SetTargetPointer = void (__thiscall*)(void*, void*);

        static constexpr std::uintptr_t VtableRva = 0x02B10904;
        static constexpr std::size_t GetScriptTargetOverrideSlot = 35;
        static constexpr std::size_t SetScriptTargetOverrideSlot = 36;
        static constexpr std::uintptr_t GetScriptTargetOverrideRva =
            0x01A94505;
        static constexpr std::uintptr_t SetScriptTargetOverrideRva =
            0x01A9450D;

        [[nodiscard]] static bool Validate(
            HMODULE gameModule,
            void* targetingComponent) noexcept;
        [[nodiscard]] static void* GetScriptTargetOverride(
            HMODULE gameModule,
            void* targetingComponent) noexcept;
        [[nodiscard]] static bool SetScriptTargetOverride(
            HMODULE gameModule,
            void* targetingComponent,
            void* target) noexcept;
    };
}
