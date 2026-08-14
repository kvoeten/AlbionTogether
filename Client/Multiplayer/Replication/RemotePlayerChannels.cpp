#include "RemotePlayerChannels.h"

#include <cmath>

namespace
{
    bool IsNewerSequence(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return current == 0 ||
            static_cast<std::int32_t>(candidate - current) > 0;
    }

    float NormalizeFacing(float facing) noexcept
    {
        if (!std::isfinite(facing))
        {
            return 0.0f;
        }
        facing -= std::floor(facing);
        return facing < 0.0f ? facing + 1.0f : facing;
    }
}

namespace fable::multiplayer::replication
{
    bool RemotePlayerChannels::Apply(
        const PlayerState& update,
        std::uint64_t receivedAt)
    {
        const std::uint32_t changed =
            update.changedProperties & player_property::All;
        if (changed == 0 || update.actorId == 0 ||
            update.authorityEpoch == 0)
        {
            return false;
        }
        if ((changed & player_property::Retired) != 0)
        {
            channels_.erase(update.actorId);
            return true;
        }
        const auto existing = channels_.find(update.actorId);
        const bool hasExisting = existing != channels_.end();
        if (hasExisting &&
            update.authorityEpoch < existing->second.state.authorityEpoch)
        {
            return false;
        }
        if (hasExisting && update.sequence != existing->second.state.sequence &&
            update.authorityEpoch == existing->second.state.authorityEpoch &&
            !IsNewerSequence(update.sequence, existing->second.state.sequence))
        {
            return false;
        }
        const bool newAuthority = !hasExisting ||
            update.authorityEpoch > existing->second.state.authorityEpoch;
        constexpr std::uint32_t requiredBaseline = player_property::Identity |
            player_property::Map | player_property::Movement;
        if (newAuthority && (changed & requiredBaseline) != requiredBaseline)
        {
            return false;
        }

        RemotePlayerSnapshot& snapshot = channels_[update.actorId];
        if (newAuthority)
        {
            snapshot = {};
        }
        PlayerState& state = snapshot.state;
        if ((changed & player_property::Identity) != 0)
        {
            if (update.playerId.empty() || update.appearanceDefinition.empty())
            {
                if (newAuthority)
                {
                    channels_.erase(update.actorId);
                }
                return false;
            }
            state.actorId = update.actorId;
            state.authorityEpoch = update.authorityEpoch;
            state.role = update.role;
            state.playerId = update.playerId;
            state.appearanceDefinition = update.appearanceDefinition;
        }
        if ((changed & player_property::Map) != 0)
        {
            state.mapName = update.mapName;
        }
        if ((changed & player_property::Appearance) != 0)
        {
            state.heroMorph = update.heroMorph;
            state.heroClothing = update.heroClothing;
            state.heroBoneScales = update.heroBoneScales;
            state.heroAppearanceModifiers = update.heroAppearanceModifiers;
        }
        if ((changed & player_property::Movement) != 0)
        {
            state.position = update.position;
            state.velocity = update.velocity;
            state.facing = NormalizeFacing(update.facing);
            state.angularVelocity = std::isfinite(update.angularVelocity)
                ? update.angularVelocity
                : 0.0f;
            state.moving = update.moving;
        }
        state.sequence = update.sequence;
        state.changedProperties = changed;
        snapshot.receivedAt = receivedAt;
        return true;
    }

    std::vector<RemotePlayerSnapshot> RemotePlayerChannels::Snapshots() const
    {
        std::vector<RemotePlayerSnapshot> result;
        result.reserve(channels_.size());
        for (const auto& [actorId, snapshot] : channels_)
        {
            (void)actorId;
            result.push_back(snapshot);
        }
        return result;
    }

    void RemotePlayerChannels::Remove(std::uint64_t actorId) noexcept
    {
        channels_.erase(actorId);
    }

    void RemotePlayerChannels::Clear() noexcept
    {
        channels_.clear();
    }

    std::size_t RemotePlayerChannels::Size() const noexcept
    {
        return channels_.size();
    }
}
