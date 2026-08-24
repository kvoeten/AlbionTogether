#pragma once

#include "Multiplayer/Protocol/CombatHitMessage.h"

namespace fable::multiplayer::combat
{
    [[nodiscard]] inline bool ShouldSubmitVictimReaction(
        const protocol::CombatHitMessage& result) noexcept
    {
        if (result.phase != protocol::CombatHitPhase::Result ||
            result.healthAfter + 0.001f >= result.healthBefore)
        {
            return false;
        }
        const std::uint32_t suppressed =
            protocol::combat_hit_reaction_flag::Healing |
            protocol::combat_hit_reaction_flag::Blocked |
            protocol::combat_hit_reaction_flag::HitNegated;
        // Killed is deliberately not suppressing. Retail runs the terminal
        // strike response before the creature controller submits Die.
        return (result.reactionFlags & suppressed) == 0;
    }
}
