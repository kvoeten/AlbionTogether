#pragma once

#include <Windows.h>

namespace fable::game::hero_pawn::equipment::native
{
    struct HeroWeaponComponentProvisioning final
    {
        void* heroCore = nullptr;
        void* component = nullptr;
        bool heroCoreAdded = false;
        bool added = false;
    };

    class HeroWeaponComponentProvisioner final
    {
    public:
        [[nodiscard]] static bool Ensure(
            HMODULE gameModule,
            void* hero,
            HeroWeaponComponentProvisioning& result) noexcept;
    };
}
