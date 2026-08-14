#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion::native
{
    struct CreatureModeManagerFunctions final
    {
        using AddSourcePointer = bool(__thiscall*)(void* manager, int source);
        using RemoveSourcePointer = void(__thiscall*)(void* manager, int source);
        using EvaluateLocomotionPointer = void(__thiscall*)(
            void* mode,
            void* evaluationContext);

        static constexpr std::uintptr_t AddSourceRva = 0x01974A50;
        static constexpr std::uintptr_t RemoveSourceRva = 0x01973A10;
        static constexpr std::uintptr_t EvaluateLocomotionRva = 0x0181A4B0;
        static constexpr std::size_t DisplacedBytes = 7;

        // The absolute SEH address following AddSource's third byte is rebased.
        static constexpr std::array<std::uint8_t, 3> AddSourceExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };
        static constexpr std::array<std::uint8_t, DisplacedBytes>
            RemoveSourceExpectedPrefix = {
                0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10,
            };
        static constexpr std::array<std::uint8_t, DisplacedBytes>
            EvaluateLocomotionExpectedPrefix = {
                0x83, 0xEC, 0x1C, 0x56, 0x8B, 0xF1, 0x57,
            };

        static bool ResolveAddSource(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveRemoveSource(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveEvaluateLocomotion(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
    };
}
