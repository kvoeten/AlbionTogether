#pragma once

#include <Windows.h>

namespace fable::game::entity::native
{
    class ThingComponentProvisioner final
    {
    public:
        [[nodiscard]] static bool IsSupported(HMODULE gameModule) noexcept;
        [[nodiscard]] static void* AddNamed(
            HMODULE gameModule,
            void* thing,
            const char* componentName) noexcept;
    };
}
