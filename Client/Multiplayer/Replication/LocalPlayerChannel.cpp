#include "LocalPlayerChannel.h"

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

    float SignedFacingDelta(float from, float to) noexcept
    {
        float delta = to - from;
        if (delta > 0.5f)
        {
            delta -= 1.0f;
        }
        else if (delta < -0.5f)
        {
            delta += 1.0f;
        }
        return delta;
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
    void LocalPlayerChannel::Open(
        std::uint64_t actorId,
        std::uint32_t authorityEpoch,
        PeerRole role,
        const std::string& playerId,
        const std::string& appearanceDefinition,
        const game::hero_pawn::appearance::HeroMorphState& heroMorph,
        const game::hero_pawn::appearance::HeroClothingState& heroClothing,
        const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
        const game::hero_pawn::appearance::HeroAppearanceModifierState& modifiers,
        const game::hero_pawn::equipment::HeroEquipmentState& heroEquipment,
        const std::string& mapName,
        std::uint16_t mapId,
        const game::Vector3& position,
        float facing,
        std::uint64_t capturedAtMilliseconds,
        const protocol::SessionTimeMs capturedAtSessionTime)
    {
        // Every successful native Hero bind is a new actor incarnation. This
        // includes re-entering the same map after a level reload: old movement,
        // actions, and vitals must never target the replacement native actor.
        ++actorGeneration_;
        if (actorGeneration_ == 0)
        {
            actorGeneration_ = 1;
        }
        ++mapEpoch_;
        if (mapEpoch_ == 0)
        {
            mapEpoch_ = 1;
        }
        state_ = {};
        actorId_ = actorId;
        authorityEpoch_ = authorityEpoch;
        state_.sequence = 1;
        state_.authorityEpoch = authorityEpoch;
        state_.actorGeneration = actorGeneration_;
        state_.mapEpoch = mapEpoch_;
        state_.actorId = actorId;
        state_.role = role;
        state_.playerId = playerId;
        state_.appearanceDefinition = appearanceDefinition;
        state_.mapName = mapName;
        state_.mapId = mapId;
        state_.position = position;
        state_.facing = NormalizeFacing(facing);
        state_.movementSampleTimeMs = capturedAtSessionTime;
        state_.movementSampleAt = capturedAtMilliseconds;
        const bool appearanceReady = !appearanceDefinition.empty() &&
            heroMorph.IsSane() && heroClothing.IsSane() &&
            heroBoneScales.IsSane() && modifiers.IsSane();
        if (appearanceReady)
        {
            state_.heroMorph = heroMorph;
            state_.heroClothing = heroClothing;
            state_.heroBoneScales = heroBoneScales;
            state_.heroAppearanceModifiers = modifiers;
        }
        const bool equipmentReady = heroEquipment.IsSane();
        if (equipmentReady)
        {
            state_.heroEquipment = heroEquipment;
        }
        // Structural state is published by PlayerActorStateReplication. This
        // dirty mask is exclusively the replace-in-place movement lane.
        dirtyProperties_ = player_property::Movement;
        lastCaptureAt_ = capturedAtMilliseconds;
        open_ = true;
    }

    bool LocalPlayerChannel::CaptureMovement(
        const std::string& mapName,
        const game::Vector3& position,
        float facing,
        std::uint64_t capturedAtMilliseconds,
        const protocol::SessionTimeMs capturedAtSessionTime)
    {
        if (!open_ || mapName != state_.mapName ||
            !std::isfinite(position.x) ||
            !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            return false;
        }
        const float normalizedFacing = NormalizeFacing(facing);
        const bool positionChanged = PositionChanged(state_.position, position);
        const bool facingChanged =
            FacingDistance(state_.facing, normalizedFacing) >= 0.0005f;
        const bool wasMoving = state_.moving;
        const float previousAngularVelocity = state_.angularVelocity;
        game::Vector3 velocity = {};
        float angularVelocity = 0.0f;
        if (capturedAtMilliseconds > lastCaptureAt_)
        {
            const float seconds = static_cast<float>(
                capturedAtMilliseconds - lastCaptureAt_) / 1000.0f;
            if (seconds >= 0.001f && seconds <= 0.25f)
            {
                if (positionChanged)
                {
                    velocity.x = (position.x - state_.position.x) / seconds;
                    velocity.y = (position.y - state_.position.y) / seconds;
                    velocity.z = (position.z - state_.position.z) / seconds;
                }
                if (facingChanged)
                {
                    angularVelocity = SignedFacingDelta(
                        state_.facing, normalizedFacing) / seconds;
                }
            }
        }
        constexpr float kLinearMotionThresholdSquared = 0.0025f;
        const bool moving = velocity.x * velocity.x +
            velocity.y * velocity.y >= kLinearMotionThresholdSquared;
        state_.position = position;
        state_.velocity = velocity;
        state_.facing = normalizedFacing;
        state_.angularVelocity = angularVelocity;
        state_.moving = moving;
        state_.movementSampleTimeMs = capturedAtSessionTime;
        state_.movementSampleAt = capturedAtMilliseconds;
        lastCaptureAt_ = capturedAtMilliseconds;
        const bool angularVelocityChanged = std::fabs(
            previousAngularVelocity - angularVelocity) >= 0.0005f;
        std::uint32_t changed = 0;
        if (positionChanged || facingChanged || angularVelocityChanged ||
            wasMoving != state_.moving)
        {
            changed |= player_property::Movement;
        }
        if (changed == 0)
        {
            return false;
        }
        ++state_.sequence;
        dirtyProperties_ |= changed;
        return true;
    }

    bool LocalPlayerChannel::CaptureAppearance(
        const std::string& appearanceDefinition,
        const game::hero_pawn::appearance::HeroMorphState& heroMorph,
        const game::hero_pawn::appearance::HeroClothingState& heroClothing,
        const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
        const game::hero_pawn::appearance::HeroAppearanceModifierState& modifiers)
    {
        if (!open_ || appearanceDefinition.empty() || !heroMorph.IsSane() ||
            !heroClothing.IsSane() || !heroBoneScales.IsSane() ||
            !modifiers.IsSane())
        {
            return false;
        }
        const bool definitionChanged =
            state_.appearanceDefinition != appearanceDefinition;
        if (!definitionChanged && state_.heroMorph.Equals(heroMorph) &&
            state_.heroClothing.Equals(heroClothing) &&
            state_.heroBoneScales.Equals(heroBoneScales) &&
            state_.heroAppearanceModifiers.Equals(modifiers))
        {
            return true;
        }
        state_.appearanceDefinition = appearanceDefinition;
        state_.heroMorph = heroMorph;
        state_.heroClothing = heroClothing;
        state_.heroBoneScales = heroBoneScales;
        state_.heroAppearanceModifiers = modifiers;
        ++state_.sequence;
        return true;
    }

    bool LocalPlayerChannel::CaptureEquipment(
        const game::hero_pawn::equipment::HeroEquipmentState& equipment)
    {
        if (!open_ || !equipment.IsSane())
        {
            return false;
        }
        if (state_.heroEquipment.Equals(equipment))
        {
            return true;
        }
        state_.heroEquipment = equipment;
        ++state_.sequence;
        return true;
    }

    bool LocalPlayerChannel::TakeDirtyUpdate(PlayerState& update)
    {
        if (!open_ || dirtyProperties_ == 0)
        {
            return false;
        }
        update = state_;
        update.changedProperties = dirtyProperties_;
        dirtyProperties_ = 0;
        return true;
    }

    void LocalPlayerChannel::Close() noexcept
    {
        state_ = {};
        dirtyProperties_ = 0;
        lastCaptureAt_ = 0;
        open_ = false;
    }

    bool LocalPlayerChannel::IsOpen() const noexcept
    {
        return open_;
    }

    const PlayerState* LocalPlayerChannel::CurrentState() const noexcept
    {
        return open_ ? &state_ : nullptr;
    }
}
