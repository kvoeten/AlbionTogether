#pragma once

#include <Windows.h>

namespace fable::game::hero_pawn::abilities::native
{
    struct HeroWillComponentProvisioning final
    {
        void* component = nullptr;
        bool added = false;
        bool abilityInventoryPresent = false;
        bool abilityInventoryAdded = false;
    };

    class HeroWillComponentProvisioner final
    {
    public:
        [[nodiscard]] static bool Ensure(
            HMODULE gameModule,
            void* hero,
            HeroWillComponentProvisioning& result) noexcept;
    };
}
