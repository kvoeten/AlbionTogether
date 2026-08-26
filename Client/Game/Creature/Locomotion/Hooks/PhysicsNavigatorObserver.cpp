#include "PhysicsNavigatorObserver.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
    bool IsFinite(const fable::game::Vector3& value) noexcept
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    std::uint32_t HashBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount) noexcept
    {
        std::uint32_t hash = 2166136261u;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 16777619u;
        }
        return hash;
    }

    void DescribeChangedAnimationOffsets(
        const std::array<std::uint32_t, 0xC0 / sizeof(std::uint32_t)>& before,
        const std::array<std::uint32_t, 0xC0 / sizeof(std::uint32_t)>& after,
        char* output,
        std::size_t outputSize) noexcept
    {
        if (output == nullptr || outputSize == 0)
        {
            return;
        }

        output[0] = '\0';
        std::size_t used = 0;
        unsigned int reported = 0;
        unsigned int changed = 0;
        for (std::size_t index = 0; index < before.size(); ++index)
        {
            if (before[index] == after[index])
            {
                continue;
            }
            ++changed;
            if (reported >= 12)
            {
                continue;
            }

            const int written = std::snprintf(
                output + used,
                outputSize - used,
                "%s0x%02X",
                reported == 0 ? "" : ",",
                static_cast<unsigned int>(index * sizeof(std::uint32_t)));
            if (written <= 0 || static_cast<std::size_t>(written) >= outputSize - used)
            {
                break;
            }
            used += static_cast<std::size_t>(written);
            ++reported;
        }

        if (changed == 0)
        {
            std::snprintf(output, outputSize, "none");
        }
        else if (changed > reported && used < outputSize)
        {
            std::snprintf(
                output + used,
                outputSize - used,
                ",...(%u total)",
                changed);
        }
    }
}

namespace fable::game::creature::locomotion
{
    PhysicsNavigatorObserver* PhysicsNavigatorObserver::active_ = nullptr;

