#include "CreatureModeManagerObserver.h"

#include <array>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    using Patch = std::array<
        std::uint8_t,
        fable::game::creature::locomotion::native::
            CreatureModeManagerFunctions::DisplacedBytes>;

    std::uint32_t HashDwords(
        const std::uint32_t* values,
        std::size_t count) noexcept
    {
        std::uint32_t hash = 2166136261u;
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(values);
        for (std::size_t index = 0;
             index < count * sizeof(std::uint32_t);
             ++index)
        {
            hash ^= bytes[index];
            hash *= 16777619u;
        }
        return hash;
    }

    bool BuildTrampoline(
        std::uint8_t* target,
        const void* replacement,
        void*& trampolineResult,
        Patch& patchResult) noexcept
    {
        constexpr std::size_t displacedBytes =
            fable::game::creature::locomotion::native::
                CreatureModeManagerFunctions::DisplacedBytes;
        trampolineResult = nullptr;
        patchResult.fill(0x90);

        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t resumeDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t observerDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resumeDisplacement < INT32_MIN || resumeDisplacement > INT32_MAX ||
            observerDisplacement < INT32_MIN || observerDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        const std::int32_t resumeRelative =
            static_cast<std::int32_t>(resumeDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        patchResult[0] = 0xE9;
        const std::int32_t observerRelative =
            static_cast<std::int32_t>(observerDisplacement);
        std::memcpy(
            patchResult.data() + 1,
            &observerRelative,
            sizeof(observerRelative));
        trampolineResult = trampoline;
        return true;
    }
}

namespace fable::game::creature::locomotion
{
    CreatureModeManagerObserver* CreatureModeManagerObserver::active_ = nullptr;

