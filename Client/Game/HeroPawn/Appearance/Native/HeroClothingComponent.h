#pragma once

#include "Game/HeroPawn/Appearance/HeroClothingState.h"

#include <cstdint>

namespace fable::game::hero_pawn::appearance::native
{
    class HeroClothingComponent final
    {
    public:
        [[nodiscard]] static bool Capture(
            void* nativeThing,
            HeroClothingState& state) noexcept;

        // Makes the proxy's actor-local clothing catalog aware of the selected
        // visual definitions, then uses CTCInventoryClothing's own wear and
        // appearance-rebuild lifecycle. The player's real inventory is never
        // modified and no renderer-owned buffers are copied.
        [[nodiscard]] static bool Apply(
            void* nativeThing,
            const HeroClothingState& state,
            std::uint32_t* insertedCount = nullptr) noexcept;
    };
}
