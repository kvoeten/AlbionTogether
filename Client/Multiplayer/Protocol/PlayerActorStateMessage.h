#pragma once

#include "Game/HeroPawn/Appearance/HeroAppearanceState.h"
#include "Game/HeroPawn/Appearance/HeroClothingState.h"
#include "Game/HeroPawn/Appearance/HeroMorphState.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/Math/Vector3.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class PlayerActorStateOperation : std::uint8_t
    {
        Construct = 1,
        ComponentDelta = 2,
        MapTransition = 3,
        Retire = 4,
    };

    namespace player_actor_state_flag
    {
        // A changed component is included in a ComponentDelta. Its Present bit
        // then states whether the component remains available after the delta.
        inline constexpr std::uint8_t AppearanceChanged = 1u << 0;
        inline constexpr std::uint8_t EquipmentChanged = 1u << 1;
        inline constexpr std::uint8_t AppearancePresent = 1u << 2;
        inline constexpr std::uint8_t EquipmentPresent = 1u << 3;
        inline constexpr std::uint8_t All = AppearanceChanged |
            EquipmentChanged | AppearancePresent | EquipmentPresent;
    }

    // Reliable actor lifecycle state. Movement samples intentionally remain on
    // the movement transport; this message establishes the actor/component
    // generation that those lossy samples and reliable actions reference.
    struct PlayerActorStateMessage final
    {
        PlayerActorStateOperation operation =
            PlayerActorStateOperation::Construct;
        std::uint8_t componentFlags = 0;
        std::uint64_t actorId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t structuralRevision = 0;
        PeerRole role = PeerRole::Guest;
        std::uint16_t mapId = 0;
        game::Vector3 initialPosition = {};
        float initialFacing = 0.0f;
        std::string playerId;
        std::string mapName;
        std::string appearanceDefinition;
        game::hero_pawn::appearance::HeroMorphState heroMorph = {};
        game::hero_pawn::appearance::HeroClothingState heroClothing = {};
        game::hero_pawn::appearance::HeroBoneScaleState heroBoneScales = {};
        game::hero_pawn::appearance::HeroAppearanceModifierState
            heroAppearanceModifiers = {};
        game::hero_pawn::equipment::HeroEquipmentState heroEquipment = {};

        [[nodiscard]] bool AppearanceChanged() const noexcept
        {
            return (componentFlags & player_actor_state_flag::
                AppearanceChanged) != 0;
        }

        [[nodiscard]] bool EquipmentChanged() const noexcept
        {
            return (componentFlags & player_actor_state_flag::
                EquipmentChanged) != 0;
        }

        [[nodiscard]] bool AppearancePresent() const noexcept
        {
            return (componentFlags & player_actor_state_flag::
                AppearancePresent) != 0;
        }

        [[nodiscard]] bool EquipmentPresent() const noexcept
        {
            return (componentFlags & player_actor_state_flag::
                EquipmentPresent) != 0;
        }
    };
}
