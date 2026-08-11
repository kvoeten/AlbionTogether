#include "CreatureModeManagerFunctions.h"

#include <cstring>

namespace fable::game::creature::locomotion::native
{
    bool CreatureModeManagerFunctions::ResolveAddSource(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + AddSourceRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    AddSourceExpectedPrefix.data(),
                    AddSourceExpectedPrefix.size()) != 0)
            {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool CreatureModeManagerFunctions::ResolveRemoveSource(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + RemoveSourceRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    RemoveSourceExpectedPrefix.data(),
                    RemoveSourceExpectedPrefix.size()) != 0)
            {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool CreatureModeManagerFunctions::ResolveEvaluateLocomotion(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) +
            EvaluateLocomotionRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    EvaluateLocomotionExpectedPrefix.data(),
                    EvaluateLocomotionExpectedPrefix.size()) != 0)
            {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
