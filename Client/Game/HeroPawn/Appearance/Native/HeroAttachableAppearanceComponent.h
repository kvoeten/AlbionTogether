#pragma once

#include "Game/HeroPawn/Appearance/HeroAppearanceState.h"

#include <cstdint>

namespace fable::game::hero_pawn::appearance::native
{
    class HeroAttachableAppearanceComponent final
    {
    public:
        [[nodiscard]] static bool Capture(
            void* nativeThing,
            HeroAppearanceModifierState& state) noexcept;

        // Reconciles the remote actor through the component's own native
        // remove/add/dirty-refresh lifecycle. No renderer buffers are copied.
        [[nodiscard]] static bool Apply(
            void* nativeThing,
            const HeroAppearanceModifierState& state,
            std::uint32_t* removedCount = nullptr,
            std::uint32_t* addedCount = nullptr) noexcept;
    };
}
