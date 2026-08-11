#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::native
{
    struct CreatureFrameFunctions final
    {
        using UpdateFramePointer = bool(__thiscall*)(void* creature);

        static constexpr std::uintptr_t CreatureVtableRva = 0x02B1AFE4;
        static constexpr std::uintptr_t PlayerCreatureVtableRva = 0x02B1DBB4;
        static constexpr std::uintptr_t CreatureUpdateFrameRva = 0x01B36F20;
        static constexpr std::uintptr_t PlayerCreatureUpdateFrameRva =
            0x01B60940;
        static constexpr std::size_t UpdateFrameVtableSlot = 22;
        static constexpr std::size_t MotionXOffset = 0x134;
        static constexpr std::size_t MotionYOffset = 0x138;
        static constexpr std::size_t MotionZOffset = 0x13C;

        static constexpr std::array<std::uint8_t, 4>
            CreatureUpdateFrameExpectedPrefix = {
            0x83, 0xEC, 0x14, 0x80,
        };
        static constexpr std::array<std::uint8_t, 4>
            CreatureUpdateFrameExpectedSuffix = {
            0x56, 0x57, 0x8B, 0xF1,
        };
        static constexpr std::size_t CreatureUpdateFrameSuffixOffset = 10;
        static constexpr std::array<std::uint8_t, 7>
            PlayerUpdateFrameExpectedPrefix = {
            0x83, 0xEC, 0x14, 0x56, 0x57, 0x8B, 0xF1,
        };

        static bool ValidateImplementations(HMODULE gameModule) noexcept;
        static bool ResolveCreatureUpdateFrameSlot(
            HMODULE gameModule,
            void*** slot,
            UpdateFramePointer& function) noexcept;
        static bool ResolvePlayerUpdateFrameSlot(
            HMODULE gameModule,
            void*** slot,
            UpdateFramePointer& function) noexcept;
        static bool ValidateCreature(
            HMODULE gameModule,
            void* nativeThing) noexcept;
        static bool ValidatePlayerCreature(
            HMODULE gameModule,
            void* nativeThing) noexcept;
    };
}
