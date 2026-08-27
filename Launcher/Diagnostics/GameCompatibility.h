#pragma once

#include <filesystem>
#include <string>

namespace fable::launcher::diagnostics
{
    enum class GameCompatibilityState
    {
        Compatible,
        Missing,
        Unsupported,
        Error,
    };

    struct GameCompatibilityResult final
    {
        GameCompatibilityState state = GameCompatibilityState::Missing;
        std::filesystem::path executable;
        std::wstring detail;

        [[nodiscard]] bool IsCompatible() const noexcept
        {
            return state == GameCompatibilityState::Compatible;
        }
    };

    [[nodiscard]] GameCompatibilityResult CheckGameCompatibility(
        const std::filesystem::path& executable);
}
