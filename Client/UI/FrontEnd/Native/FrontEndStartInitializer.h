#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::ui::front_end::native
{
    struct FrontEndStartInitializer final
    {
        using Pointer = void(__thiscall*)(void* frontEndStart);

        static constexpr std::uintptr_t AddressRva = 0x01C38500;
        static constexpr std::uintptr_t ExceptionHandlerRva = 0x025673B8;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, 3> ExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool Resolve(HMODULE gameModule, std::uint8_t*& address) noexcept;
    };
}
