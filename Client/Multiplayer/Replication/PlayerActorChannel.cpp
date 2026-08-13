#include "PlayerActorChannel.h"

#include <algorithm>
#include <cmath>

namespace
{
    float NormalizeFacing(float facing) noexcept
    {
        if (!std::isfinite(facing))
        {
            return 0.0f;
        }
        facing -= std::floor(facing);
        return facing < 0.0f ? facing + 1.0f : facing;
    }

    float FacingDistance(float first, float second) noexcept
    {
        const float difference = std::fabs(first - second);
        return std::min(difference, 1.0f - difference);
    }

    bool PositionChanged(
        const fable::game::Vector3& first,
        const fable::game::Vector3& second) noexcept
    {
        return first.HorizontalDistanceTo(second) >= 0.001f ||
            std::fabs(first.z - second.z) >= 0.001f;
    }
}

namespace fable::multiplayer::replication
{
    void PlayerActorChannel::OpenOwner(
        std::uint64_t actorId,
        std::uint32_t authorityEpoch,
        PeerRole role,
        const std::string& playerId,
        const std::string& appearanceDefinition,
        const game::hero_pawn::appearance::HeroMorphState& heroMorph,
        const game::hero_pawn::appearance::HeroClothingState& heroClothing,
        const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
        const game::hero_pawn::appearance::HeroAppearanceModifierState&
            heroAppearanceModifiers,
        const std::string& mapName,
        const game::Vector3& position,
        float facing,
        std::uint64_t capturedAtMilliseconds)
    {
        ownerState_ = {};
        ownerState_.sequence = 1;
        ownerState_.authorityEpoch = authorityEpoch;
        ownerState_.actorId = actorId;
        ownerState_.role = role;
        ownerState_.playerId = playerId;
        // The creature definition constructs the remote actor and is therefore
        // part of stable identity. Morph values are optional presentation data
        // that may become available after the actor channel opens.
        ownerState_.appearanceDefinition = appearanceDefinition;
        ownerState_.mapName = mapName;
        ownerState_.position = position;
        ownerState_.facing = NormalizeFacing(facing);
        const bool appearanceReady = !appearanceDefinition.empty() &&
            heroMorph.IsSane() && heroClothing.IsSane() &&
            heroBoneScales.IsSane() &&
            heroAppearanceModifiers.IsSane();
        if (appearanceReady)
        {
            ownerState_.heroMorph = heroMorph;
            ownerState_.heroClothing = heroClothing;
            ownerState_.heroBoneScales = heroBoneScales;
            ownerState_.heroAppearanceModifiers = heroAppearanceModifiers;
        }
        ownerState_.changedProperties = player_property::Identity |
            player_property::Map | player_property::Movement |
            (appearanceReady ? player_property::Appearance : 0u);
        dirtyProperties_ = ownerState_.changedProperties;
        lastOwnerCaptureAt_ = capturedAtMilliseconds;
        ownerOpen_ = true;
    }

    bool PlayerActorChannel::CaptureOwnerMovement(
        const std::string& mapName,
        const game::Vector3& position,
        float facing,
        std::uint64_t capturedAtMilliseconds)
    {
        if (!ownerOpen_ || !std::isfinite(position.x) ||
            !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            return false;
        }

        const float normalizedFacing = NormalizeFacing(facing);
        const bool positionChanged = PositionChanged(ownerState_.position, position);
        const bool facingChanged =
            FacingDistance(ownerState_.facing, normalizedFacing) >= 0.0005f;
        const bool mapChanged = ownerState_.mapName != mapName;
        const bool wasMoving = ownerState_.moving;

        game::Vector3 velocity = {};
        if (positionChanged && capturedAtMilliseconds > lastOwnerCaptureAt_)
        {
            const float seconds = static_cast<float>(
                capturedAtMilliseconds - lastOwnerCaptureAt_) / 1000.0f;
            if (seconds >= 0.001f && seconds <= 0.25f)
            {
                velocity.x = (position.x - ownerState_.position.x) / seconds;
                velocity.y = (position.y - ownerState_.position.y) / seconds;
                velocity.z = (position.z - ownerState_.position.z) / seconds;
            }
        }

        ownerState_.mapName = mapName;
        ownerState_.position = position;
        ownerState_.velocity = velocity;
        ownerState_.facing = normalizedFacing;
        ownerState_.moving = positionChanged;
        lastOwnerCaptureAt_ = capturedAtMilliseconds;

        std::uint32_t changed = 0;
        if (mapChanged)
        {
            changed |= player_property::Map | player_property::Movement;
        }
        if (positionChanged || facingChanged || wasMoving != ownerState_.moving)
        {
            changed |= player_property::Movement;
        }
        if (changed == 0)
        {
            return false;
        }

        ++ownerState_.sequence;
        dirtyProperties_ |= changed;
        return true;
    }

