#include "MapwhoFunctions.h"

#include <cstring>

namespace fable::game::entity::presence::native
{
    bool MapwhoFunctions::ResolveRegister(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(gameModule, RegisterAddressRva, address);
    }

    bool MapwhoFunctions::ResolveUnregister(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(gameModule, UnregisterAddressRva, address);
    }

    bool MapwhoFunctions::ResolveUpdate(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(gameModule, UpdateAddressRva, address);
    }

    bool MapwhoFunctions::ResolveDestructor(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + DestructorAddressRva);
        std::uintptr_t exceptionHandler = 0;
        bool valid = false;
        __try
        {
            valid = candidate[0] == 0x6A && candidate[1] == 0xFF &&
                candidate[2] == 0x68;
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
            exceptionHandler != base + DestructorExceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool MapwhoFunctions::Resolve(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + addressRva);
        bool valid = false;
        __try
        {
            valid = std::memcmp(
                candidate,
                ExpectedPrefix.data(),
                ExpectedPrefix.size()) == 0;
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
