#pragma once

#include "Game/Creature/Equipment/CreatureWeaponFamily.h"

#include <cstring>

namespace fable::multiplayer::replication::player_action_semantics
{
    [[nodiscard]] inline bool IsRangedFire(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            std::strstr(actionType, "FireMissileWeapon") != nullptr;
    }

    [[nodiscard]] inline bool IsRangedAimStart(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            std::strstr(actionType, "HeroLoadRangedWeapon") != nullptr;
    }

    [[nodiscard]] inline bool IsWeaponTransition(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            (std::strstr(actionType, "UnsheatheItemFromInventory") != nullptr ||
                std::strstr(actionType, "SheatheItemToInventory") != nullptr);
    }

    [[nodiscard]] inline bool IsUnsheathe(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            std::strstr(actionType, "UnsheatheItemFromInventory") != nullptr;
    }

    [[nodiscard]] inline bool IsExpression(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            std::strstr(actionType, "PerformExpression") != nullptr;
    }

    // Trust the live carrying state for melee versus unarmed attacks. A Hero
    // can own a melee weapon while deliberately fighting with empty hands, so
    // inventory presence must never promote None to Melee. Ranged fire is the
    // sole override because its accepted native action identifies the weapon
    // family even when presentation state is between frames.
    [[nodiscard]] inline game::creature::equipment::CreatureWeaponFamily
        ResolveCapturedWeaponFamily(
            game::creature::equipment::CreatureWeaponFamily liveFamily,
            bool attackCommand,
            const char* resolvedActionType,
            bool hasRangedWeapon) noexcept
    {
        return attackCommand && hasRangedWeapon &&
                IsRangedFire(resolvedActionType)
            ? game::creature::equipment::CreatureWeaponFamily::Ranged
            : liveFamily;
    }

    [[nodiscard]] inline bool IsReplicatedAction(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            (std::strstr(actionType, "InterruptableMidAttack") != nullptr ||
                std::strstr(actionType, "InterruptableNearAttack") != nullptr ||
                IsRangedAimStart(actionType) ||
                IsRangedFire(actionType) ||
                IsWeaponTransition(actionType) ||
                IsExpression(actionType));
    }
}
