#include "CreatureHitResolutionFunction.h"

#include <cstring>

namespace fable::game::creature::combat::native
{
    bool CreatureHitResolutionFunction::Resolve(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + AddressRva);
        bool valid = false;
        __try
        {
            valid = std::memcmp(
                candidate,
                ExpectedPrefix.data(),
                ExpectedPrefix.size()) == 0 &&
                *reinterpret_cast<const std::uintptr_t*>(
                    candidate + ExpectedPrefix.size()) ==
                    reinterpret_cast<std::uintptr_t>(gameModule) +
                        ExceptionHandlerRva;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (valid)
        {
            address = candidate;
        }
        return valid;
    }
}