    bool CreatureModeManagerObserver::Install(
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
            "Hook: creature-mode observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another creature-mode observer is already active.");
            return false;
        }

        std::uint8_t* addTarget = nullptr;
        std::uint8_t* removeTarget = nullptr;
        std::uint8_t* evaluateTarget = nullptr;
        if (!native::CreatureModeManagerFunctions::ResolveAddSource(
                gameModule,
                addTarget) ||
            !native::CreatureModeManagerFunctions::ResolveRemoveSource(
                gameModule,
                removeTarget) ||
            !native::CreatureModeManagerFunctions::ResolveEvaluateLocomotion(
                gameModule,
                evaluateTarget))
        {
            diagnostics_.Log(
                "Hook: CTCCreatureModeManager source definitions failed validation.");
            return false;
        }

        Patch addPatch = {};
        Patch removePatch = {};
        Patch evaluatePatch = {};
        void* addTrampoline = nullptr;
        void* removeTrampoline = nullptr;
        void* evaluateTrampoline = nullptr;
        if (!BuildTrampoline(
                addTarget,
                reinterpret_cast<const void*>(
                    &CreatureModeManagerObserver::ObserveAddSource),
                addTrampoline,
                addPatch) ||
            !BuildTrampoline(
                removeTarget,
                reinterpret_cast<const void*>(
                    &CreatureModeManagerObserver::ObserveRemoveSource),
                removeTrampoline,
                removePatch) ||
            !BuildTrampoline(
                evaluateTarget,
                reinterpret_cast<const void*>(
                    &CreatureModeManagerObserver::ObserveLocomotionEvaluation),
                evaluateTrampoline,
                evaluatePatch))
        {
            if (addTrampoline != nullptr)
            {
                VirtualFree(addTrampoline, 0, MEM_RELEASE);
            }
            if (removeTrampoline != nullptr)
            {
                VirtualFree(removeTrampoline, 0, MEM_RELEASE);
            }
            if (evaluateTrampoline != nullptr)
            {
                VirtualFree(evaluateTrampoline, 0, MEM_RELEASE);
            }
            diagnostics_.Log(
                "Hook: creature-mode trampoline construction failed.");
            return false;
        }

        DWORD addProtection = 0;
        DWORD removeProtection = 0;
        DWORD evaluateProtection = 0;
        if (!VirtualProtect(
                addTarget,
                addPatch.size(),
                PAGE_EXECUTE_READWRITE,
                &addProtection))
        {
            VirtualFree(addTrampoline, 0, MEM_RELEASE);
            VirtualFree(removeTrampoline, 0, MEM_RELEASE);
            VirtualFree(evaluateTrampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: creature-mode AddSource protection change failed.");
            return false;
        }
        if (!VirtualProtect(
                removeTarget,
                removePatch.size(),
                PAGE_EXECUTE_READWRITE,
                &removeProtection))
        {
            DWORD discarded = 0;
            VirtualProtect(
                addTarget,
                addPatch.size(),
                addProtection,
                &discarded);
            VirtualFree(addTrampoline, 0, MEM_RELEASE);
            VirtualFree(removeTrampoline, 0, MEM_RELEASE);
            VirtualFree(evaluateTrampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: creature-mode RemoveSource protection change failed.");
            return false;
        }
        if (!VirtualProtect(
                evaluateTarget,
                evaluatePatch.size(),
                PAGE_EXECUTE_READWRITE,
                &evaluateProtection))
        {
            DWORD discarded = 0;
            VirtualProtect(
                removeTarget,
                removePatch.size(),
                removeProtection,
                &discarded);
            VirtualProtect(
                addTarget,
                addPatch.size(),
                addProtection,
                &discarded);
            VirtualFree(addTrampoline, 0, MEM_RELEASE);
            VirtualFree(removeTrampoline, 0, MEM_RELEASE);
            VirtualFree(evaluateTrampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: locomotion-mode evaluation protection change failed.");
            return false;
        }

        addSourceTrampoline_ = addTrampoline;
        removeSourceTrampoline_ = removeTrampoline;
        evaluateLocomotionTrampoline_ = evaluateTrampoline;
        originalAddSource_ = reinterpret_cast<
            native::CreatureModeManagerFunctions::AddSourcePointer>(
                addSourceTrampoline_);
        originalRemoveSource_ = reinterpret_cast<
            native::CreatureModeManagerFunctions::RemoveSourcePointer>(
                removeSourceTrampoline_);
        originalEvaluateLocomotion_ = reinterpret_cast<
            native::CreatureModeManagerFunctions::EvaluateLocomotionPointer>(
                evaluateLocomotionTrampoline_);
        active_ = this;

        std::memcpy(addTarget, addPatch.data(), addPatch.size());
        std::memcpy(removeTarget, removePatch.data(), removePatch.size());
        std::memcpy(
            evaluateTarget,
            evaluatePatch.data(),
            evaluatePatch.size());
        FlushInstructionCache(GetCurrentProcess(), addTarget, addPatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            removeTarget,
            removePatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            evaluateTarget,
            evaluatePatch.size());

        DWORD discarded = 0;
        if (!VirtualProtect(
                evaluateTarget,
                evaluatePatch.size(),
                evaluateProtection,
                &discarded) ||
            !VirtualProtect(
                removeTarget,
                removePatch.size(),
                removeProtection,
                &discarded) ||
            !VirtualProtect(
                addTarget,
                addPatch.size(),
                addProtection,
                &discarded))
        {
            diagnostics_.Log(
                "Hook: creature-mode observers installed, but code protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "add_source=%p remove_source=%p evaluate_locomotion=%p source_1_selects_speed_animation",
            addTarget,
            removeTarget,
            evaluateTarget);
        diagnostics_.Log(
            "Hook: CTCCreatureModeManager source and locomotion evaluation observers installed.");
        diagnostics_.Event("CreatureModeManagerObserverReady", detail);
        return true;
#endif
    }

    bool CreatureModeManagerObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalAddSource_ != nullptr && originalRemoveSource_ != nullptr &&
            originalEvaluateLocomotion_ != nullptr &&
            addSourceTrampoline_ != nullptr && removeSourceTrampoline_ != nullptr &&
            evaluateLocomotionTrampoline_ != nullptr;
    }

    bool CreatureModeManagerObserver::WatchOwner(void* nativeThing) noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || nativeThing == nullptr ||
            !observer->IsInstalled())
        {
            return false;
        }

        const int existing = observer->FindWatchedOwner(nativeThing);
        if (existing >= 0)
        {
            return true;
        }
        for (std::size_t index = 0;
             index < observer->watchedOwners_.size();
             ++index)
        {
            void* expected = nullptr;
            if (observer->watchedOwners_[index].compare_exchange_strong(
                    expected,
                    nativeThing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                observer->locomotionEvaluationCounts_[index].store(
                    0,
                    std::memory_order_release);
                observer->lastMoving_[index].store(
                    false,
                    std::memory_order_release);
                char detail[192] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "owner=%p watch_slot=%zu source_1_motion_offsets=0x134/0x138",
                    nativeThing,
                    index);
                observer->diagnostics_.Event(
                    "CreatureLocomotionOwnerWatched",
                    detail);
                return true;
            }
            if (expected == nativeThing)
            {
                return true;
            }
        }
        return false;
    }

    bool CreatureModeManagerObserver::BindAnimationMotionSource(
        void* sourcePlayerCreature,
        void* targetCreature) noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || !observer->IsInstalled() ||
            !::fable::game::creature::native::CreatureFrameFunctions::ValidateImplementations(
                observer->gameModule_) ||
            !::fable::game::creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                observer->gameModule_,
                sourcePlayerCreature) ||
            !::fable::game::creature::native::CreatureFrameFunctions::ValidateCreature(
                observer->gameModule_,
                targetCreature) ||
            !WatchOwner(targetCreature))
        {
            return false;
        }

        observer->animationMotionTarget_.store(nullptr, std::memory_order_release);
        observer->mirroredAnimationMotionCount_.store(
            0,
            std::memory_order_release);
        observer->animationMotionSource_.store(
            sourcePlayerCreature,
            std::memory_order_release);
        observer->animationMotionTarget_.store(
            targetCreature,
            std::memory_order_release);

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "source_player=%p target_creature=%p source_update_rva=0x%08X target_update_rva=0x%08X motion_offsets=0x134/0x138",
            sourcePlayerCreature,
            targetCreature,
            static_cast<unsigned int>(
                ::fable::game::creature::native::CreatureFrameFunctions::PlayerCreatureUpdateFrameRva),
            static_cast<unsigned int>(
                ::fable::game::creature::native::CreatureFrameFunctions::CreatureUpdateFrameRva));
        observer->diagnostics_.Event("CreatureAnimationMotionSourceBound", detail);
        return true;
    }

    void CreatureModeManagerObserver::ClearAnimationMotionSource() noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr)
        {
            return;
        }
        observer->animationMotionTarget_.store(nullptr, std::memory_order_release);
        observer->animationMotionSource_.store(nullptr, std::memory_order_release);
    }

    bool CreatureModeManagerObserver::SetReplicatedAnimationMotion(
        void* targetCreature,
        const Vector3& linearVelocity,
        float angularVelocity) noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || !observer->IsInstalled() ||
            targetCreature == nullptr || !std::isfinite(linearVelocity.x) ||
            !std::isfinite(linearVelocity.y) ||
            !std::isfinite(linearVelocity.z) ||
            !std::isfinite(angularVelocity))
        {
            return false;
        }
        const std::uint64_t now = GetTickCount64();
        std::shared_ptr<ReplicatedAnimationMotion> motion =
            observer->FindReplicatedAnimationMotion(targetCreature);
        if (motion == nullptr)
        {
            AcquireSRWLockExclusive(
                &observer->replicatedAnimationMotionLock_);
            auto& stored =
                observer->replicatedAnimationMotions_[targetCreature];
            if (stored == nullptr)
            {
                stored = std::make_shared<ReplicatedAnimationMotion>();
            }
            motion = stored;
            ReleaseSRWLockExclusive(
                &observer->replicatedAnimationMotionLock_);
        }
        AcquireSRWLockExclusive(&motion->valueLock);
        motion->linearVelocity = linearVelocity;
        motion->angularVelocity = angularVelocity;
        motion->updatedAt.store(now, std::memory_order_release);
        ReleaseSRWLockExclusive(&motion->valueLock);
        return WatchOwner(targetCreature);
    }

    std::shared_ptr<CreatureModeManagerObserver::ReplicatedAnimationMotion>
        CreatureModeManagerObserver::FindReplicatedAnimationMotion(
            void* owner) const noexcept
    {
        std::shared_ptr<ReplicatedAnimationMotion> result;
        AcquireSRWLockShared(&replicatedAnimationMotionLock_);
        const auto found = replicatedAnimationMotions_.find(owner);
        if (found != replicatedAnimationMotions_.end())
        {
            result = found->second;
        }
        ReleaseSRWLockShared(&replicatedAnimationMotionLock_);
        return result;
    }

    bool CreatureModeManagerObserver::ReadReplicatedAnimationMotion(
        void* owner,
        std::uint64_t now,
        Vector3& linearVelocity,
        float& angularVelocity,
        float& evaluationSeconds) const noexcept
    {
        const std::shared_ptr<ReplicatedAnimationMotion> motion =
            FindReplicatedAnimationMotion(owner);
        if (motion == nullptr)
        {
            return false;
        }
        AcquireSRWLockShared(&motion->valueLock);
        linearVelocity = motion->linearVelocity;
        angularVelocity = motion->angularVelocity;
        const std::uint64_t updatedAt = motion->updatedAt.load(
            std::memory_order_acquire);
        ReleaseSRWLockShared(&motion->valueLock);
        constexpr std::uint64_t kMaximumMotionAgeMilliseconds = 250;
        if (updatedAt == 0 || now < updatedAt ||
            now - updatedAt > kMaximumMotionAgeMilliseconds)
        {
            return false;
        }
        const std::uint64_t evaluatedAt = motion->evaluatedAt.exchange(
            now, std::memory_order_acq_rel);
        constexpr float kDefaultEvaluationSeconds = 1.0f / 60.0f;
        evaluationSeconds = evaluatedAt == 0 || now <= evaluatedAt
            ? kDefaultEvaluationSeconds
            : std::clamp(
                static_cast<float>(now - evaluatedAt) / 1000.0f,
                1.0f / 240.0f,
                0.1f);
        return true;
    }

    void CreatureModeManagerObserver::ClearReplicatedAnimationMotion(
        void* targetCreature) noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || targetCreature == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&observer->replicatedAnimationMotionLock_);
        observer->replicatedAnimationMotions_.erase(targetCreature);
        ReleaseSRWLockExclusive(&observer->replicatedAnimationMotionLock_);

        void* expectedTarget = targetCreature;
        if (observer->animationMotionTarget_.compare_exchange_strong(
                expectedTarget,
                nullptr,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            observer->animationMotionSource_.store(
                nullptr,
                std::memory_order_release);
        }
        for (std::size_t index = 0;
             index < observer->watchedOwners_.size();
             ++index)
        {
            void* expectedOwner = targetCreature;
            if (observer->watchedOwners_[index].compare_exchange_strong(
                    expectedOwner,
                    nullptr,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                observer->locomotionEvaluationCounts_[index].store(
                    0,
                    std::memory_order_release);
                observer->lastMoving_[index].store(
                    false,
                    std::memory_order_release);
                break;
            }
        }
    }

    void CreatureModeManagerObserver::ClearReplicatedAnimationMotions()
        noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr)
        {
            return;
        }
        AcquireSRWLockExclusive(&observer->replicatedAnimationMotionLock_);
        observer->replicatedAnimationMotions_.clear();
        ReleaseSRWLockExclusive(&observer->replicatedAnimationMotionLock_);
    }

    unsigned int CreatureModeManagerObserver::MirroredAnimationMotionCount() noexcept
    {
        CreatureModeManagerObserver* const observer = active_;
        return observer != nullptr
            ? observer->mirroredAnimationMotionCount_.load(
                std::memory_order_acquire)
            : 0;
    }

    int CreatureModeManagerObserver::FindWatchedOwner(void* owner) const noexcept
    {
        for (std::size_t index = 0; index < watchedOwners_.size(); ++index)
        {
            if (watchedOwners_[index].load(std::memory_order_acquire) == owner)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    CreatureModeManagerObserver::Snapshot CreatureModeManagerObserver::Capture(
        void* manager) noexcept
    {
        Snapshot snapshot;
        if (manager == nullptr)
        {
            return snapshot;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(manager);
            snapshot.owner = *reinterpret_cast<void* const*>(bytes + 0x04);
            snapshot.sentinel = *reinterpret_cast<void* const*>(bytes + 0x0C);
            snapshot.count = *reinterpret_cast<const std::uint32_t*>(bytes + 0x10);
            if (snapshot.count > 1024)
            {
                return {};
            }
            if (snapshot.sentinel != nullptr && snapshot.count != 0)
            {
                snapshot.activeNode = *static_cast<void* const*>(snapshot.sentinel);
                if (snapshot.activeNode != nullptr &&
                    snapshot.activeNode != snapshot.sentinel)
                {
                    const auto* const nodeBytes =
                        static_cast<const std::uint8_t*>(snapshot.activeNode);
                    snapshot.activeMode = *reinterpret_cast<void* const*>(
                        nodeBytes + 0x08);
                    if (snapshot.activeMode != nullptr)
                    {
                        snapshot.activeModeVtable =
                            *static_cast<void* const*>(snapshot.activeMode);
                        std::memcpy(
                            snapshot.modeDwords.data(),
                            snapshot.activeMode,
                            snapshot.modeDwords.size() * sizeof(std::uint32_t));
                        snapshot.activeModeHash = HashDwords(
                            snapshot.modeDwords.data(),
                            snapshot.modeDwords.size());
                    }
                }
            }
            snapshot.valid = snapshot.sentinel != nullptr || snapshot.count == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot = {};
        }
        return snapshot;
    }

    void CreatureModeManagerObserver::Report(
        const char* state,
        void* manager,
        int source,
        bool result,
        const Snapshot& before,
        const Snapshot& after,
        unsigned int ordinal) const
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto beforeVtable =
            reinterpret_cast<std::uintptr_t>(before.activeModeVtable);
        const auto afterVtable =
            reinterpret_cast<std::uintptr_t>(after.activeModeVtable);
        const std::uintptr_t beforeVtableRva =
            beforeVtable >= base ? beforeVtable - base : 0;
        const std::uintptr_t afterVtableRva =
            afterVtable >= base ? afterVtable - base : 0;

        char detail[768] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "manager=%p owner=%p source=%d ordinal=%u result=%s count=%u->%u active_mode=%p->%p active_vtable=%p->%p active_vtable_rva=0x%08X->0x%08X active_hash=0x%08X->0x%08X active_changed=%s valid=%s/%s thread=%lu",
            manager,
            after.owner != nullptr ? after.owner : before.owner,
            source,
            ordinal,
            result ? "true" : "false",
            before.count,
            after.count,
            before.activeMode,
            after.activeMode,
            before.activeModeVtable,
            after.activeModeVtable,
            static_cast<unsigned int>(beforeVtableRva),
            static_cast<unsigned int>(afterVtableRva),
            before.activeModeHash,
            after.activeModeHash,
            before.activeMode != after.activeMode ||
                    before.activeModeVtable != after.activeModeVtable
                ? "true"
                : "false",
            before.valid ? "true" : "false",
            after.valid ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event(state, detail);
    }

    void CreatureModeManagerObserver::ReportLocomotionEvaluation(
        void* mode,
        void* owner,
        void* evaluationContext,
        float motionX,
        float motionY,
        float angularVelocity,
        float evaluationSeconds,
        unsigned int ordinal) const
    {
        void* vtable = nullptr;
        std::uint32_t idleFrames = 0;
        std::uint32_t motionHoldFrames = 0;
        void* idleAnimation = nullptr;
        void* slowWalkAnimation = nullptr;
        void* walkAnimation = nullptr;
        void* jogAnimation = nullptr;
        void* runAnimation = nullptr;
        void* sprintAnimation = nullptr;
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(mode);
            vtable = *reinterpret_cast<void* const*>(bytes);
            idleFrames = *reinterpret_cast<const std::uint32_t*>(bytes + 0x1C);
            motionHoldFrames = *reinterpret_cast<const std::uint32_t*>(
                bytes + 0x20);
            idleAnimation = *reinterpret_cast<void* const*>(bytes + 0x38);
            slowWalkAnimation = *reinterpret_cast<void* const*>(bytes + 0x40);
            walkAnimation = *reinterpret_cast<void* const*>(bytes + 0x48);
            jogAnimation = *reinterpret_cast<void* const*>(bytes + 0x50);
            runAnimation = *reinterpret_cast<void* const*>(bytes + 0x58);
            sprintAnimation = *reinterpret_cast<void* const*>(bytes + 0x60);
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
        const std::uintptr_t vtableRva =
            vtableAddress >= base ? vtableAddress - base : 0;
        const float planarSpeed = std::sqrt(
            motionX * motionX + motionY * motionY);
        char detail[896] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "mode=%p owner=%p ordinal=%u evaluation_context=%p owner_motion_134_138=(%.6f,%.6f) planar_speed=%.6f angular_velocity=%.6f evaluation_seconds=%.4f mode_vtable=%p mode_vtable_rva=0x%08X idle_frames=%u motion_hold_frames=%u animations={idle:%p,slow_walk:%p,walk:%p,jog:%p,run:%p,sprint:%p} readable=%s thread=%lu",
            mode,
            owner,
            ordinal,
            evaluationContext,
            motionX,
            motionY,
            planarSpeed,
            angularVelocity,
            evaluationSeconds,
            vtable,
            static_cast<unsigned int>(vtableRva),
            idleFrames,
            motionHoldFrames,
            idleAnimation,
            slowWalkAnimation,
            walkAnimation,
            jogAnimation,
            runAnimation,
            sprintAnimation,
            readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("CreatureLocomotionModeEvaluated", detail);
    }

    bool __fastcall CreatureModeManagerObserver::ObserveAddSource(
        void* manager,
        void*,
        int source)
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || observer->originalAddSource_ == nullptr)
        {
            return false;
        }

        const Snapshot before = Capture(manager);
        const bool result = observer->originalAddSource_(manager, source);
        const Snapshot after = Capture(manager);
        const unsigned int ordinal = observer->addCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (source == 7 || ordinal <= 32)
        {
            observer->Report(
                "CreatureModeSourceAdded",
                manager,
                source,
                result,
                before,
                after,
                ordinal);
        }
        return result;
    }

    void __fastcall CreatureModeManagerObserver::ObserveRemoveSource(
        void* manager,
        void*,
        int source)
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr || observer->originalRemoveSource_ == nullptr)
        {
            return;
        }

        const Snapshot before = Capture(manager);
        observer->originalRemoveSource_(manager, source);
        const Snapshot after = Capture(manager);
        const unsigned int ordinal = observer->removeCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (source == 7 || ordinal <= 32)
        {
            observer->Report(
                "CreatureModeSourceRemoved",
                manager,
                source,
                true,
                before,
                after,
                ordinal);
        }
    }

    void __fastcall CreatureModeManagerObserver::ObserveLocomotionEvaluation(
        void* mode,
        void*,
        void* evaluationContext)
    {
        CreatureModeManagerObserver* const observer = active_;
        if (observer == nullptr ||
            observer->originalEvaluateLocomotion_ == nullptr)
        {
            return;
        }

        void* owner = nullptr;
        float motionX = 0.0f;
        float motionY = 0.0f;
        float angularVelocity = 0.0f;
        float evaluationSeconds = 0.0f;
        bool readable = false;
        bool motionMirrored = false;
        bool replicatedMotionApplied = false;
        const std::uint64_t now = GetTickCount64();
        __try
        {
            const auto* const modeBytes = static_cast<const std::uint8_t*>(mode);
            owner = *reinterpret_cast<void* const*>(modeBytes + 0x04);
            if (owner != nullptr)
            {
                auto* const ownerBytes = static_cast<std::uint8_t*>(owner);
                motionX = *reinterpret_cast<const float*>(ownerBytes + 0x134);
                motionY = *reinterpret_cast<const float*>(ownerBytes + 0x138);
                readable = std::isfinite(motionX) && std::isfinite(motionY);

                Vector3 replicatedLinearVelocity = {};
                float replicatedAngularVelocity = 0.0f;
                const bool hasReplicatedMotion =
                    observer->ReadReplicatedAnimationMotion(
                        owner,
                        now,
                        replicatedLinearVelocity,
                        replicatedAngularVelocity,
                        evaluationSeconds);
                if (hasReplicatedMotion)
                {
                    motionX = replicatedLinearVelocity.x *
                        evaluationSeconds;
                    motionY = replicatedLinearVelocity.y *
                        evaluationSeconds;
                    angularVelocity = replicatedAngularVelocity;
                    *reinterpret_cast<float*>(
                        ownerBytes +
                        ::fable::game::creature::native::
                            CreatureFrameFunctions::MotionXOffset) = motionX;
                    *reinterpret_cast<float*>(
                        ownerBytes +
                        ::fable::game::creature::native::
                            CreatureFrameFunctions::MotionYOffset) = motionY;
                    readable = true;
                    replicatedMotionApplied = true;
                }

                if (!replicatedMotionApplied &&
                    owner == observer->animationMotionTarget_.load(
                        std::memory_order_acquire))
                {
                    const auto* const sourceBytes = static_cast<const std::uint8_t*>(
                        observer->animationMotionSource_.load(
                            std::memory_order_acquire));
                    if (sourceBytes != nullptr)
                    {
                        const float sourceMotionX = *reinterpret_cast<const float*>(
                            sourceBytes +
                            ::fable::game::creature::native::CreatureFrameFunctions::MotionXOffset);
                        const float sourceMotionY = *reinterpret_cast<const float*>(
                            sourceBytes +
                            ::fable::game::creature::native::CreatureFrameFunctions::MotionYOffset);
                        const float sourceMotionSquared =
                            sourceMotionX * sourceMotionX +
                            sourceMotionY * sourceMotionY;
                        if (std::isfinite(sourceMotionX) &&
                            std::isfinite(sourceMotionY) &&
                            sourceMotionSquared > 0.00000001f)
                        {
                            *reinterpret_cast<float*>(
                                ownerBytes +
                                ::fable::game::creature::native::CreatureFrameFunctions::MotionXOffset) =
                                    sourceMotionX;
                            *reinterpret_cast<float*>(
                                ownerBytes +
                                ::fable::game::creature::native::CreatureFrameFunctions::MotionYOffset) =
                                    sourceMotionY;
                            motionX = sourceMotionX;
                            motionY = sourceMotionY;
                            readable = true;
                            motionMirrored = true;
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        const int watchIndex = readable
            ? observer->FindWatchedOwner(owner)
            : -1;
        unsigned int mirroredOrdinal = 0;
        if (motionMirrored)
        {
            mirroredOrdinal = observer->mirroredAnimationMotionCount_.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
        }
        observer->originalEvaluateLocomotion_(mode, evaluationContext);
        if (mirroredOrdinal == 1 || mirroredOrdinal == 2 ||
            mirroredOrdinal == 10 || mirroredOrdinal == 60)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u source_player=%p target_creature=%p owner_motion_134_138=(%.6f,%.6f) thread=%lu",
                mirroredOrdinal,
                observer->animationMotionSource_.load(std::memory_order_acquire),
                owner,
                motionX,
                motionY,
                static_cast<unsigned long>(GetCurrentThreadId()));
            observer->diagnostics_.Event(
                "CreatureAnimationMotionMirrored",
                detail);
        }
        if (watchIndex < 0)
        {
            return;
        }

        const std::size_t index = static_cast<std::size_t>(watchIndex);
        const unsigned int ordinal =
            observer->locomotionEvaluationCounts_[index].fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
        const bool moving = motionX * motionX + motionY * motionY > 0.00000001f;
        const bool previousMoving = observer->lastMoving_[index].exchange(
            moving,
            std::memory_order_acq_rel);
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60 ||
            moving != previousMoving)
        {
            observer->ReportLocomotionEvaluation(
                mode,
                owner,
                evaluationContext,
                motionX,
                motionY,
                angularVelocity,
                evaluationSeconds,
                ordinal);
        }
    }
}
