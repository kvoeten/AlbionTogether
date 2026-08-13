#include "CreatureFacingInputRouterHook.h"

#include "Game/Creature/Locomotion/Native/PhysicsMovementFunctions.h"
#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"
#include "Game/Creature/Look/Native/CreatureLookFunctions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace fable::game::creature::look
{
    CreatureFacingInputRouterHook* CreatureFacingInputRouterHook::active_ = nullptr;

    bool CreatureFacingInputRouterHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: movement-facing routing is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another movement-facing router is already active.");
            return false;
        }

        void** slot = nullptr;
        ::fable::game::creature::native::CreatureFrameFunctions::UpdateFramePointer original =
            nullptr;
        if (!::fable::game::creature::native::CreatureFrameFunctions::ResolveCreatureUpdateFrameSlot(
                gameModule,
                &slot,
                original) ||
            !native::CreatureLookFunctions::ValidateDefinitions(gameModule))
        {
            diagnostics_.Log(
                "Hook: creature frame or look/facing definitions failed validation.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                PAGE_READWRITE,
                &previousProtection))
        {
            diagnostics_.Log(
                "Hook: creature frame vtable protection change failed.");
            return false;
        }

        original_ = original;
        vtableSlot_ = slot;
        active_ = this;
        *slot = reinterpret_cast<void*>(
            &CreatureFacingInputRouterHook::ObserveCreatureUpdate);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

        DWORD discarded = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                previousProtection,
                &discarded))
        {
            diagnostics_.Log(
                "Hook: movement-facing router installed, but vtable protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "slot=%p original=%p creature_update_rva=0x%08X navigator_set_facing_rva=0x%08X",
            slot,
            reinterpret_cast<void*>(original_),
            static_cast<unsigned int>(
                ::fable::game::creature::native::CreatureFrameFunctions::CreatureUpdateFrameRva),
            static_cast<unsigned int>(
                native::CreatureLookFunctions::SetNavigatorFacingRva));
        diagnostics_.Event("CreatureFacingInputRouterReady", detail);
        return true;
#endif
    }

    bool CreatureFacingInputRouterHook::Bind(
        void* targetCreature,
        void* targetPhysicsNavigator,
        ReplicatedMovementProvider provider,
        void* providerContext)
    {
        if (!IsInstalled() ||
            !::fable::game::creature::native::CreatureFrameFunctions::ValidateCreature(
                gameModule_,
                targetCreature) ||
            !native::CreatureLookFunctions::ValidateNavigator(
                gameModule_,
                targetPhysicsNavigator))
        {
            return false;
        }

        AcquireSRWLockExclusive(&bindingLock_);
        Binding* available = nullptr;
        for (Binding& binding : bindings_)
        {
            if (binding.creature == targetCreature)
            {
                available = &binding;
                break;
            }
            if (available == nullptr && binding.creature == nullptr)
            {
                available = &binding;
            }
        }
        if (available == nullptr)
        {
            ReleaseSRWLockExclusive(&bindingLock_);
            diagnostics_.Log("Hook: movement-facing router binding capacity exhausted.");
            return false;
        }
        available->creature = targetCreature;
        available->navigator = targetPhysicsNavigator;
        available->provider = provider;
        available->providerContext = providerContext;
        available->lastFrameAt = 0;
        ReleaseSRWLockExclusive(&bindingLock_);

        char detail[192] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target_creature=%p target_navigator=%p replicated_movement=%s",
            targetCreature,
            targetPhysicsNavigator,
            provider != nullptr ? "true" : "false");
        diagnostics_.Event("CreatureFacingInputRouterBound", detail);
        return true;
    }

    void CreatureFacingInputRouterHook::Clear() noexcept
    {
        AcquireSRWLockExclusive(&bindingLock_);
        for (Binding& binding : bindings_)
        {
            binding = {};
        }
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    void CreatureFacingInputRouterHook::Unbind(void* targetCreature) noexcept
    {
        if (targetCreature == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&bindingLock_);
        for (Binding& binding : bindings_)
        {
            if (binding.creature == targetCreature)
            {
                binding = {};
                break;
            }
        }
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    bool CreatureFacingInputRouterHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    bool CreatureFacingInputRouterHook::IsBound() const noexcept
    {
        AcquireSRWLockShared(&bindingLock_);
        const bool bound = std::any_of(
            bindings_.begin(),
            bindings_.end(),
            [](const Binding& binding)
            {
                return binding.creature != nullptr && binding.navigator != nullptr;
            });
        ReleaseSRWLockShared(&bindingLock_);
        return bound;
    }

    unsigned int CreatureFacingInputRouterHook::RoutedFacingCount() const noexcept
    {
        return routedFacingCount_.load(std::memory_order_acquire);
    }

    bool __fastcall CreatureFacingInputRouterHook::ObserveCreatureUpdate(
        void* creature,
        void*)
    {
        CreatureFacingInputRouterHook* const router = active_;
        if (router == nullptr || router->original_ == nullptr)
        {
            return false;
        }

        Binding activeBinding;
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&router->bindingLock_);
        for (Binding& binding : router->bindings_)
        {
            if (binding.creature == creature)
            {
                activeBinding = binding;
                binding.lastFrameAt = now;
                break;
            }
        }
        ReleaseSRWLockExclusive(&router->bindingLock_);

        ReplicatedMovementInput replicatedInput;
        bool hasReplicatedInput = activeBinding.provider != nullptr &&
            activeBinding.provider(
                activeBinding.providerContext,
                creature,
                replicatedInput);
        bool routedReplicatedMovement = false;
        const bool controlledPhysics = activeBinding.navigator != nullptr &&
            ::fable::game::creature::locomotion::native::
                PhysicsWorldPositionFunctions::ValidateControlledComponent(
                    router->gameModule_,
                    activeBinding.navigator);
        Vector3 appliedFrameMotion = {};
        if (hasReplicatedInput && activeBinding.navigator != nullptr &&
            std::isfinite(replicatedInput.position.x) &&
            std::isfinite(replicatedInput.position.y) &&
            std::isfinite(replicatedInput.position.z) &&
            std::isfinite(replicatedInput.velocity.x) &&
            std::isfinite(replicatedInput.velocity.y) &&
            std::isfinite(replicatedInput.velocity.z))
        {
            Vector3 currentPosition = {};
            bool readable = false;
            __try
            {
                std::memcpy(
                    &currentPosition,
                    static_cast<const std::uint8_t*>(activeBinding.navigator) +
                        ::fable::game::creature::locomotion::native::PhysicsNavigatorFunctions::WorldPositionOffset,
                    sizeof(currentPosition));
                readable = std::isfinite(currentPosition.x) &&
                    std::isfinite(currentPosition.y) &&
                    std::isfinite(currentPosition.z);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                readable = false;
            }

            if (readable)
            {
                const float predictionSeconds = std::clamp(
                    replicatedInput.sampleAgeSeconds,
                    0.0f,
                    0.15f);
                const Vector3 target = {
                    replicatedInput.position.x +
                        replicatedInput.velocity.x * predictionSeconds,
                    replicatedInput.position.y +
                        replicatedInput.velocity.y * predictionSeconds,
                    replicatedInput.position.z +
                        replicatedInput.velocity.z * predictionSeconds,
                };
                const float deltaX = target.x - currentPosition.x;
                const float deltaY = target.y - currentPosition.y;
                const float deltaZ = target.z - currentPosition.z;
                const float distance = std::sqrt(
                    deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

                if (distance >= 8.0f)
                {
                    routedReplicatedMovement = controlledPhysics
                        ? ::fable::game::creature::locomotion::native::
                            PhysicsWorldPositionFunctions::SetControlledWorldPosition(
                                router->gameModule_,
                                activeBinding.navigator,
                                target)
                        : ::fable::game::creature::locomotion::native::
                            PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
                                router->gameModule_,
                                activeBinding.navigator,
                                target);
                    if (routedReplicatedMovement)
                    {
                        appliedFrameMotion = {
                            target.x - currentPosition.x,
                            target.y - currentPosition.y,
                            target.z - currentPosition.z,
                        };
                    }
                }
                else if (distance >= 0.005f)
                {
                    const float frameSeconds = activeBinding.lastFrameAt == 0
                        ? 1.0f / 60.0f
                        : std::clamp(
                            static_cast<float>(now - activeBinding.lastFrameAt) /
                                1000.0f,
                            1.0f / 240.0f,
                            1.0f / 15.0f);
                    const float speed = std::sqrt(
                        replicatedInput.velocity.x * replicatedInput.velocity.x +
                        replicatedInput.velocity.y * replicatedInput.velocity.y +
                        replicatedInput.velocity.z * replicatedInput.velocity.z);
                    const float maximumStep = std::clamp(
                        speed * frameSeconds * 1.25f +
                            (replicatedInput.moving ? 0.01f : 0.025f),
                        0.015f,
                        0.35f);
                    const float step = (std::min)(distance, maximumStep);
                    const float scale = step / distance;
                    const Vector3 desiredPosition = {
                        currentPosition.x + deltaX * scale,
                        currentPosition.y + deltaY * scale,
                        currentPosition.z + deltaZ * scale,
                    };
                    routedReplicatedMovement = controlledPhysics
                        ? ::fable::game::creature::locomotion::native::
                            PhysicsWorldPositionFunctions::SetControlledWorldPosition(
                                router->gameModule_,
                                activeBinding.navigator,
                                desiredPosition)
                        : ::fable::game::creature::locomotion::native::
                            PhysicsNavigatorFunctions::RequestNextPosition(
                                router->gameModule_,
                                activeBinding.navigator,
                                desiredPosition);
                    if (routedReplicatedMovement)
                    {
                        appliedFrameMotion = {
                            desiredPosition.x - currentPosition.x,
                            desiredPosition.y - currentPosition.y,
                            desiredPosition.z - currentPosition.z,
                        };
                    }
                }
            }
        }

        if (controlledPhysics && hasReplicatedInput)
        {
            __try
            {
                auto* const creatureBytes = static_cast<std::uint8_t*>(creature);
                *reinterpret_cast<float*>(
                    creatureBytes +
                    ::fable::game::creature::native::CreatureFrameFunctions::
                        MotionXOffset) = appliedFrameMotion.x;
                *reinterpret_cast<float*>(
                    creatureBytes +
                    ::fable::game::creature::native::CreatureFrameFunctions::
                        MotionYOffset) = appliedFrameMotion.y;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                routedReplicatedMovement = false;
            }
        }

        const bool result = router->original_(creature);
        void* const navigator = activeBinding.navigator;
        if (navigator == nullptr)
        {
            return result;
        }

        float motionX = 0.0f;
        float motionY = 0.0f;
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(creature);
            std::memcpy(
                &motionX,
                bytes + ::fable::game::creature::native::CreatureFrameFunctions::MotionXOffset,
                sizeof(motionX));
            std::memcpy(
                &motionY,
                bytes + ::fable::game::creature::native::CreatureFrameFunctions::MotionYOffset,
                sizeof(motionY));
            readable = std::isfinite(motionX) && std::isfinite(motionY);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        float facing = replicatedInput.facing;
        if (!hasReplicatedInput)
        {
            const float planarSquared = motionX * motionX + motionY * motionY;
            if (!readable || planarSquared <= 0.00000001f)
            {
                return result;
            }
            constexpr float inverseTau = 0.15915494309189533577f;
            facing = std::atan2(motionX, motionY) * inverseTau;
        }
        if (!std::isfinite(facing))
        {
            return result;
        }
        facing -= std::floor(facing);
        if (facing < 0.0f)
        {
            facing += 1.0f;
        }

        const bool applied = native::CreatureLookFunctions::SetNavigatorFacing(
            router->gameModule_,
            navigator,
            facing);
        if (!applied)
        {
            return result;
        }

        const unsigned int ordinal = router->routedFacingCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u target_creature=%p motion=(%.6f,%.6f) normalized_facing=%.6f replicated_movement=%s requested=%s physics=%s thread=%lu",
                ordinal,
                creature,
                motionX,
                motionY,
                facing,
                hasReplicatedInput ? "true" : "false",
                routedReplicatedMovement ? "true" : "false",
                controlledPhysics ? "controlled" : "navigator",
                static_cast<unsigned long>(GetCurrentThreadId()));
            router->diagnostics_.Event("CreatureMovementFacingRouted", detail);
        }
        return result;
    }
}
