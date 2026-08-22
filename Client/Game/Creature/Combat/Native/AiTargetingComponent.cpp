#include "AiTargetingComponent.h"

#include <array>
#include <cstring>

namespace fable::game::creature::combat::native
{
    bool AiTargetingComponent::Validate(
        HMODULE gameModule,
        void* targetingComponent) noexcept
    {
        if (gameModule == nullptr || targetingComponent == nullptr)
        {
            return false;
        }

        constexpr std::array<std::uint8_t, 3> getterPrefix = {
            0x83, 0xC1, 0x25,
        };
        constexpr std::array<std::uint8_t, 3> setterPrefix = {
            0x6A, 0x08, 0xB8,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(
                targetingComponent);
            valid = vtable == reinterpret_cast<void**>(base + VtableRva) &&
                vtable[GetScriptTargetOverrideSlot] ==
                    reinterpret_cast<void*>(
                        base + GetScriptTargetOverrideRva) &&
                vtable[SetScriptTargetOverrideSlot] ==
                    reinterpret_cast<void*>(
                        base + SetScriptTargetOverrideRva) &&
                std::memcmp(
                    vtable[GetScriptTargetOverrideSlot],
                    getterPrefix.data(),
                    getterPrefix.size()) == 0 &&
                std::memcmp(
                    vtable[SetScriptTargetOverrideSlot],
                    setterPrefix.data(),
                    setterPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    void* AiTargetingComponent::GetScriptTargetOverride(
        HMODULE gameModule,
        void* targetingComponent) noexcept
    {
        if (!Validate(gameModule, targetingComponent))
        {
            return nullptr;
        }

        void* target = nullptr;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(
                targetingComponent);
            target = reinterpret_cast<GetTargetPointer>(
                vtable[GetScriptTargetOverrideSlot])(targetingComponent);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            target = nullptr;
        }
        return target;
    }

    bool AiTargetingComponent::SetScriptTargetOverride(
        HMODULE gameModule,
        void* targetingComponent,
        void* target) noexcept
    {
        if (!Validate(gameModule, targetingComponent))
        {
            return false;
        }

        bool invoked = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(
                targetingComponent);
            reinterpret_cast<SetTargetPointer>(
                vtable[SetScriptTargetOverrideSlot])(
                    targetingComponent, target);
            invoked = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        return invoked &&
            GetScriptTargetOverride(gameModule, targetingComponent) == target;
    }
}
