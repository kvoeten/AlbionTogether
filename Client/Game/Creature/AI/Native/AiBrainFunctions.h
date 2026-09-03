#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::ai::native
{
    struct AiBrainFunctions final
    {
        using UpdatePointer = void(__thiscall*)(void* brain);
        // The creature state-group selector is reached by several native
        // paths that do not call CAIBrain::Update.  It is a member function
        // with two stack arguments (the dispatcher returns with ret 8).
        using StateGroupDecisionPointer = bool(__thiscall*)(
            void* creature,
            int frameTime,
            void* nativeProposal);

        static constexpr std::uintptr_t VtableRva = 0x02AAF388;
        static constexpr std::uintptr_t UpdateRva = 0x016D7700;
        static constexpr std::size_t UpdateSlot = 4;

        static constexpr std::uintptr_t StateGroupDispatcherRva = 0x01B3B800;
        static constexpr std::uintptr_t StateGroupGlobalRva = 0x02FF1773;
        // The disassembler reports the preferred VA 0x01AD7F20; subtract
        // the PE image base (0x00400000) for the relocation-safe RVA.
        static constexpr std::uintptr_t StateGroupArbitrationRva = 0x016D7F20;
        static constexpr std::size_t StateGroupDisplacedBytes = 7;

        static constexpr std::size_t FiberOffset = 0x44;
        static constexpr std::size_t DefinitionOffset = 0x9C;
        static constexpr std::size_t ContextBeginOffset = 0x1C;
        static constexpr std::size_t FiberPausedOffset = 0x10;
        static constexpr std::size_t FiberDispatchSlot = 12;

        static bool ResolveUpdateSlot(
            HMODULE gameModule,
            void*** slot,
            UpdatePointer& function) noexcept;

        static bool ResolveStateGroupDispatcher(
            HMODULE gameModule,
            void** target,
            StateGroupDecisionPointer& function) noexcept;
    };
}
