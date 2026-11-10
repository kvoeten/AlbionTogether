#include "CreatureActionInspector.h"

#include <array>
#include <cstring>

namespace fable::game::creature::actions::native
{
    bool CreatureActionInspector::DescribeType(
        void* const action,
        char* const name,
        const std::size_t capacity) noexcept
    {
        if (name == nullptr || capacity == 0)
        {
            return false;
        }
        name[0] = '\0';
        if (action == nullptr)
        {
            return false;
        }

        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(action);
            const auto* const locator =
                reinterpret_cast<const std::uint8_t*>(vtable[-1]);
            const auto* const typeDescriptor =
                *reinterpret_cast<const std::uint8_t* const*>(locator + 0x0C);
            const char* rawName =
                reinterpret_cast<const char*>(typeDescriptor + 0x08);
            if (rawName[0] == '.' && rawName[1] == '?' &&
                rawName[2] == 'A' &&
                (rawName[3] == 'V' || rawName[3] == 'U'))
            {
                rawName += 4;
            }

            std::size_t length = 0;
            while (length + 1 < capacity && rawName[length] != '\0')
            {
                if (rawName[length] == '@' && rawName[length + 1] == '@')
                {
                    break;
                }
                name[length] = rawName[length];
                ++length;
            }
            name[length] = '\0';
            valid = length != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            name[0] = '\0';
            valid = false;
        }
        return valid;
    }

    std::uint32_t CreatureActionInspector::AnimationId(
        void* const action) noexcept
    {
        if (action == nullptr)
        {
            return 0;
        }
        std::int32_t animationId = 0;
        __try
        {
            void* const resource = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(action) + 0x74);
            if (resource != nullptr)
            {
                animationId = *static_cast<const std::int32_t*>(resource);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            animationId = 0;
        }
        return animationId > 0 && animationId <= 0xFFFF
            ? static_cast<std::uint32_t>(animationId)
            : 0;
    }

    void* CreatureActionInspector::AttackTarget(
        HMODULE gameModule,
        void* const action,
        const char* const actionType) noexcept
    {
        // CCreatureAction_InterruptableMidAttack and its concrete variants
        // derive from the verified attack base constructed at retail RVA
        // 0x017A70E0. That base owns its target as a weak pointer at +0xB4.
        if (gameModule == nullptr || action == nullptr ||
            actionType == nullptr ||
            std::strstr(actionType, "InterruptableMidAttack") == nullptr)
        {
            return nullptr;
        }
        constexpr std::uintptr_t WeakPointerGetRva = 0x012E6EA0;
        constexpr std::size_t TargetWeakPointerOffset = 0xB4;
        constexpr std::array<std::uint8_t, 6> ExpectedPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
        };
        void* target = nullptr;
        __try
        {
            auto* const getterAddress = reinterpret_cast<std::uint8_t*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                WeakPointerGetRva);
            if (std::memcmp(
                    getterAddress,
                    ExpectedPrefix.data(),
                    ExpectedPrefix.size()) != 0)
            {
                return nullptr;
            }
            using WeakPointerGet = void* (__thiscall*)(void* weakPointer);
            target = reinterpret_cast<WeakPointerGet>(getterAddress)(
                static_cast<std::uint8_t*>(action) +
                    TargetWeakPointerOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            target = nullptr;
        }
        return target;
    }

    bool CreatureActionInspector::MayCarryAnimation(
        const char* const actionType) noexcept
    {
        if (actionType == nullptr || actionType[0] == '\0')
        {
            return false;
        }
        constexpr const char* markers[] = {
            "Animation", "Attack", "Combat", "Strike", "Weapon",
            "Projectile", "Block", "Parry", "Dodge", "Hit", "Damage",
            "Response", "Unsheathe", "Sheathe", "Cast", "Spell",
            "Ability", "Expression",
        };
        for (const char* const marker : markers)
        {
            if (std::strstr(actionType, marker) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void* CreatureActionInspector::Owner(void* const action) noexcept
    {
        void* creature = nullptr;
        __try
        {
            if (action != nullptr)
            {
                void* const countedPointer = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(action) + 0x0C);
                if (countedPointer != nullptr)
                {
                    creature = *reinterpret_cast<void**>(countedPointer);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            creature = nullptr;
        }
        return creature;
    }

    void* CreatureActionInspector::ActiveAction(
        void* const creature) noexcept
    {
        void* action = nullptr;
        __try
        {
            if (creature != nullptr)
            {
                action = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) + 0x120);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            action = nullptr;
        }
        return action;
    }

    bool CreatureActionInspector::IsFinished(void* const action) noexcept
    {
        if (action == nullptr)
        {
            return false;
        }
        bool finished = false;
        __try
        {
            finished = *reinterpret_cast<const std::uint8_t*>(
                static_cast<const std::uint8_t*>(action) + 0x61) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            finished = false;
        }
        return finished;
    }
}
