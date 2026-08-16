#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::actions::native
{
    struct CreatureActionFunctions final
    {
        using SubmitPointer = bool(__thiscall*)(void* creature, void* action);
        using FinishPointer = void(__thiscall*)(void* action);
        using UpdatePointer = void(__thiscall*)(void* creature);

        static constexpr std::uintptr_t UpdateAddressRva = 0x01B42E20;
        static constexpr std::uintptr_t UpdateExceptionHandlerRva = 0x02549BC8;
        static constexpr std::uintptr_t SubmitAddressRva = 0x01B42F70;
        static constexpr std::uintptr_t SubmitExceptionHandlerRva = 0x02553CB0;
        static constexpr std::uintptr_t FinishAddressRva = 0x017EF370;
        static constexpr std::uintptr_t FinishExceptionHandlerRva = 0x02512A56;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, 3> ExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool ResolveUpdate(HMODULE gameModule, std::uint8_t*& address) noexcept;
        static bool ResolveSubmit(HMODULE gameModule, std::uint8_t*& address) noexcept;
        static bool ResolveFinish(HMODULE gameModule, std::uint8_t*& address) noexcept;

    private:
        static bool Resolve(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            std::uintptr_t exceptionHandlerRva,
            std::uint8_t*& address) noexcept;
    };
}
