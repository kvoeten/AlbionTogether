#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence::native
{
    struct ThingSaveFunctions final
    {
        using SavePointer = void(__thiscall*)(void* thing, void* writer);
        using LoadPointer = bool(__thiscall*)(void* thing, void* reader);

        static constexpr std::uintptr_t SaveAddressRva = 0x01B2DD10;
        static constexpr std::uintptr_t SaveExceptionHandlerRva = 0x025524B4;
        static constexpr std::uintptr_t LoadAddressRva = 0x01B30C20;
        static constexpr std::uintptr_t LoadExceptionHandlerRva = 0x02552955;
        static constexpr std::size_t DisplacedBytes = 7;

        static bool ResolveSave(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveLoad(
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
