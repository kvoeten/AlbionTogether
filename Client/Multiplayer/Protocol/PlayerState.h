#pragma once

#include "Game/HeroPawn/Appearance/HeroAppearanceState.h"
#include "Game/HeroPawn/Appearance/HeroClothingState.h"
#include "Game/HeroPawn/Appearance/HeroMorphState.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/Math/Vector3.h"
#include "Multiplayer/Protocol/SessionTime.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer
{
    enum class PeerRole : std::uint8_t
    {
        Host = 1,
        Guest = 2,
    };

    namespace player_property
    {
        inline constexpr std::uint32_t Identity = 1u << 0;
        inline constexpr std::uint32_t Map = 1u << 1;
        inline constexpr std::uint32_t Appearance = 1u << 2;
        inline constexpr std::uint32_t Movement = 1u << 3;
        inline constexpr std::uint32_t Retired = 1u << 4;
        inline constexpr std::uint32_t Equipment = 1u << 5;
        inline constexpr std::uint32_t All =
            Identity | Map | Appearance | Movement | Retired | Equipment;
    }

    struct PlayerState final
    {
        std::uint32_t sequence = 0;
        std::uint32_t changedProperties = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        // The owner capture instant on the shared session timeline. The
        // transport also materializes it as a receiver-local monotonic tick
        // so presentation can interpolate source cadence rather than packet
        // arrival jitter.
        protocol::SessionTimeMs movementSampleTimeMs =
            protocol::SessionTimeUnset;
        std::uint64_t movementSampleAt = 0;
        std::uint64_t actorId = 0;
        PeerRole role = PeerRole::Guest;
        bool moving = false;
        game::Vector3 position = {};
        game::Vector3 velocity = {};
        float facing = 0.0f;
        // Horizontal angular velocity in normalized turns per second. Keeping
        // this beside linear velocity lets the receiver extrapolate the
        // complete planar transform and derive animation state from motion.
        float angularVelocity = 0.0f;
        std::uint16_t mapId = 0;
        std::string playerId;
        std::string mapName;
        std::string appearanceDefinition;
        game::hero_pawn::appearance::HeroMorphState heroMorph = {};
        game::hero_pawn::appearance::HeroClothingState heroClothing = {};
        game::hero_pawn::appearance::HeroBoneScaleState heroBoneScales = {};
        game::hero_pawn::appearance::HeroAppearanceModifierState
            heroAppearanceModifiers = {};
        game::hero_pawn::equipment::HeroEquipmentState heroEquipment = {};
    };
}
