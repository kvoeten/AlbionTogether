#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fable::core::game_thread::native
{
    // Current retail native loop: void __thiscall(Application*). Unlike
    // PeekMessage, this wraps the simulation's own two update stages.
    struct SimulationFrameFunction final
    {
        static constexpr std::uintptr_t AddressRva = 0x018C9E16;
        static constexpr std::uintptr_t CallerRva = 0x018CCD72;
        static constexpr std::size_t ReturnOffset = 0x74;
        static constexpr std::size_t DisplacedBytes = 6;
        inline static constexpr std::uint8_t Prefix[] =
            {0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0x8B, 0xF1};

        static bool Matches(const void* prefix, const void* epilogue,
            const void* caller) noexcept
        {
            constexpr std::uint8_t returnBytes[] = {0x5E, 0xC9, 0xC3};
            constexpr std::uint8_t callBytes[] = {0xE8, 0x9F, 0xD0, 0xFF, 0xFF};
            return prefix != nullptr && epilogue != nullptr && caller != nullptr &&
                std::memcmp(prefix, Prefix, sizeof(Prefix)) == 0 &&
                std::memcmp(epilogue, returnBytes, sizeof(returnBytes)) == 0 &&
                std::memcmp(caller, callBytes, sizeof(callBytes)) == 0;
        }
    };
}
