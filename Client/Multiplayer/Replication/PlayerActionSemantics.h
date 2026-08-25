#pragma once

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

    [[nodiscard]] inline bool IsReplicatedAction(
        const char* actionType) noexcept
    {
        return actionType != nullptr &&
            (std::strstr(actionType, "InterruptableMidAttack") != nullptr ||
                std::strstr(actionType, "InterruptableNearAttack") != nullptr ||
                IsRangedAimStart(actionType) ||
                IsRangedFire(actionType) ||
                IsWeaponTransition(actionType));
    }
}
