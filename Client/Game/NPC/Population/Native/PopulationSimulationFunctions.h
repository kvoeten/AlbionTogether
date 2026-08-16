#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::npc::population::native
{
    struct PopulationSimulationFunctions final
    {
        using SimulationPointer = void(__thiscall*)(void* scriptObject);

        static constexpr std::uintptr_t ProcessAlbionAddressRva = 0x01608E80;
        static constexpr std::uintptr_t ProcessAlbionExceptionHandlerRva =
            0x024EE978;
        static constexpr std::uintptr_t HighDetailAddressRva = 0x01609DD0;
        static constexpr std::uintptr_t HighDetailExceptionHandlerRva =
            0x024EEC05;
        static constexpr std::size_t DisplacedBytes = 7;

        static bool ResolveProcessAlbion(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveHighDetail(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;

    private:
        static bool Resolve(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            std::uintptr_t exceptionHandlerRva,
            std::uint8_t*& address) noexcept;
    };
}