    bool PhysicsNavigatorObserver::Install(
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
        diagnostics_.Log("Hook: physics navigator observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log("Hook: another physics navigator observer is already active.");
            return false;
        }

        void** requestSlot = nullptr;
        void** updateSlot = nullptr;
        native::PhysicsNavigatorFunctions::RequestNextPositionPointer request = nullptr;
        native::PhysicsNavigatorFunctions::UpdateMovementPointer update = nullptr;
        if (!native::PhysicsNavigatorFunctions::ResolveSlots(
                gameModule,
                &requestSlot,
                request,
                &updateSlot,
                update))
        {
            diagnostics_.Log("Hook: CTCPhysicsNavigator request/update definitions failed validation.");
            return false;
        }

        void* const expectedRequest = reinterpret_cast<void*>(request);
        void* const replacementRequest = reinterpret_cast<void*>(
            &PhysicsNavigatorObserver::ObserveRequest);
        if (!requestPatch_.Install(
                requestSlot,
                &expectedRequest,
                sizeof(expectedRequest),
                &replacementRequest,
                sizeof(replacementRequest)))
        {
            diagnostics_.Log("Hook: navigator request-slot patch installation failed.");
            return false;
        }
        void* const expectedUpdate = reinterpret_cast<void*>(update);
        void* const replacementUpdate = reinterpret_cast<void*>(
            &PhysicsNavigatorObserver::ObserveUpdate);
        if (!updatePatch_.Install(
                updateSlot,
                &expectedUpdate,
                sizeof(expectedUpdate),
                &replacementUpdate,
                sizeof(replacementUpdate)))
        {
            const bool requestRemoved = requestPatch_.Shutdown();
            if (!requestRemoved)
            {
                originalRequest_ = request;
                originalUpdate_ = update;
                active_ = this;
                diagnostics_.Log(
                    "Hook: navigator update-slot install failed and the request-slot patch could not be rolled back; callback state retained.");
                return false;
            }
            if (requestPatch_.ProtectionRestoreFailed())
            {
                diagnostics_.Log(
                    "Hook: navigator request-slot rollback restored bytes, but memory protection restoration failed.");
            }
            diagnostics_.Log("Hook: navigator update-slot patch installation failed.");
            return false;
        }

        originalRequest_ = request;
        originalUpdate_ = update;
        active_ = this;

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "request_slot=%p request=%p update_slot=%p update=%p tracked_limit=%zu",
            requestSlot,
            reinterpret_cast<void*>(request),
            updateSlot,
            reinterpret_cast<void*>(update),
            TrackedNavigatorLimit);
        diagnostics_.Log("Hook: read-only CTCPhysicsNavigator request/integration observer installed.");
        diagnostics_.Event("PhysicsNavigatorObserverReady", detail);
        return true;
#endif
    }

    bool PhysicsNavigatorObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalRequest_ != nullptr && originalUpdate_ != nullptr &&
            requestPatch_.IsInstalled() && updatePatch_.IsInstalled();
    }

    void PhysicsNavigatorObserver::Shutdown() noexcept
    {
        const bool updateRemoved = !updatePatch_.IsInstalled() || updatePatch_.Shutdown();
        const bool requestRemoved = !requestPatch_.IsInstalled() || requestPatch_.Shutdown();
        if (!updateRemoved || !requestRemoved)
        {
            diagnostics_.Log("Hook: navigator shutdown skipped because a vtable slot changed.");
            return;
        }
        if (updatePatch_.ProtectionRestoreFailed() ||
            requestPatch_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: navigator vtable slots restored, but memory protection restoration failed.");
        }
        if (active_ == this) active_ = nullptr;
        originalUpdate_ = nullptr;
        originalRequest_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    unsigned int PhysicsNavigatorObserver::RequestCount() const noexcept
    {
        return requestCount_.load(std::memory_order_acquire);
    }

    PhysicsNavigatorObserver::Snapshot PhysicsNavigatorObserver::Capture(
        void* navigator) noexcept
    {
        Snapshot snapshot;
        if (navigator == nullptr)
        {
            return snapshot;
        }
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(navigator);
            snapshot.ownerCandidate = *reinterpret_cast<void* const*>(
                bytes + sizeof(void*));
            if (snapshot.ownerCandidate != nullptr)
            {
                const auto* const ownerBytes =
                    static_cast<const std::uint8_t*>(snapshot.ownerCandidate);
                snapshot.ownerMotionX = *reinterpret_cast<const float*>(
                    ownerBytes + 0x134);
                snapshot.ownerMotionY = *reinterpret_cast<const float*>(
                    ownerBytes + 0x138);
                snapshot.ownerMotionValid =
                    std::isfinite(snapshot.ownerMotionX) &&
                    std::isfinite(snapshot.ownerMotionY);
            }
            snapshot.animationComplex = entity::native::ThingComponentAccess::Find(
                snapshot.ownerCandidate,
                entity::native::ThingComponentType::AnimationComplex);
            if (snapshot.animationComplex != nullptr)
            {
                const auto* const animationBytes =
                    static_cast<const std::uint8_t*>(snapshot.animationComplex);
                snapshot.animationState = *reinterpret_cast<void* const*>(
                    animationBytes + 0x0C);
                if (snapshot.animationState != nullptr)
                {
                    std::memcpy(
                        snapshot.animationStateDwords.data(),
                        snapshot.animationState,
                        AnimationSnapshotByteCount);
                    snapshot.animationStateHash = HashBytes(
                        reinterpret_cast<const std::uint8_t*>(
                            snapshot.animationStateDwords.data()),
                        AnimationSnapshotByteCount);
                    snapshot.animationSnapshotValid = true;
                }
            }
            std::memcpy(
                &snapshot.worldPosition,
                bytes + native::PhysicsNavigatorFunctions::WorldPositionOffset,
                sizeof(snapshot.worldPosition));
            std::memcpy(
                &snapshot.vectorAt28,
                bytes + native::PhysicsNavigatorFunctions::IntegratedMotionOffset,
                sizeof(snapshot.vectorAt28));
            std::memcpy(
                &snapshot.desiredPosition,
                bytes + native::PhysicsNavigatorFunctions::DesiredPositionOffset,
                sizeof(snapshot.desiredPosition));
            std::memcpy(
                &snapshot.vectorAt95,
                bytes + native::PhysicsNavigatorFunctions::TransientMotionOffset,
                sizeof(snapshot.vectorAt95));
            snapshot.stateFlags = bytes[
                native::PhysicsNavigatorFunctions::StateFlagsOffset];
            snapshot.collisionFlags = bytes[
                native::PhysicsNavigatorFunctions::CollisionFlagsOffset];
            snapshot.valid = IsFinite(snapshot.worldPosition) &&
                IsFinite(snapshot.vectorAt28) &&
                IsFinite(snapshot.desiredPosition) &&
                IsFinite(snapshot.vectorAt95);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot = {};
        }
        return snapshot;
    }

    int PhysicsNavigatorObserver::Track(void* navigator) noexcept
    {
        const int existing = Find(navigator);
        if (existing >= 0)
        {
            return existing;
        }
        for (std::size_t index = 0; index < trackedNavigators_.size(); ++index)
        {
            void* expected = nullptr;
            if (trackedNavigators_[index].compare_exchange_strong(
                    expected,
                    navigator,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                requestCounts_[index].store(0, std::memory_order_release);
                updateCounts_[index].store(0, std::memory_order_release);
                animationTransitionCounts_[index].store(
                    0,
                    std::memory_order_release);
                lastSnapshots_[index] = Capture(navigator);
                return static_cast<int>(index);
            }
            if (expected == navigator)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    int PhysicsNavigatorObserver::Find(void* navigator) const noexcept
    {
        for (std::size_t index = 0; index < trackedNavigators_.size(); ++index)
        {
            if (trackedNavigators_[index].load(std::memory_order_acquire) == navigator)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    void PhysicsNavigatorObserver::ReportRequest(
        void* navigator,
        const Vector3* requested,
        const Snapshot& after,
        void* caller,
        unsigned int ordinal) const
    {
        Vector3 request = {};
        bool requestReadable = false;
        if (requested != nullptr)
        {
            __try
            {
                request = *requested;
                requestReadable = IsFinite(request);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                requestReadable = false;
            }
        }

        std::uintptr_t callerRva = 0;
        if (caller != nullptr && gameModule_ != nullptr)
        {
            const auto address = reinterpret_cast<std::uintptr_t>(caller);
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
            callerRva = address >= base ? address - base : 0;
        }

        char detail[768] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "navigator=%p owner_candidate=%p navigator_ordinal=%u caller=%p caller_rva=0x%08X requested=(%.3f,%.3f,%.3f) current=(%.3f,%.3f,%.3f) stored_desired=(%.3f,%.3f,%.3f) owner_motion_134_138=(%.6f,%.6f) owner_motion_valid=%s flags=0x%02X/0x%02X request_readable=%s snapshot_valid=%s thread=%lu",
            navigator,
            after.ownerCandidate,
            ordinal,
            caller,
            static_cast<unsigned int>(callerRva),
            request.x,
            request.y,
            request.z,
            after.worldPosition.x,
            after.worldPosition.y,
            after.worldPosition.z,
            after.desiredPosition.x,
            after.desiredPosition.y,
            after.desiredPosition.z,
            after.ownerMotionX,
            after.ownerMotionY,
            after.ownerMotionValid ? "true" : "false",
            after.stateFlags,
            after.collisionFlags,
            requestReadable ? "true" : "false",
            after.valid ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("PhysicsNavigatorRequestObserved", detail);
    }

    void PhysicsNavigatorObserver::ReportUpdate(
        void* navigator,
        const Snapshot& before,
        const Snapshot& after,
        unsigned int ordinal) const
    {
        const Vector3 displacement = {
            after.worldPosition.x - before.worldPosition.x,
            after.worldPosition.y - before.worldPosition.y,
            after.worldPosition.z - before.worldPosition.z,
        };
        char changedAnimationOffsets[192] = {};
        if (before.animationSnapshotValid && after.animationSnapshotValid &&
            before.animationState == after.animationState)
        {
            DescribeChangedAnimationOffsets(
                before.animationStateDwords,
                after.animationStateDwords,
                changedAnimationOffsets,
                std::size(changedAnimationOffsets));
        }
        else
        {
            std::snprintf(
                changedAnimationOffsets,
                std::size(changedAnimationOffsets),
                "unavailable");
        }

        char detail[1152] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "navigator=%p owner_candidate=%p ordinal=%u before=(%.3f,%.3f,%.3f) after=(%.3f,%.3f,%.3f) displacement=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f) vector_28=(%.3f,%.3f,%.3f) vector_95=(%.3f,%.3f,%.3f) owner_motion_134_138=(%.6f,%.6f)->(%.6f,%.6f) owner_motion_valid=%s/%s flags=0x%02X/0x%02X animation_complex=%p animation_state=%p animation_hash=0x%08X->0x%08X animation_state_word_00=0x%08X->0x%08X animation_changed_offsets=%s animation_valid=%s valid=%s thread=%lu",
            navigator,
            after.ownerCandidate,
            ordinal,
            before.worldPosition.x,
            before.worldPosition.y,
            before.worldPosition.z,
            after.worldPosition.x,
            after.worldPosition.y,
            after.worldPosition.z,
            displacement.x,
            displacement.y,
            displacement.z,
            after.desiredPosition.x,
            after.desiredPosition.y,
            after.desiredPosition.z,
            after.vectorAt28.x,
            after.vectorAt28.y,
            after.vectorAt28.z,
            after.vectorAt95.x,
            after.vectorAt95.y,
            after.vectorAt95.z,
            before.ownerMotionX,
            before.ownerMotionY,
            after.ownerMotionX,
            after.ownerMotionY,
            before.ownerMotionValid ? "true" : "false",
            after.ownerMotionValid ? "true" : "false",
            after.stateFlags,
            after.collisionFlags,
            after.animationComplex,
            after.animationState,
            before.animationStateHash,
            after.animationStateHash,
            before.animationStateDwords[0],
            after.animationStateDwords[0],
            changedAnimationOffsets,
            before.animationSnapshotValid && after.animationSnapshotValid
                ? "true"
                : "false",
            before.valid && after.valid ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("PhysicsNavigatorUpdateObserved", detail);
    }

    void PhysicsNavigatorObserver::ReportAnimationTransition(
        void* navigator,
        const Snapshot& previousFrame,
        const Snapshot& currentFrame,
        unsigned int ordinal) const
    {
        char changedAnimationOffsets[192] = {};
        DescribeChangedAnimationOffsets(
            previousFrame.animationStateDwords,
            currentFrame.animationStateDwords,
            changedAnimationOffsets,
            std::size(changedAnimationOffsets));

        char detail[768] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "navigator=%p owner_candidate=%p ordinal=%u animation_complex=%p animation_state=%p animation_hash=0x%08X->0x%08X animation_state_word_00=0x%08X->0x%08X changed_offsets=%s thread=%lu",
            navigator,
            currentFrame.ownerCandidate,
            ordinal,
            currentFrame.animationComplex,
            currentFrame.animationState,
            previousFrame.animationStateHash,
            currentFrame.animationStateHash,
            previousFrame.animationStateDwords[0],
            currentFrame.animationStateDwords[0],
            changedAnimationOffsets,
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureAnimationStateTransitionObserved", detail);
    }

    void __fastcall PhysicsNavigatorObserver::ObserveRequest(
        void* navigator,
        void*,
        const Vector3* desiredPosition)
    {
        PhysicsNavigatorObserver* const observer = active_;
        if (observer == nullptr || observer->originalRequest_ == nullptr)
        {
            return;
        }

        void* const caller = _ReturnAddress();
        observer->originalRequest_(navigator, desiredPosition);
        const unsigned int globalOrdinal = observer->requestCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        const int trackedIndex = observer->Track(navigator);
        unsigned int navigatorOrdinal = 0;
        if (trackedIndex >= 0)
        {
            navigatorOrdinal = observer->requestCounts_[
                static_cast<std::size_t>(trackedIndex)].fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
        }
        const bool reportNavigatorSample =
            navigatorOrdinal == 1 || navigatorOrdinal == 2 ||
            navigatorOrdinal == 10 || navigatorOrdinal == 60;
        const bool reportGlobalSample =
            globalOrdinal <= 32 || globalOrdinal == 60 || globalOrdinal == 120;
        if (reportNavigatorSample || reportGlobalSample)
        {
            observer->ReportRequest(
                navigator,
                desiredPosition,
                Capture(navigator),
                caller,
                navigatorOrdinal);
        }
    }

    void __fastcall PhysicsNavigatorObserver::ObserveUpdate(void* navigator, void*)
    {
        PhysicsNavigatorObserver* const observer = active_;
        if (observer == nullptr || observer->originalUpdate_ == nullptr)
        {
            return;
        }

        const int trackedIndex = observer->Find(navigator);
        if (trackedIndex < 0)
        {
            observer->originalUpdate_(navigator);
            return;
        }

        const Snapshot before = Capture(navigator);
        observer->originalUpdate_(navigator);
        const Snapshot after = Capture(navigator);
        const std::size_t index = static_cast<std::size_t>(trackedIndex);
        const unsigned int ordinal = observer->updateCounts_[index].fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
        const Snapshot previousFrame = observer->lastSnapshots_[index];
        observer->lastSnapshots_[index] = after;
        if (previousFrame.animationSnapshotValid &&
            after.animationSnapshotValid &&
            previousFrame.animationState == after.animationState &&
            previousFrame.animationStateHash != after.animationStateHash)
        {
            const unsigned int transitionOrdinal =
                observer->animationTransitionCounts_[index].fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            if (transitionOrdinal == 1 || transitionOrdinal == 2 ||
                transitionOrdinal == 10 || transitionOrdinal == 60 ||
                previousFrame.animationStateDwords[0] !=
                    after.animationStateDwords[0])
            {
                observer->ReportAnimationTransition(
                    navigator,
                    previousFrame,
                    after,
                    transitionOrdinal);
            }
        }
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60)
        {
            observer->ReportUpdate(navigator, before, after, ordinal);
        }
    }
}
