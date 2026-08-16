#include "ThingSaveFunctions.h"

#include <cstring>

namespace fable::game::entity::persistence::native
{
    bool ThingSaveFunctions::ResolveSave(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            SaveAddressRva,
            SaveExceptionHandlerRva,
            address);
    }

    bool ThingSaveFunctions::ResolveLoad(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            LoadAddressRva,
            LoadExceptionHandlerRva,
            address);
    }

    bool ThingSaveFunctions::Resolve(
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
        if (!valid ||
            exceptionHandler != base + exceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
