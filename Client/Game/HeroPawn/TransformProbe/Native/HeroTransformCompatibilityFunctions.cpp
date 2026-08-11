#include "HeroTransformCompatibilityFunctions.h"

#include <cstring>

namespace
{
    template <std::size_t Size>
    bool ResolveFunction(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        const std::array<std::uint8_t, Size>& expectedPrefix,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + addressRva);
        __try
        {
            if (std::memcmp(
                    candidate,
                    expectedPrefix.data(),
                    expectedPrefix.size()) != 0)
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

namespace fable::game::hero_pawn::transform_probe::native
{
    bool Component68FractionalProgressFunction::Resolve(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return ResolveFunction(
            gameModule,
            AddressRva,
            ExpectedPrefix,
            address);
    }

    bool Component68DiscreteLevelFunction::Resolve(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return ResolveFunction(
            gameModule,
            AddressRva,
            ExpectedPrefix,
            address);
    }

    bool HeroUpdateComponent11Branch::Resolve(
        HMODULE gameModule,
        Addresses& addresses) noexcept
    {
        addresses = {};
        if (!ResolveFunction(
                gameModule,
                AddressRva,
                ExpectedPrefix,
                addresses.branch))
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        addresses.resume = base + ResumeRva;
        addresses.missingComponentCleanup =
            base + MissingComponentCleanupRva;
        return true;
    }
}
