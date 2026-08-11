#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

namespace fable::game::hero_pawn::transform_probe::native
{
    struct Component68FractionalProgressFunction final
    {
        static constexpr std::uintptr_t AddressRva = 0x019B8A90;
        static constexpr std::array<std::uint8_t, 6> ExpectedPrefix = {
            0x83, 0xEC, 0x08, 0x8B, 0x41, 0x70,
        };

        static bool Resolve(HMODULE gameModule, std::uint8_t*& address) noexcept;
    };

    struct Component68DiscreteLevelFunction final
    {
        static constexpr std::uintptr_t AddressRva = 0x019B91E0;
        static constexpr std::array<std::uint8_t, 8> ExpectedPrefix = {
            0x8B, 0x41, 0x70, 0xF3, 0x0F, 0x10, 0x41, 0x38,
        };

        static bool Resolve(HMODULE gameModule, std::uint8_t*& address) noexcept;
    };

    struct HeroUpdateComponent11Branch final
    {
        static constexpr std::uintptr_t AddressRva = 0x018FAF3D;
        static constexpr std::uintptr_t ResumeRva = 0x018FAF44;
        static constexpr std::uintptr_t MissingComponentCleanupRva = 0x018FB8B0;
        static constexpr std::array<std::uint8_t, 7> ExpectedPrefix = {
            0x84, 0xC0, 0x74, 0x2E, 0x8D, 0x45, 0xD8,
        };

        struct Addresses final
        {
            std::uint8_t* branch = nullptr;
            std::uintptr_t resume = 0;
            std::uintptr_t missingComponentCleanup = 0;
        };

        static bool Resolve(HMODULE gameModule, Addresses& addresses) noexcept;
    };
}
