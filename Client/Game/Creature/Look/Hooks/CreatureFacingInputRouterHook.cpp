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

        void* const expected = reinterpret_cast<void*>(original);
        void* const replacement = reinterpret_cast<void*>(
            &CreatureFacingInputRouterHook::ObserveCreatureUpdate);
        if (!vtablePatch_.Install(
                slot, &expected, sizeof(expected), &replacement, sizeof(replacement)))
        {
            diagnostics_.Log(
                "Hook: creature frame vtable patch installation failed.");
            return false;
        }

        original_ = original;
        active_ = this;

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
        auto& available = bindings_[targetCreature];
        if (available == nullptr)
        {
            available = std::make_shared<Binding>();
        }
        available->navigator.store(
            targetPhysicsNavigator, std::memory_order_release);
        available->providerContext.store(
            providerContext, std::memory_order_release);
        available->provider.store(provider, std::memory_order_release);
        available->lastFrameAt.store(0, std::memory_order_release);
        available->lastNativeMovementReportAt.store(
            0, std::memory_order_release);
        available->lastBackgroundMovementReportAt.store(
            0, std::memory_order_release);
        available->nativeMoving.store(false, std::memory_order_release);
        available->backgroundMoving.store(false, std::memory_order_release);
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
        bindings_.clear();
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    void CreatureFacingInputRouterHook::Shutdown() noexcept
    {
        if (vtablePatch_.IsInstalled())
        {
            if (!vtablePatch_.Shutdown())
            {
                diagnostics_.Log(
                    "Hook: movement-facing router shutdown skipped because its vtable slot changed.");
                return;
            }
        }
        if (vtablePatch_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: movement-facing router bytes restored, but vtable protection restoration failed.");
        }
        Clear();
        SetFrameObserver(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void CreatureFacingInputRouterHook::Unbind(void* targetCreature) noexcept
    {
        if (targetCreature == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&bindingLock_);
        bindings_.erase(targetCreature);
        ReleaseSRWLockExclusive(&bindingLock_);
    }

    std::shared_ptr<CreatureFacingInputRouterHook::Binding>
        CreatureFacingInputRouterHook::FindBinding(
            void* creature) const noexcept
    {
        std::shared_ptr<Binding> result;
        AcquireSRWLockShared(&bindingLock_);
        const auto found = bindings_.find(creature);
        if (found != bindings_.end())
        {
            result = found->second;
        }
        ReleaseSRWLockShared(&bindingLock_);
        return result;
    }

    bool CreatureFacingInputRouterHook::SnapshotBinding(
        void* creature,
        BindingSnapshot& snapshot,
        ULONGLONG frameAt) noexcept
    {
        snapshot = {};
        const std::shared_ptr<Binding> binding = FindBinding(creature);
        if (binding == nullptr)
        {
            return false;
        }
        if (frameAt != 0)
        {
            binding->lastFrameAt.store(frameAt, std::memory_order_release);
        }
        snapshot.navigator = binding->navigator.load(
            std::memory_order_acquire);
        snapshot.provider = binding->provider.load(
            std::memory_order_acquire);
        snapshot.providerContext = binding->providerContext.load(
            std::memory_order_acquire);
        snapshot.lastFrameAt = binding->lastFrameAt.load(
            std::memory_order_acquire);
        return true;
    }

    bool CreatureFacingInputRouterHook::UpdateMovementReport(
        void* creature,
        bool nativeFrame,
        bool moving,
        ULONGLONG now) noexcept
    {
        const std::shared_ptr<Binding> binding = FindBinding(creature);
        if (binding == nullptr)
        {
            return false;
        }
        std::atomic_bool& movingState = nativeFrame
            ? binding->nativeMoving
            : binding->backgroundMoving;
        std::atomic<ULONGLONG>& lastReportAt = nativeFrame
            ? binding->lastNativeMovementReportAt
            : binding->lastBackgroundMovementReportAt;
        const bool wasMoving = movingState.exchange(
            moving, std::memory_order_acq_rel);
        const ULONGLONG previousReportAt = lastReportAt.load(
            std::memory_order_acquire);
        const bool report = moving &&
            (!wasMoving || now - previousReportAt >= 250);
        if (report)
        {
            lastReportAt.store(now, std::memory_order_release);
        }
        return report;
    }

    bool CreatureFacingInputRouterHook::Drive(void* targetCreature)
    {
        if (!IsInstalled() || targetCreature == nullptr)
        {
            return false;
        }
        const ULONGLONG now = GetTickCount64();
        BindingSnapshot binding;
        if (!SnapshotBinding(targetCreature, binding))
        {
            return false;
        }
        void* const navigator = binding.navigator;
        const ReplicatedMovementProvider provider = binding.provider;
        void* const providerContext = binding.providerContext;
        if (navigator == nullptr || provider == nullptr)
        {
            return false;
        }
        // Never add a second transform writer beside engine-owned creature
        // frames. Background-native frames can be tens of milliseconds apart,
        // so the timer fallback must wait for a genuine simulation stall
        // instead of filling every apparent gap with absolute corrections.
        constexpr ULONGLONG NativeFrameGraceMilliseconds = 120;
        if (binding.lastFrameAt != 0 &&
            now - binding.lastFrameAt < NativeFrameGraceMilliseconds)
        {
            return true;
        }

        ReplicatedMovementInput input;
        if (!provider(
                providerContext,
                targetCreature,
                input) ||
            !std::isfinite(input.position.x) ||
            !std::isfinite(input.position.y) ||
            !std::isfinite(input.position.z) ||
            !std::isfinite(input.velocity.x) ||
            !std::isfinite(input.velocity.y) ||
            !std::isfinite(input.velocity.z) ||
            !std::isfinite(input.facing) ||
            !std::isfinite(input.angularVelocity))
        {
            return false;
        }

        Vector3 currentPosition = {};
        bool readable = false;
        __try
        {
            std::memcpy(
                &currentPosition,
                static_cast<const std::uint8_t*>(navigator) +
                    ::fable::game::creature::locomotion::native::
                        PhysicsNavigatorFunctions::WorldPositionOffset,
                sizeof(currentPosition));
            readable = std::isfinite(currentPosition.x) &&
                std::isfinite(currentPosition.y) &&
                std::isfinite(currentPosition.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (!readable)
        {
            return false;
        }

        const bool controlledPhysics =
            ::fable::game::creature::locomotion::native::
                PhysicsWorldPositionFunctions::ValidateControlledComponent(
                    gameModule_, navigator);
        const bool positioned = controlledPhysics
            ? ::fable::game::creature::locomotion::native::
                PhysicsWorldPositionFunctions::SetControlledWorldPosition(
                    gameModule_, navigator, input.position)
            : ::fable::game::creature::locomotion::native::
                PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
                    gameModule_, navigator, input.position);
        if (!positioned)
        {
            return false;
        }
        float facing = input.facing - std::floor(input.facing);
        if (facing < 0.0f)
        {
            facing += 1.0f;
        }
        const bool facingApplied =
            native::CreatureLookFunctions::SetNavigatorFacing(
                gameModule_, navigator, facing);
        float observedFacing = 0.0f;
        const bool facingObserved = facingApplied &&
            native::CreatureLookFunctions::ReadNavigatorFacing(
                gameModule_, navigator, observedFacing);
        float facingError = facingObserved
            ? std::fabs(observedFacing - facing)
            : 1.0f;
        facingError = (std::min)(facingError, 1.0f - facingError);
        const bool moving = input.velocity.x * input.velocity.x +
            input.velocity.y * input.velocity.y +
            input.velocity.z * input.velocity.z >= 0.0025f;
        const bool reportActorMovement = UpdateMovementReport(
            targetCreature, false, moving, now);

        const unsigned int ordinal = backgroundMovementCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (reportActorMovement || ordinal == 1 || ordinal == 2 ||
            ordinal == 10 || ordinal == 60)
        {
            char detail[448] = {};
            std::snprintf(
                detail, std::size(detail),
                "ordinal=%u actor_id=%llu target_creature=%p linear_velocity=(%.6f,%.6f,%.6f) angular_velocity=%.6f requested_facing=%.6f observed_facing=%.6f facing_error=%.6f positioned=%s facing_applied=%s facing_observed=%s thread=%lu",
                ordinal,
                static_cast<unsigned long long>(input.actorId),
                targetCreature,
                input.velocity.x,
                input.velocity.y,
                input.velocity.z,
                input.angularVelocity,
                facing,
                observedFacing, facingError,
                positioned ? "true" : "false",
                facingApplied ? "true" : "false",
                facingObserved ? "true" : "false",
                static_cast<unsigned long>(GetCurrentThreadId()));
            diagnostics_.Event(
                "CreatureBackgroundReplicatedMovementDriven", detail);
        }
        return positioned && facingApplied;
    }

    void CreatureFacingInputRouterHook::SetFrameObserver(
        FrameObserver observer,
        void* context) noexcept
    {
        frameObserverContext_.store(context, std::memory_order_release);
        frameObserver_.store(observer, std::memory_order_release);
    }

    bool CreatureFacingInputRouterHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtablePatch_.IsInstalled();
    }

    bool CreatureFacingInputRouterHook::IsBound() const noexcept
    {
        AcquireSRWLockShared(&bindingLock_);
        const bool bound = !bindings_.empty();
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

        const ULONGLONG now = GetTickCount64();
        BindingSnapshot binding;
        (void)router->SnapshotBinding(creature, binding, now);
        void* const navigator = binding.navigator;
        const ReplicatedMovementProvider provider = binding.provider;
        void* const providerContext = binding.providerContext;

        ReplicatedMovementInput replicatedInput;
        bool hasReplicatedInput = provider != nullptr &&
            provider(
                providerContext,
                creature,
                replicatedInput);
        const bool validReplicatedInput = hasReplicatedInput &&
            navigator != nullptr &&
            std::isfinite(replicatedInput.position.x) &&
            std::isfinite(replicatedInput.position.y) &&
            std::isfinite(replicatedInput.position.z) &&
            std::isfinite(replicatedInput.velocity.x) &&
            std::isfinite(replicatedInput.velocity.y) &&
            std::isfinite(replicatedInput.velocity.z) &&
            std::isfinite(replicatedInput.facing) &&
            std::isfinite(replicatedInput.angularVelocity);
        const bool replicatedMoving = validReplicatedInput &&
            replicatedInput.velocity.x * replicatedInput.velocity.x +
                replicatedInput.velocity.y * replicatedInput.velocity.y +
                replicatedInput.velocity.z * replicatedInput.velocity.z >=
                0.0025f;
        const bool reportNativeMovement = router->UpdateMovementReport(
            creature, true, replicatedMoving, now);
        bool positionApplied = false;
        bool facingAppliedBeforeUpdate = false;
        float replicatedFacing = 0.0f;
        const bool controlledPhysics = navigator != nullptr &&
            ::fable::game::creature::locomotion::native::
                PhysicsWorldPositionFunctions::ValidateControlledComponent(
                    router->gameModule_,
                    navigator);
        if (validReplicatedInput)
        {
            Vector3 currentPosition = {};
            bool readable = false;
            __try
            {
                std::memcpy(
                    &currentPosition,
                    static_cast<const std::uint8_t*>(navigator) +
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
                // The actor-generic replication layer already evaluates a
                // delayed/interpolated target for this exact native frame.
                // Applying that point directly prevents a second prediction
                // pass and removes the old 0.35-unit-per-frame crawl.
                const Vector3 target = replicatedInput.position;
                const float deltaX = target.x - currentPosition.x;
                const float deltaY = target.y - currentPosition.y;
                const float deltaZ = target.z - currentPosition.z;
                const float distance = std::sqrt(
                    deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

                if (distance >= 0.005f)
                {
                    positionApplied = controlledPhysics
                        ? ::fable::game::creature::locomotion::native::
                            PhysicsWorldPositionFunctions::SetControlledWorldPosition(
                                router->gameModule_,
                                navigator,
                                target)
                        : ::fable::game::creature::locomotion::native::
                            PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
                                router->gameModule_,
                                navigator,
                                target);
                }
                else
                {
                    positionApplied = true;
                }
            }

            // Position and yaw are one presented transform. Apply both before
            // Fable evaluates creature motion so its native locomotion and
            // turn/lean consumers observe the same smoothed frame.
            replicatedFacing = replicatedInput.facing -
                std::floor(replicatedInput.facing);
            if (replicatedFacing < 0.0f)
            {
                replicatedFacing += 1.0f;
            }
            facingAppliedBeforeUpdate =
                native::CreatureLookFunctions::SetNavigatorFacing(
                    router->gameModule_,
                    navigator,
                    replicatedFacing);
        }

        const bool result = router->original_(creature);
        const FrameObserver observer = router->frameObserver_.load(
            std::memory_order_acquire);
        if (observer != nullptr)
        {
            observer(
                router->frameObserverContext_.load(
                    std::memory_order_acquire),
                creature);
        }
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

        if (validReplicatedInput)
        {
            float observedFacing = 0.0f;
            bool facingObserved =
                native::CreatureLookFunctions::ReadNavigatorFacing(
                    router->gameModule_, navigator, observedFacing);
            float facingError = facingObserved
                ? std::fabs(observedFacing - replicatedFacing)
                : 1.0f;
            facingError = (std::min)(facingError, 1.0f - facingError);

            bool facingReasserted = false;
            if (!facingObserved || facingError >= 0.0005f)
            {
                facingReasserted =
                    native::CreatureLookFunctions::SetNavigatorFacing(
                        router->gameModule_, navigator, replicatedFacing);
                if (facingReasserted)
                {
                    facingObserved =
                        native::CreatureLookFunctions::ReadNavigatorFacing(
                            router->gameModule_, navigator, observedFacing);
                    facingError = facingObserved
                        ? std::fabs(observedFacing - replicatedFacing)
                        : 1.0f;
                    facingError =
                        (std::min)(facingError, 1.0f - facingError);
                }
            }

            const unsigned int ordinal = router->routedFacingCount_.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
            if (reportNativeMovement || ordinal == 1 || ordinal == 2 ||
                ordinal == 10 || ordinal == 60)
            {
                char detail[448] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "ordinal=%u actor_id=%llu target_creature=%p motion=(%.6f,%.6f) linear_velocity=(%.6f,%.6f,%.6f) angular_velocity=%.6f requested_facing=%.6f observed_facing=%.6f facing_error=%.6f positioned=%s facing_applied_before_update=%s facing_reasserted=%s facing_observed=%s physics=%s thread=%lu",
                    ordinal,
                    static_cast<unsigned long long>(replicatedInput.actorId),
                    creature,
                    motionX,
                    motionY,
                    replicatedInput.velocity.x,
                    replicatedInput.velocity.y,
                    replicatedInput.velocity.z,
                    replicatedInput.angularVelocity,
                    replicatedFacing,
                    observedFacing,
                    facingError,
                    positionApplied ? "true" : "false",
                    facingAppliedBeforeUpdate ? "true" : "false",
                    facingReasserted ? "true" : "false",
                    facingObserved ? "true" : "false",
                    controlledPhysics ? "controlled" : "navigator",
                    static_cast<unsigned long>(GetCurrentThreadId()));
                router->diagnostics_.Event(
                    "CreatureMovementFacingRouted", detail);
            }
            return result;
        }

        float facing = 0.0f;
        const float planarSquared = motionX * motionX + motionY * motionY;
        if (!readable || planarSquared <= 0.00000001f)
        {
            return result;
        }
        constexpr float inverseTau = 0.15915494309189533577f;
        facing = std::atan2(motionX, motionY) * inverseTau;
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
        float observedFacing = 0.0f;
        const bool facingObserved =
            native::CreatureLookFunctions::ReadNavigatorFacing(
                router->gameModule_, navigator, observedFacing);
        float facingError = facingObserved
            ? std::fabs(observedFacing - facing)
            : 1.0f;
        facingError = (std::min)(facingError, 1.0f - facingError);

        const unsigned int ordinal = router->routedFacingCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u target_creature=%p motion=(%.6f,%.6f) requested_facing=%.6f observed_facing=%.6f facing_error=%.6f facing_observed=%s replicated_movement=%s requested=%s physics=%s thread=%lu",
                ordinal,
                creature,
                motionX,
                motionY,
                facing,
                observedFacing,
                facingError,
                facingObserved ? "true" : "false",
                "false",
                "false",
                controlledPhysics ? "controlled" : "navigator",
                static_cast<unsigned long>(GetCurrentThreadId()));
            router->diagnostics_.Event("CreatureMovementFacingRouted", detail);
        }
        return result;
    }
}
