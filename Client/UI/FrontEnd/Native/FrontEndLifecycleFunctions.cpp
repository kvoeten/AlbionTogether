#include "FrontEndLifecycleFunctions.h"

#include <cstring>

namespace fable::ui::front_end::native
{
    bool FrontEndLifecycleFunctions::Resolve(
        HMODULE gameModule,
        Addresses& addresses,
        const char*& failedDefinition) noexcept
    {
        addresses.fill(nullptr);
        failedDefinition = nullptr;
        if (gameModule == nullptr)
        {
            failedDefinition = "main executable module";
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        for (const Definition& definition : Definitions)
        {
            const auto index = static_cast<std::size_t>(definition.boundary);
            auto* const candidate = reinterpret_cast<std::uint8_t*>(
                base + definition.addressRva);
            bool matches = false;
            __try
            {
                matches = std::memcmp(
                    candidate,
                    ExpectedPrefix.data(),
                    ExpectedPrefix.size()) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                matches = false;
            }

            if (!matches)
            {
                failedDefinition = definition.name;
                return false;
            }
            addresses[index] = candidate;
        }
        return true;
    }
}
