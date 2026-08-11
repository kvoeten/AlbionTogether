#include "CreatureConstructorFunction.h"

#include <cstring>

namespace fable::game::creature::native
{
    bool CreatureConstructorFunction::Resolve(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(base + AddressRva);
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

        if (exceptionHandler != base + ExceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
