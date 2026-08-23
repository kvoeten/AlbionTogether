#include "PhysicsMovementInputHook.h"

#include <cmath>
#include <cstdio>

namespace fable::game::creature::locomotion
{
    PhysicsWorldPositionMirrorHook* PhysicsWorldPositionMirrorHook::active_ = nullptr;

    bool PhysicsWorldPositionMirrorHook::Install(
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
            "Hook: physics world-position mirroring is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another physics world-position mirror is already active.");
            return false;
        }

        void** slot = nullptr;
        native::PhysicsWorldPositionFunctions::SetWorldPositionPointer original = nullptr;
        if (!native::PhysicsWorldPositionFunctions::ResolveControlledVtableSlot(
                gameModule,
                &slot,
                original))
        {
            diagnostics_.Log(
                "Hook: CTCPhysicsControlled/CTCPhysicsNavigator world-position definitions failed validation.");
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
                "Hook: CTCPhysicsControlled world-position vtable protection change failed.");
            return false;
        }

        original_ = original;
        vtableSlot_ = slot;
        active_ = this;
        *slot = reinterpret_cast<void*>(&PhysicsWorldPositionMirrorHook::Observe);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

        DWORD discarded = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                previousProtection,
                &discarded))
        {
            diagnostics_.Log(
                "Hook: physics world-position mirror installed, but vtable protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "controlled_slot=%p original=%p navigator_rva=0x%08X",
            slot,
            reinterpret_cast<void*>(original_),
            static_cast<unsigned int>(
                native::PhysicsWorldPositionFunctions::
                    PhysicsNavigatorSetWorldPositionRva));
        diagnostics_.Log(
            "Hook: diagnostic Hero CTCPhysicsControlled world-position mirror installed; this is not locomotion input.");
        diagnostics_.Event("PhysicsWorldPositionMirrorReady", detail);
        return true;
#endif
    }

    bool PhysicsWorldPositionMirrorHook::Bind(
        void* sourcePhysicsControlled,
        void* targetPhysicsNavigator)
    {
        if (!IsInstalled() ||
            !native::PhysicsWorldPositionFunctions::ValidateControlledComponent(
                gameModule_,
                sourcePhysicsControlled) ||
            !native::PhysicsWorldPositionFunctions::ValidateNavigatorComponent(
                gameModule_,
                targetPhysicsNavigator))
        {
            return false;
        }

        mirrorCount_.store(0, std::memory_order_release);
        target_.store(targetPhysicsNavigator, std::memory_order_release);
        source_.store(sourcePhysicsControlled, std::memory_order_release);

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "source=%p target=%p",
            sourcePhysicsControlled,
            targetPhysicsNavigator);
        diagnostics_.Log("Diagnostics: Hero-to-NPC physics world-position mirror armed; gait and facing remain NPC-owned.");
        diagnostics_.Event("PhysicsWorldPositionMirrorBound", detail);
        return true;
    }

    void PhysicsWorldPositionMirrorHook::Shutdown() noexcept
    {
        Clear();
        if (active_ == this && vtableSlot_ != nullptr && original_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(vtableSlot_, sizeof(*vtableSlot_), PAGE_READWRITE, &previousProtection))
            {
                if (*vtableSlot_ == reinterpret_cast<void*>(&Observe))
                {
                    *vtableSlot_ = reinterpret_cast<void*>(original_);
                    FlushInstructionCache(GetCurrentProcess(), vtableSlot_, sizeof(*vtableSlot_));
                }
                DWORD discarded = 0;
                VirtualProtect(vtableSlot_, sizeof(*vtableSlot_), previousProtection, &discarded);
            }
            active_ = nullptr;
        }
        original_ = nullptr;
        vtableSlot_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void PhysicsWorldPositionMirrorHook::Clear() noexcept
    {
        source_.store(nullptr, std::memory_order_release);
        target_.store(nullptr, std::memory_order_release);
    }

    bool PhysicsWorldPositionMirrorHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    bool PhysicsWorldPositionMirrorHook::IsBound() const noexcept
    {
        return source_.load(std::memory_order_acquire) != nullptr &&
            target_.load(std::memory_order_acquire) != nullptr;
    }

    unsigned int PhysicsWorldPositionMirrorHook::MirrorCount() const noexcept
    {
        return mirrorCount_.load(std::memory_order_acquire);
    }

    void __fastcall PhysicsWorldPositionMirrorHook::Observe(
        void* component,
        void*,
        const Vector3* worldPosition)
    {
        PhysicsWorldPositionMirrorHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }

        hook->original_(component, worldPosition);
        if (component != hook->source_.load(std::memory_order_acquire) ||
            worldPosition == nullptr)
        {
            return;
        }

        const void* const target = hook->target_.load(std::memory_order_acquire);
        if (target == nullptr)
        {
            return;
        }

        Vector3 captured = {};
        bool readable = false;
        __try
        {
            captured = *worldPosition;
            readable = std::isfinite(captured.x) &&
                std::isfinite(captured.y) &&
                std::isfinite(captured.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (!readable ||
            !native::PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
                hook->gameModule_,
                const_cast<void*>(target),
                captured))
        {
            return;
        }

        const unsigned int ordinal =
            hook->mirrorCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (ordinal <= 4 || ordinal == 10)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u vector=(%.4f,%.4f,%.4f) source=%p target=%p thread=%lu",
                ordinal,
                captured.x,
                captured.y,
                captured.z,
                component,
                target,
                static_cast<unsigned long>(GetCurrentThreadId()));
            if (ordinal == 1)
            {
                hook->diagnostics_.Log(
                    "Diagnostics: first Hero physics world position mirrored to the NPC.");
            }
            hook->diagnostics_.Event("PhysicsWorldPositionMirrored", detail);
        }
    }
}
