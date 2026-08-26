#include "AssassinRushPresentationHook.h"

#include "Game/Creature/Locomotion/Native/PhysicsMovementFunctions.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    using fable::game::hero_pawn::abilities::hooks::
        AssassinRushPresentationHook;

    constexpr std::uintptr_t kAssassinRushVtableRva = 0x02AED61C;
    constexpr std::uintptr_t kAssassinRushFrameUpdateRva = 0x01936490;
    constexpr std::size_t kAssassinRushFrameUpdateSlot = 10;
    constexpr std::array<std::uint8_t, 3> kFrameUpdatePrefix = {
        0x6A, 0xFF, 0x68,
    };

    thread_local unsigned int g_remoteRushFrameDepth = 0;
    thread_local void* g_remoteRushComponent = nullptr;
    thread_local std::uint64_t g_remoteRushActorId = 0;

    bool BytesMatch(
        const void* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        if (address == nullptr || expected == nullptr || size == 0)
        {
            return false;
        }
        __try
        {
            return std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* ReadOwner(void* component) noexcept
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
}

namespace fable::game::hero_pawn::abilities::hooks
{
    AssassinRushPresentationHook* AssassinRushPresentationHook::active_ =
        nullptr;

    bool AssassinRushPresentationHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        if (IsInstalled())
        {
            return true;
        }
        if (gameModule == nullptr || (active_ != nullptr && active_ != this))
        {
            return false;
        }

#if !defined(_M_IX86)
        diagnostics.Event(
            "AssassinRushPresentationHookRejected",
            "reason=x86-client-required");
        return false;
#else
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const frameUpdate = reinterpret_cast<std::uint8_t*>(
            base + kAssassinRushFrameUpdateRva);
        auto** const frameVtable = reinterpret_cast<void**>(
            base + kAssassinRushVtableRva);
        auto** const controlledVtable = reinterpret_cast<void**>(
            base + creature::locomotion::native::
                PhysicsWorldPositionFunctions::PhysicsControlledVtableRva);
        auto** const navigatorVtable = reinterpret_cast<void**>(
            base + creature::locomotion::native::
                PhysicsWorldPositionFunctions::PhysicsNavigatorVtableRva);
        void** const frameSlot = frameVtable + kAssassinRushFrameUpdateSlot;
        void** const controlledSlot = controlledVtable +
            creature::locomotion::native::PhysicsWorldPositionFunctions::
                SetWorldPositionSlot;
        void** const navigatorSlot = navigatorVtable +
            creature::locomotion::native::PhysicsWorldPositionFunctions::
                SetWorldPositionSlot;

        bool definitionsValid = BytesMatch(
            frameUpdate,
            kFrameUpdatePrefix.data(),
            kFrameUpdatePrefix.size());
        __try
        {
            definitionsValid = definitionsValid &&
                *frameSlot == frameUpdate &&
                *controlledSlot != nullptr &&
                *navigatorSlot != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            definitionsValid = false;
        }
        if (!definitionsValid)
        {
            diagnostics.Event(
                "AssassinRushPresentationHookRejected",
                "reason=native-definitions");
            return false;
        }

        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        originalFrameUpdate_ = reinterpret_cast<FrameUpdatePointer>(
            *frameSlot);
        // Preserve any earlier locomotion observer by chaining the currently
        // installed slot, not by jumping around it to the retail function.
        originalControlledPosition_ =
            reinterpret_cast<SetWorldPositionPointer>(*controlledSlot);
        originalNavigatorPosition_ =
            reinterpret_cast<SetWorldPositionPointer>(*navigatorSlot);
        void* const controlledExpected = *controlledSlot;
        void* const controlledReplacement = reinterpret_cast<void*>(
            &ObserveControlledPosition);
        void* const navigatorExpected = *navigatorSlot;
        void* const navigatorReplacement = reinterpret_cast<void*>(
            &ObserveNavigatorPosition);
        void* const frameExpected = *frameSlot;
        void* const frameReplacement = reinterpret_cast<void*>(
            &ObserveFrameUpdate);
        active_ = this;
        if (!controlledPositionPatch_.Install(
                controlledSlot,
                &controlledExpected,
                sizeof(controlledExpected),
                &controlledReplacement,
                sizeof(controlledReplacement)) ||
            !navigatorPositionPatch_.Install(
                navigatorSlot,
                &navigatorExpected,
                sizeof(navigatorExpected),
                &navigatorReplacement,
                sizeof(navigatorReplacement)) ||
            !frameUpdatePatch_.Install(
                frameSlot,
                &frameExpected,
                sizeof(frameExpected),
                &frameReplacement,
                sizeof(frameReplacement)))
        {
            Shutdown();
            return false;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "frame_slot=%p frame=%p controlled_slot=%p controlled_chain=%p navigator_slot=%p navigator_chain=%p capacity=%zu",
            frameSlot,
            reinterpret_cast<void*>(originalFrameUpdate_),
            controlledSlot,
            reinterpret_cast<void*>(originalControlledPosition_),
            navigatorSlot,
            reinterpret_cast<void*>(originalNavigatorPosition_),
            bindings_.size());
        diagnostics_.Event("AssassinRushPresentationHookReady", detail);
        return true;
#endif
    }

    void AssassinRushPresentationHook::Shutdown() noexcept
    {
        const bool frameRestored = frameUpdatePatch_.Shutdown();
        const bool navigatorRestored = navigatorPositionPatch_.Shutdown();
        const bool controlledRestored = controlledPositionPatch_.Shutdown();
        if (!frameRestored || !navigatorRestored || !controlledRestored)
        {
            diagnostics_.Event(
                "AssassinRushHookUninstallSkipped",
                "target-changed-by-another-hook");
            return;
        }
        if (frameUpdatePatch_.ProtectionRestoreFailed() ||
            navigatorPositionPatch_.ProtectionRestoreFailed() ||
            controlledPositionPatch_.ProtectionRestoreFailed())
        {
            diagnostics_.Event(
                "AssassinRushHookProtectionRestoreWarning",
                "original-bytes-restored");
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
        AcquireSRWLockExclusive(&bindingLock_);
        bindings_ = {};
        ReleaseSRWLockExclusive(&bindingLock_);
        suppressionCount_.store(0, std::memory_order_release);
        originalFrameUpdate_ = nullptr;
        originalControlledPosition_ = nullptr;
        originalNavigatorPosition_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    bool AssassinRushPresentationHook::BindRemoteHero(
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        if (!IsInstalled() || nativeHero == nullptr || actorId == 0)
        {
            return false;
        }
        AcquireSRWLockExclusive(&bindingLock_);
        for (Binding& binding : bindings_)
        {
            if (binding.nativeHero == nativeHero)
            {
                binding.actorId = actorId;
                ReleaseSRWLockExclusive(&bindingLock_);
                return true;
            }
        }
        for (Binding& binding : bindings_)
        {
            if (binding.nativeHero == nullptr)
            {
                binding = {nativeHero, actorId};
                ReleaseSRWLockExclusive(&bindingLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&bindingLock_);
        diagnostics_.Event(
            "AssassinRushPresentationBindRejected",
            "reason=remote-hero-capacity");
        return false;
    }

    void AssassinRushPresentationHook::UnbindRemoteHero(
        void* nativeHero) noexcept
    {
        if (nativeHero == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&bindingLock_);
        for (Binding& binding : bindings_)
        {
            if (binding.nativeHero == nativeHero)
            {
                binding = {};
            }
        }
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    bool AssassinRushPresentationHook::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr &&
            originalFrameUpdate_ != nullptr &&
            originalControlledPosition_ != nullptr &&
            originalNavigatorPosition_ != nullptr &&
            frameUpdatePatch_.IsInstalled() &&
            controlledPositionPatch_.IsInstalled() &&
            navigatorPositionPatch_.IsInstalled();
    }

    void __fastcall AssassinRushPresentationHook::ObserveFrameUpdate(
        void* component,
        void*)
    {
        AssassinRushPresentationHook* const hook = active_;
        if (hook == nullptr || hook->originalFrameUpdate_ == nullptr)
        {
            return;
        }
        void* const owner = ReadOwner(component);
        std::uint64_t actorId = 0;
        if (!hook->IsRemoteHero(owner, actorId))
        {
            hook->originalFrameUpdate_(component);
            return;
        }

        ++g_remoteRushFrameDepth;
        g_remoteRushComponent = component;
        g_remoteRushActorId = actorId;
        hook->originalFrameUpdate_(component);
        g_remoteRushActorId = 0;
        g_remoteRushComponent = nullptr;
        --g_remoteRushFrameDepth;
    }

    void __fastcall AssassinRushPresentationHook::ObserveControlledPosition(
        void* component,
        void*,
        const Vector3* worldPosition)
    {
        AssassinRushPresentationHook* const hook = active_;
        if (hook == nullptr || hook->originalControlledPosition_ == nullptr)
        {
            return;
        }
        if (g_remoteRushFrameDepth != 0)
        {
            hook->ReportSuppressed(component, worldPosition);
            return;
        }
        hook->originalControlledPosition_(component, worldPosition);
    }

    void __fastcall AssassinRushPresentationHook::ObserveNavigatorPosition(
        void* component,
        void*,
        const Vector3* worldPosition)
    {
        AssassinRushPresentationHook* const hook = active_;
        if (hook == nullptr || hook->originalNavigatorPosition_ == nullptr)
        {
            return;
        }
        if (g_remoteRushFrameDepth != 0)
        {
            hook->ReportSuppressed(component, worldPosition);
            return;
        }
        hook->originalNavigatorPosition_(component, worldPosition);
    }

    bool AssassinRushPresentationHook::IsRemoteHero(
        void* nativeHero,
        std::uint64_t& actorId) const noexcept
    {
        actorId = 0;
        if (nativeHero == nullptr)
        {
            return false;
        }
        AcquireSRWLockShared(&bindingLock_);
        for (const Binding& binding : bindings_)
        {
            if (binding.nativeHero == nativeHero)
            {
                actorId = binding.actorId;
                break;
            }
        }
        ReleaseSRWLockShared(&bindingLock_);
        return actorId != 0;
    }

    void AssassinRushPresentationHook::ReportSuppressed(
        void* component,
        const Vector3* worldPosition) noexcept
    {
        const unsigned int ordinal = suppressionCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal > 4 && ordinal != 10 && ordinal != 30)
        {
            return;
        }
        Vector3 position = {};
        bool readable = false;
        __try
        {
            if (worldPosition != nullptr)
            {
                position = *worldPosition;
                readable = std::isfinite(position.x) &&
                    std::isfinite(position.y) &&
                    std::isfinite(position.z);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u actor_id=%llu rush_component=%p physics_component=%p requested_position=(%.4f,%.4f,%.4f) readable=%s thread=%lu route=owner-movement",
            ordinal,
            static_cast<unsigned long long>(g_remoteRushActorId),
            g_remoteRushComponent,
            component,
            position.x,
            position.y,
            position.z,
            readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("AssassinRushTraversalSuppressed", detail);
    }
}
