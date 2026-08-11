#include "FollowCreatureActionFunctions.h"

#include <cstring>

namespace fable::game::creature::locomotion::native
{
    bool FollowCreatureActionFunctions::ResolveTick(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + ActionTickRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    TickExpectedPrefix.data(),
                    TickExpectedPrefix.size()) != 0)
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

    bool FollowCreatureActionFunctions::ResolveStart(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + ActionStartRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    StartExpectedPrefix.data(),
                    StartExpectedPrefix.size()) != 0)
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
