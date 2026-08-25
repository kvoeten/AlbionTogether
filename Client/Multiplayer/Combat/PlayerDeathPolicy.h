#pragma once

#include <cmath>

namespace fable::multiplayer::combat
{
    enum class PlayerDeathOutcome
    {
        Alive,
        GuildRespawnRequired,
        Invalid,
    };

    // Classifies the post-native result. Resurrection phials have already
    // been consumed by Fable before this value is observed, so any positive
    // health remains native recovery and never enters the Guild fallback.
    [[nodiscard]] inline PlayerDeathOutcome ClassifyPlayerDeath(
        const float currentHealth,
        const float maximumHealth) noexcept
    {
        if (!std::isfinite(currentHealth) ||
            !std::isfinite(maximumHealth) || maximumHealth <= 0.01f)
        {
            return PlayerDeathOutcome::Invalid;
        }
        return currentHealth <= 0.01f
            ? PlayerDeathOutcome::GuildRespawnRequired
            : PlayerDeathOutcome::Alive;
    }
}
