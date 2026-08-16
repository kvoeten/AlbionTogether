#include "CreatureActionFunctions.h"

#include <cstring>

namespace fable::game::creature::actions::native
{
    bool CreatureActionFunctions::ResolveUpdate(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            UpdateAddressRva,
            UpdateExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::ResolveSubmit(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            SubmitAddressRva,
            SubmitExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::ResolveFinish(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            FinishAddressRva,
            FinishExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::Resolve(
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
        auto* const candidate = reinterpret_cast<std::uint8_t*>(base + addressRva);
        std::uintptr_t exceptionHandler = 0;
        __try
        {
            if (std::memcmp(
                    candidate,
                    ExpectedPrefix.data(),
                    ExpectedPrefix.size()) != 0)
            {
                return false;
            }
            std::memcpy(
                &exceptionHandler,
                candidate + ExpectedPrefix.size(),
                sizeof(exceptionHandler));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (exceptionHandler != base + exceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
