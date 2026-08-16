#include "SavedEntitiesFunctions.h"

#include <array>
#include <cstring>

namespace fable::game::entity::persistence::native
{
    bool SavedEntitiesFunctions::ResolveLoadText(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            LoadTextAddressRva,
            LoadTextExceptionHandlerRva,
            address);
    }

    bool SavedEntitiesFunctions::ResolveLoadBinary(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            LoadBinaryAddressRva,
            LoadBinaryExceptionHandlerRva,
            address);
    }

    bool SavedEntitiesFunctions::ResolveRecordVectorResize(
        HMODULE gameModule,
        RecordVectorResizePointer& function) noexcept
    {
        static constexpr std::array<std::uint8_t, 15> Expected = {
            0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x5E, 0x04,
            0x8B, 0x2E, 0x8B, 0xCB, 0x2B, 0xCD, 0xB8,
        };
        std::uint8_t* address = nullptr;
        if (!Matches(
                gameModule,
                RecordVectorResizeAddressRva,
                Expected.data(),
                Expected.size(),
                address))
        {
            function = nullptr;
            return false;
        }
        function = reinterpret_cast<RecordVectorResizePointer>(address);
        return true;
    }

    bool SavedEntitiesFunctions::ResolveByteVectorResize(
        HMODULE gameModule,
        ByteVectorResizePointer& function) noexcept
    {
        static constexpr std::array<std::uint8_t, 17> Expected = {
            0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x04, 0x8B, 0x06,
            0x8B, 0xD1, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x2B,
            0xD0,
        };
        std::uint8_t* address = nullptr;
        if (!Matches(
                gameModule,
                ByteVectorResizeAddressRva,
                Expected.data(),
                Expected.size(),
                address))
        {
            function = nullptr;
            return false;
        }
        function = reinterpret_cast<ByteVectorResizePointer>(address);
        return true;
    }

    bool SavedEntitiesFunctions::Resolve(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        std::uintptr_t exceptionHandlerRva,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + addressRva);
        std::uintptr_t exceptionHandler = 0;
        bool valid = false;
        __try
        {
            valid = candidate[0] == 0x6A && candidate[1] == 0xFF &&
                candidate[2] == 0x68 && candidate[7] == 0x64 &&
                candidate[8] == 0xA1;
            std::memcpy(
                &exceptionHandler,
                candidate + 3,
                sizeof(exceptionHandler));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid || exceptionHandler != base + exceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool SavedEntitiesFunctions::Matches(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        const std::uint8_t* expected,
        std::size_t expectedSize,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr || expected == nullptr || expectedSize == 0)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + addressRva);
        bool valid = false;
        __try
        {
            valid = std::memcmp(candidate, expected, expectedSize) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
