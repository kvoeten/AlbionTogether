#include "HeroWillAbilityFunctions.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <cstring>

namespace
{
    constexpr std::array<std::uint8_t, 3> kStructuredExceptionPrefix = {
        0x6A, 0xFF, 0x68,
    };
    constexpr std::array<std::uint8_t, 7> kTogglePrefix = {
        0x51, 0x83, 0x7C, 0x24, 0x08, 0x04, 0x56,
    };
    constexpr std::array<std::uint8_t, 7> kCancelPrefix = {
        0x51, 0x8B, 0x44, 0x24, 0x08, 0x48, 0x56,
    };
    constexpr std::array<std::uint8_t, 7> kEligibilityPrefix = {
        0x51, 0x53, 0x8B, 0x5C, 0x24, 0x0C, 0x56,
    };
    constexpr std::array<std::uint8_t, 4> kTurncoatStatePrefix = {
        // The following absolute address is loader-relocated under ASLR.
        0x66, 0x0F, 0x6E, 0x05,
    };
    constexpr std::array<std::uint8_t, 6> kAbilityProgressionGetterPrefix = {
        0x8B, 0x81, 0x88, 0x03, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 10> kAbilityProgressionSetterPrefix = {
        0x8B, 0x81, 0x88, 0x03, 0x00, 0x00, 0x8B, 0x4C, 0x24, 0x08,
    };

    bool Matches(
        const std::uint8_t* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        __try
        {
            return std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::hero_pawn::abilities::native
{
    namespace
    {
        bool ResolveActionComponentType(
            HeroAbility ability,
            entity::native::ThingComponentType& type) noexcept
        {
            using entity::native::ThingComponentType;
            switch (ability)
            {
            case HeroAbility::ForcePush:
            case HeroAbility::Enflame:
            case HeroAbility::Turncoat:
            case HeroAbility::DrainLife:
            case HeroAbility::RaiseDead:
            case HeroAbility::Berserk:
            case HeroAbility::Lightning:
                type = ThingComponentType::HeroWillStandardAction;
                return true;
            case HeroAbility::Time:
                type = ThingComponentType::HeroWillTimeAction;
                return true;
            case HeroAbility::PhysicalShield:
                type = ThingComponentType::HeroWillPhysicalShieldAction;
                return true;
            case HeroAbility::DoubleStrike:
                type = ThingComponentType::HeroWillDoubleStrikeAction;
                return true;
            case HeroAbility::Summon:
                type = ThingComponentType::HeroWillSummonAction;
                return true;
            case HeroAbility::BattleCharge:
            case HeroAbility::AssassinRush:
            case HeroAbility::MultiArrow:
                type = ThingComponentType::HeroWillChargedAction;
                return true;
            case HeroAbility::GhostSword:
                type = ThingComponentType::HeroWillGhostSwordAction;
                return true;
            default:
                return false;
            }
        }
    }

    bool HeroWillAbilityFunctions::ResolveCommand(
        HMODULE gameModule,
        HeroAbilityCommand command,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr || !IsValid(command))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const std::uintptr_t rva = command == HeroAbilityCommand::Use
            ? UseRva
            : command == HeroAbilityCommand::Toggle
                ? ToggleRva
                : CancelRva;
        auto* const candidate = reinterpret_cast<std::uint8_t*>(base + rva);
        const bool valid = command == HeroAbilityCommand::Use
            ? Matches(
                candidate,
                kStructuredExceptionPrefix.data(),
                kStructuredExceptionPrefix.size())
            : command == HeroAbilityCommand::Toggle
                ? Matches(candidate, kTogglePrefix.data(), kTogglePrefix.size())
                : Matches(candidate, kCancelPrefix.data(), kCancelPrefix.size());
        if (!valid)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool HeroWillAbilityFunctions::ResolveEligibility(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + EligibilityRva);
        if (!Matches(
                candidate,
                kEligibilityPrefix.data(),
                kEligibilityPrefix.size()))
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool HeroWillAbilityFunctions::ResolveTurncoatState(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + TurncoatStateRva);
        if (!Matches(
                candidate,
                kTurncoatStatePrefix.data(),
                kTurncoatStatePrefix.size()))
        {
            return false;
        }
        address = candidate;
        return true;
    }

    void* HeroWillAbilityFunctions::FindComponent(
        void* hero,
        HMODULE gameModule) noexcept
    {
        return entity::native::ThingComponentAccess::FindByVtableRva(
            hero, gameModule, ControllerVtableRva);
    }

    void* HeroWillAbilityFunctions::ReadOwner(void* component) noexcept
    {
        if (component == nullptr)
        {
            return nullptr;
        }
        void* owner = nullptr;
        __try
        {
            owner = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(component) + sizeof(void*));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            owner = nullptr;
        }
        return owner;
    }

    void* HeroWillAbilityFunctions::ReadCreature(void* component) noexcept
    {
        if (component == nullptr)
        {
            return nullptr;
        }
        void* creature = nullptr;
        __try
        {
            creature = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(component) + 0x20);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            creature = nullptr;
        }
        return creature;
    }

    bool HeroWillAbilityFunctions::ReadAbilityProgressionState(
        void* component,
        HeroAbility ability,
        int& state) noexcept
    {
        state = -1;
        if (component == nullptr || !IsValid(ability))
        {
            return false;
        }
        void* const owner = ReadOwner(component);
        void* const progression = entity::native::ThingComponentAccess::Find(
            owner, entity::native::ThingComponentType::HeroAbilityInventory);
        if (progression == nullptr)
        {
            return false;
        }
        HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            return false;
        }
        auto* const address = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(module) +
                AbilityProgressionGetterRva);
        if (!Matches(
                address,
                kAbilityProgressionGetterPrefix.data(),
                kAbilityProgressionGetterPrefix.size()))
        {
            return false;
        }
        const auto getter =
            reinterpret_cast<AbilityProgressionGetterPointer>(address);
        __try
        {
            // The retail array reserves slot zero for HeroAbility::None.
            state = getter(progression, static_cast<int>(ability));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = -1;
            return false;
        }
    }

    bool HeroWillAbilityFunctions::ApplyAbilityProgressionState(
        void* component,
        HeroAbility ability,
        int state) noexcept
    {
        if (component == nullptr || !IsValid(ability) || state < 0 ||
            state > 3)
        {
            return false;
        }
        void* const owner = ReadOwner(component);
        void* const progression = entity::native::ThingComponentAccess::Find(
            owner, entity::native::ThingComponentType::HeroAbilityInventory);
        if (progression == nullptr)
        {
            return false;
        }
        HMODULE module = GetModuleHandleW(nullptr);
        if (module == nullptr)
        {
            return false;
        }
        auto* const address = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(module) +
                AbilityProgressionSetterRva);
        if (!Matches(
                address,
                kAbilityProgressionSetterPrefix.data(),
                kAbilityProgressionSetterPrefix.size()))
        {
            return false;
        }
        const auto setter =
            reinterpret_cast<AbilityProgressionSetterPointer>(address);
        __try
        {
            // This is Fable's native setter over the progression record array;
            // slot zero remains reserved for HeroAbility::None.
            setter(progression, static_cast<int>(ability), state);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        int observed = -1;
        return ReadAbilityProgressionState(component, ability, observed) &&
            observed == state;
    }

    bool HeroWillAbilityFunctions::HasActiveAction(
        void* component,
        HeroAbility ability) noexcept
    {
        if (component == nullptr || !IsValid(ability))
        {
            return false;
        }
        entity::native::ThingComponentType type =
            entity::native::ThingComponentType::HeroWillStandardAction;
        if (!ResolveActionComponentType(ability, type))
        {
            return false;
        }
        return entity::native::ThingComponentAccess::Has(
            ReadOwner(component), type);
    }
}
