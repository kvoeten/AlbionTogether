#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence::native
{
    struct SavedEntitiesFunctions final
    {
        using LoadPointer = void(__thiscall*)(
            void* savedEntities,
            void* reader);
        using RecordVectorResizePointer = void(__thiscall*)(
            void* recordVector,
            std::size_t recordCount);
        using ByteVectorResizePointer = void(__thiscall*)(
            void* byteVector,
            std::size_t byteCount);

        static constexpr std::uintptr_t LoadTextAddressRva = 0x01B527F0;
        static constexpr std::uintptr_t LoadTextExceptionHandlerRva =
            0x0255512F;
        static constexpr std::uintptr_t LoadBinaryAddressRva = 0x01B52D90;
        static constexpr std::uintptr_t LoadBinaryExceptionHandlerRva =
            0x0255517B;
        static constexpr std::uintptr_t RecordVectorResizeAddressRva =
            0x01B51A70;
        static constexpr std::uintptr_t ByteVectorResizeAddressRva =
            0x0127D700;
        static constexpr std::size_t DisplacedBytes = 7;

        static bool ResolveLoadText(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveLoadBinary(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveRecordVectorResize(
            HMODULE gameModule,
            RecordVectorResizePointer& function) noexcept;
        static bool ResolveByteVectorResize(
            HMODULE gameModule,
            ByteVectorResizePointer& function) noexcept;

    private:
        static bool Resolve(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            std::uintptr_t exceptionHandlerRva,
            std::uint8_t*& address) noexcept;
        static bool Matches(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            const std::uint8_t* expected,
            std::size_t expectedSize,
            std::uint8_t*& address) noexcept;
    };
}
