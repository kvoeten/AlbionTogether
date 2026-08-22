#pragma once

#include <cstdint>

namespace fable::game::hero_pawn::abilities
{
    enum class HeroAbility : std::uint32_t
    {
        None = 0,
        ForcePush = 1,
        Time = 2,
        Enflame = 3,
        PhysicalShield = 4,
        Turncoat = 5,
        DrainLife = 6,
        RaiseDead = 7,
        Berserk = 8,
        DoubleStrike = 9,
        Summon = 10,
        Lightning = 11,
        BattleCharge = 12,
        AssassinRush = 13,
        HealLife = 14,
        GhostSword = 15,
        Fireball = 16,
        MultiArrow = 17,
        DivineWrath = 18,
        UnholyPower = 19,
    };

    enum class HeroAbilityCommand : std::uint8_t
    {
        None = 0,
        Use = 1,
        Toggle = 2,
        Cancel = 3,
    };

    [[nodiscard]] constexpr bool IsValid(HeroAbility ability) noexcept
    {
        const auto value = static_cast<std::uint32_t>(ability);
        return value >= 1 && value <= 19;
    }

    [[nodiscard]] constexpr bool IsMultiplayerSupported(
        HeroAbility ability) noexcept
    {
        // Slow Time changes process-local world time. Replaying it on two
        // independent simulations can desynchronize or terminate either
        // client, so FableTogether deliberately removes it.
        return IsValid(ability) && ability != HeroAbility::Time;
    }

    [[nodiscard]] constexpr bool IsValid(
        HeroAbilityCommand command) noexcept
    {
        return command == HeroAbilityCommand::Use ||
            command == HeroAbilityCommand::Toggle ||
            command == HeroAbilityCommand::Cancel;
    }

    [[nodiscard]] constexpr const char* Name(HeroAbility ability) noexcept
    {
        switch (ability)
        {
        case HeroAbility::ForcePush: return "Force Push";
        case HeroAbility::Time: return "Time";
        case HeroAbility::Enflame: return "Enflame";
        case HeroAbility::PhysicalShield: return "Physical Shield";
        case HeroAbility::Turncoat: return "Turncoat";
        case HeroAbility::DrainLife: return "Drain Life";
        case HeroAbility::RaiseDead: return "Raise Dead";
        case HeroAbility::Berserk: return "Berserk";
        case HeroAbility::DoubleStrike: return "Double Strike";
        case HeroAbility::Summon: return "Summon";
        case HeroAbility::Lightning: return "Lightning";
        case HeroAbility::BattleCharge: return "Battle Charge";
        case HeroAbility::AssassinRush: return "Assassin Rush";
        case HeroAbility::HealLife: return "Heal Life";
        case HeroAbility::GhostSword: return "Ghost Sword";
        case HeroAbility::Fireball: return "Fireball";
        case HeroAbility::MultiArrow: return "Multi Arrow";
        case HeroAbility::DivineWrath: return "Divine Wrath";
        case HeroAbility::UnholyPower: return "Unholy Power";
        default: return "Unknown";
        }
    }
}
