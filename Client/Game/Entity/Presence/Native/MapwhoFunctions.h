#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::presence::native
{
    struct MapwhoFunctions final
    {
        using RegisterPointer = void(__thiscall*)(
            void* component,
            const void* worldPosition);
        using UnregisterPointer = void(__thiscall*)(void* component);
        using DestructorPointer = void* (__thiscall*)(
            void* component,
            unsigned int flags);

        static constexpr std::uintptr_t RegisterAddressRva = 0x01A5C7A0;
        static constexpr std::uintptr_t UpdateAddressRva = 0x01A5C7C0;
        static constexpr std::uintptr_t UnregisterAddressRva = 0x01A5C800;
        static constexpr std::uintptr_t DestructorAddressRva = 0x01A5C820;
        static constexpr std::uintptr_t DestructorExceptionHandlerRva =
            0x0254B568;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, DisplacedBytes>
            ExpectedPrefix = {
                0x56,
                0x8B, 0xF1,
                0xF6, 0x46, 0x50, 0x01,
            };

        static bool ResolveRegister(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveUnregister(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveUpdate(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveDestructor(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;

    private:
        static bool Resolve(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            std::uint8_t*& address) noexcept;
    };
}