    bool PlayerActorChannel::CaptureOwnerAppearance(
        const std::string& appearanceDefinition,
        const game::hero_pawn::appearance::HeroMorphState& heroMorph,
        const game::hero_pawn::appearance::HeroClothingState& heroClothing,
        const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
        const game::hero_pawn::appearance::HeroAppearanceModifierState&
            heroAppearanceModifiers)
    {
        if (!ownerOpen_ || appearanceDefinition.empty() ||
            !heroMorph.IsSane() || !heroClothing.IsSane() ||
            !heroBoneScales.IsSane() ||
            !heroAppearanceModifiers.IsSane())
        {
            return false;
        }
        const bool definitionChanged =
            ownerState_.appearanceDefinition != appearanceDefinition;
        if (!definitionChanged &&
            ownerState_.heroMorph.Equals(heroMorph) &&
            ownerState_.heroClothing.Equals(heroClothing) &&
            ownerState_.heroBoneScales.Equals(heroBoneScales) &&
            ownerState_.heroAppearanceModifiers.Equals(heroAppearanceModifiers))
        {
            return true;
        }
        ownerState_.appearanceDefinition = appearanceDefinition;
        ownerState_.heroMorph = heroMorph;
        ownerState_.heroClothing = heroClothing;
        ownerState_.heroBoneScales = heroBoneScales;
        ownerState_.heroAppearanceModifiers = heroAppearanceModifiers;
        ++ownerState_.sequence;
        dirtyProperties_ |= player_property::Appearance |
            (definitionChanged ? player_property::Identity : 0u);
        return true;
    }

    bool PlayerActorChannel::TakeDirtyUpdate(PlayerState& update)
    {
        if (!ownerOpen_ || dirtyProperties_ == 0)
        {
            return false;
        }
        update = ownerState_;
        update.changedProperties = dirtyProperties_;
        dirtyProperties_ = 0;
        return true;
    }

    bool PlayerActorChannel::ApplyRemoteUpdate(const PlayerState& update)
    {
        const std::uint32_t changed = update.changedProperties & player_property::All;
        if (changed == 0 || update.actorId == 0 || update.authorityEpoch == 0)
        {
            return false;
        }
        if (remoteOpen_ && update.authorityEpoch < remoteState_.authorityEpoch)
        {
            return false;
        }

        const bool newAuthority = !remoteOpen_ ||
            update.actorId != remoteState_.actorId ||
            update.authorityEpoch > remoteState_.authorityEpoch;
        constexpr std::uint32_t requiredBaseline = player_property::Identity |
            player_property::Map | player_property::Movement;
        if (newAuthority && (changed & requiredBaseline) != requiredBaseline)
        {
            return false;
        }
        if (newAuthority)
        {
            remoteState_ = {};
        }

        if ((changed & player_property::Identity) != 0)
        {
            if (update.playerId.empty() || update.appearanceDefinition.empty())
            {
                return false;
            }
            remoteState_.actorId = update.actorId;
            remoteState_.authorityEpoch = update.authorityEpoch;
            remoteState_.role = update.role;
            remoteState_.playerId = update.playerId;
            remoteState_.appearanceDefinition = update.appearanceDefinition;
        }
        if ((changed & player_property::Map) != 0)
        {
            remoteState_.mapName = update.mapName;
        }
        if ((changed & player_property::Appearance) != 0)
        {
            remoteState_.heroMorph = update.heroMorph;
            remoteState_.heroClothing = update.heroClothing;
            remoteState_.heroBoneScales = update.heroBoneScales;
            remoteState_.heroAppearanceModifiers =
                update.heroAppearanceModifiers;
        }
        if ((changed & player_property::Movement) != 0)
        {
            remoteState_.position = update.position;
            remoteState_.velocity = update.velocity;
            remoteState_.facing = NormalizeFacing(update.facing);
            remoteState_.moving = update.moving;
        }
        remoteState_.sequence = update.sequence;
        remoteState_.changedProperties = changed;
        remoteOpen_ = true;
        return true;
    }

    const PlayerState* PlayerActorChannel::RemoteState() const noexcept
    {
        return remoteOpen_ ? &remoteState_ : nullptr;
    }

    void PlayerActorChannel::Close() noexcept
    {
        ownerState_ = {};
        remoteState_ = {};
        dirtyProperties_ = 0;
        lastOwnerCaptureAt_ = 0;
        ownerOpen_ = false;
        remoteOpen_ = false;
    }

    bool PlayerActorChannel::IsOpen() const noexcept
    {
        return ownerOpen_;
    }
}
