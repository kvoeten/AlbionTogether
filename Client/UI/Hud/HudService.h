#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <string>

namespace fable::game::native
{
    class GameInterfaceAccess;
}

namespace fable::ui
{
    struct HudColour final
    {
        std::uint8_t red = 0;
        std::uint8_t green = 0;
        std::uint8_t blue = 0;
        std::uint8_t alpha = 255;
    };

    class HudService final
    {
    public:
        bool Initialize(
            game::native::GameInterfaceAccess& gameInterface,
            const core::Diagnostics& diagnostics);
        bool ShowMessage(const std::string& textGroup, int selectionMethod = 2);
        bool AddHealthBar(
            float current,
            float maximum,
            const HudColour& primary,
            const HudColour& secondary,
            const std::string& texture,
            const std::string& text,
            float scale,
            int& elementId);
        bool UpdateHealthBar(
            int elementId,
            float current,
            float maximum,
            float scale = 1.0f);
        bool RemoveElement(int elementId);

    private:
        game::native::GameInterfaceAccess* gameInterface_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
