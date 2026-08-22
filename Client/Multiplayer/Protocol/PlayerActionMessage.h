#pragma once

#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/HeroPawn/Abilities/HeroAbility.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class PlayerActionPhase : std::uint8_t
    {
        Intent = 1,
        Perform = 2,
    };

    enum class PlayerActionKind : std::uint8_t
    {
        AbilityRequest = 1,
        // Ranged aiming is a sustained state and will use explicit begin,
        // update, release messages once its native command seam is mapped.
        RangedAim = 2,
        // Draw/stow is an ordered semantic action. The reliable message also
        // carries the final owner-observed carry slots because PlayerState is
        // intentionally lossy and may arrive before or after the animation.
        WeaponTransition = 3,
        // Hero Will actions are executed by CTCSpecialAbilities rather than
        // CThingCreature's ordinary weapon-ability submission path.
        HeroAbility = 4,
    };

    struct PlayerActionMessage final
    {
        PlayerActionPhase phase = PlayerActionPhase::Intent;
        PlayerActionKind kind = PlayerActionKind::AbilityRequest;
        std::uint64_t ownerActorId = 0;
        std::uint64_t actionId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t abilityId = 0;
        game::hero_pawn::abilities::HeroAbilityCommand heroAbilityCommand =
            game::hero_pawn::abilities::HeroAbilityCommand::None;
        std::int32_t heroAbilityProgressionState = -1;
        float charge = 0.0f;
        game::creature::equipment::CreatureWeaponFamily weaponFamily =
            game::creature::equipment::CreatureWeaponFamily::None;
        // Reliable action prerequisite. PlayerState is intentionally lossy,
        // so a one-shot action carries the definitions needed to materialize
        // its native Hero command deterministically.
        game::hero_pawn::equipment::HeroWeaponDefinitions requiredWeapons;
        std::uint32_t requiredMeleeAttachmentSlot = 0;
        std::uint32_t requiredRangedAttachmentSlot = 0;
        // The owning game resolves the concrete native action before this is
        // published. Observers replay that action family rather than treating
        // every input request as an accepted attack.
        std::uint32_t resolvedAnimationId = 0;
        // Player targets use stable actor identity because every process owns
        // a different native Hero pointer/Thing UID for that presentation.
        std::uint64_t targetPlayerActorId = 0;
        std::uint64_t targetThingUid = 0;
        std::string mapName;
        std::string semanticName;
        std::string resolvedActionType;
    };

    bool EncodePlayerActionMessage(
        const PlayerActionMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodePlayerActionMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerActionMessage& message) noexcept;
}
