#include "CreatureFacingInputRouterHook.h"

#include "Game/Creature/Look/Native/CreatureLookFunctions.h"

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
        void* targetPhysicsNavigator)
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
        targetCreature_ = targetCreature;
        targetNavigator_ = targetPhysicsNavigator;
        routedFacingCount_.store(0, std::memory_order_release);
        ReleaseSRWLockExclusive(&bindingLock_);

        char detail[192] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target_creature=%p target_navigator=%p",
            targetCreature,
            targetPhysicsNavigator);
        diagnostics_.Event("CreatureFacingInputRouterBound", detail);
        return true;
    }

    void CreatureFacingInputRouterHook::Clear() noexcept
    {
        AcquireSRWLockExclusive(&bindingLock_);
        targetCreature_ = nullptr;
        targetNavigator_ = nullptr;
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    bool CreatureFacingInputRouterHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    bool CreatureFacingInputRouterHook::IsBound() const noexcept
    {
        AcquireSRWLockShared(&bindingLock_);
        const bool bound = targetCreature_ != nullptr && targetNavigator_ != nullptr;
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

        const bool result = router->original_(creature);
        AcquireSRWLockShared(&router->bindingLock_);
        if (creature != router->targetCreature_ || router->targetNavigator_ == nullptr)
        {
            ReleaseSRWLockShared(&router->bindingLock_);
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

        const float planarSquared = motionX * motionX + motionY * motionY;
        if (!readable || planarSquared <= 0.00000001f)
        {
            ReleaseSRWLockShared(&router->bindingLock_);
            return result;
        }

        constexpr float inverseTau = 0.15915494309189533577f;
        float facing = std::atan2(motionX, motionY) * inverseTau;
        if (facing < 0.0f)
        {
            facing += 1.0f;
        }
        else if (facing >= 1.0f)
        {
            facing -= std::floor(facing);
        }

        const bool applied = native::CreatureLookFunctions::SetNavigatorFacing(
            router->gameModule_,
            router->targetNavigator_,
            facing);
        ReleaseSRWLockShared(&router->bindingLock_);
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
                "ordinal=%u target_creature=%p motion=(%.6f,%.6f) normalized_facing=%.6f thread=%lu",
                ordinal,
                creature,
                motionX,
                motionY,
                facing,
                static_cast<unsigned long>(GetCurrentThreadId()));
            router->diagnostics_.Event("CreatureMovementFacingRouted", detail);
        }
        return result;
    }
}
