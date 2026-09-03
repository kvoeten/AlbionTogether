#include "AiBrainUpdateObserver.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace fable::game::creature::ai
{
    std::atomic<AiBrainUpdateObserver*> AiBrainUpdateObserver::active_{nullptr};
    std::mutex AiBrainUpdateObserver::stateGroupLeaseMutex_;
    std::atomic<native::AiBrainFunctions::UpdatePointer>
        AiBrainUpdateObserver::processBrainOriginal_{nullptr};
    core::hooking::InlineHook*
        AiBrainUpdateObserver::stateGroupProcessHook_ = nullptr;
    std::atomic<native::AiBrainFunctions::StateGroupDecisionPointer>
        AiBrainUpdateObserver::stateGroupProcessOriginal_{nullptr};

    bool AiBrainUpdateObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log("Hook: AI brain observation is only supported by the x86 client.");
        return false;
#else
        const AiBrainUpdateObserver* const active = active_.load(
            std::memory_order_acquire);
        if (active != nullptr && active != this)
        {
            diagnostics_.Log("Hook: another AI brain update observer is already active.");
            return false;
        }

        void** slot = nullptr;
        native::AiBrainFunctions::UpdatePointer original = nullptr;
        if (!native::AiBrainFunctions::ResolveUpdateSlot(
                gameModule,
                &slot,
                original))
        {
            diagnostics_.Log("Hook: CAIBrain update definition failed validation.");
            return false;
        }

        void* stateGroupTarget = nullptr;
        native::AiBrainFunctions::StateGroupDecisionPointer
            stateGroupOriginal = stateGroupProcessOriginal_;
        if (stateGroupProcessHook_ == nullptr)
        {
            if (!native::AiBrainFunctions::ResolveStateGroupDispatcher(
                    gameModule,
                    &stateGroupTarget,
                    stateGroupOriginal))
            {
                diagnostics_.Log(
                    "Hook: state-group decision dispatcher failed validation.");
                return false;
            }
        }

        // The replacement lives in this DLL, not in the game executable.
        // A process-lifetime trampoline also requires process-lifetime code.
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&ObserveStateGroup), &pinnedModule))
        {
            return false;
        }
        void* const expected = reinterpret_cast<void*>(original);
        void* const replacement = reinterpret_cast<void*>(&AiBrainUpdateObserver::Observe);
        processBrainOriginal_ = original;
        if (!vtablePatch_.Install(
                slot, &expected, sizeof(expected), &replacement, sizeof(replacement)))
        {
            diagnostics_.Log("Hook: CAIBrain update vtable patch installation failed.");
            return false;
        }

        if (stateGroupProcessHook_ == nullptr)
        {
            // The first dispatcher instruction is an absolute 7-byte cmp.
            // Build its relocated expected bytes from the loaded module
            // rather than baking the preferred image base into the guard.
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
            const std::uint32_t stateGroupGlobal = static_cast<std::uint32_t>(
                base + native::AiBrainFunctions::StateGroupGlobalRva);
            std::array<
                std::uint8_t,
                native::AiBrainFunctions::StateGroupDisplacedBytes>
                stateGroupExpected = {
                    0x80, 0x3D,
                    static_cast<std::uint8_t>(stateGroupGlobal & 0xFF),
                    static_cast<std::uint8_t>((stateGroupGlobal >> 8) & 0xFF),
                    static_cast<std::uint8_t>((stateGroupGlobal >> 16) & 0xFF),
                    static_cast<std::uint8_t>((stateGroupGlobal >> 24) & 0xFF),
                    0x00};
            try
            {
                stateGroupProcessHook_ = new core::hooking::InlineHook();
            }
            catch (...)
            {
                stateGroupProcessHook_ = nullptr;
            }
            if (stateGroupProcessHook_ == nullptr ||
                !stateGroupProcessHook_->Install(
                    stateGroupTarget,
                    stateGroupExpected.data(),
                    stateGroupExpected.size(),
                    reinterpret_cast<void*>(&AiBrainUpdateObserver::ObserveStateGroup),
                    native::AiBrainFunctions::StateGroupDisplacedBytes))
            {
                delete stateGroupProcessHook_;
                stateGroupProcessHook_ = nullptr;
                (void)vtablePatch_.Shutdown();
                diagnostics_.Log(
                    "Hook: state-group decision dispatcher installation failed.");
                return false;
            }
            stateGroupProcessOriginal_ = reinterpret_cast<
                native::AiBrainFunctions::StateGroupDecisionPointer>(
                    stateGroupProcessHook_->Original());
        }

        original_ = original;
        active_.store(this, std::memory_order_release);

        char detail[192] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "slot=%p original=%p tracked_limit=%zu",
            slot,
            reinterpret_cast<void*>(original),
            TrackedBrainLimit);
        diagnostics_.Log(
            "Hook: CAIBrain and state-group decision boundaries installed.");
        diagnostics_.Event("AiBrainUpdateObserverReady", detail);
        return true;
#endif
    }

    bool AiBrainUpdateObserver::IsInstalled() const noexcept
    {
        return active_.load(std::memory_order_acquire) == this &&
            original_ != nullptr &&
            stateGroupProcessOriginal_ != nullptr && vtablePatch_.IsInstalled() &&
            stateGroupProcessHook_ != nullptr &&
            stateGroupProcessHook_->IsInstalled();
    }

    void AiBrainUpdateObserver::Shutdown() noexcept
    {
        // Drain policy consumers first. The dispatcher trampoline remains
        // process-resident; a call that already read the brain vtable slot
        // likewise retains a native passthrough after this observer detaches.
        SetExecutionSink(nullptr, nullptr);
        SetStateGroupExecutionSink(nullptr, nullptr);
        if (vtablePatch_.IsInstalled())
        {
            if (!vtablePatch_.Shutdown())
            {
                // Another mod may own the slot now and still chain through
                // our callback. Detach our policy without overwriting theirs.
                diagnostics_.Log("Hook: CAIBrain vtable restore skipped because its slot changed; detaching policy.");
            }
        }
        if (vtablePatch_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: CAIBrain observer bytes restored, but vtable protection restoration failed.");
        }
        // Detach under the same lease used when a callback reads active_. A
        // prior sink clear alone does not prevent a new reader from entering.
        std::lock_guard<std::mutex> lease(stateGroupLeaseMutex_);
        AiBrainUpdateObserver* expected = this;
        (void)active_.compare_exchange_strong(expected, nullptr,
            std::memory_order_acq_rel, std::memory_order_acquire);
        original_ = nullptr;
        diagnostics_ = {};
    }

    void AiBrainUpdateObserver::SetExecutionSink(
        ExecutionSink sink,
        void* context) noexcept
    {
        std::lock_guard<std::mutex> lease(stateGroupLeaseMutex_);
        if (sink == nullptr)
        {
            executionSink_.store(nullptr, std::memory_order_release);
            executionSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        executionSinkContext_.store(context, std::memory_order_release);
        executionSink_.store(sink, std::memory_order_release);
    }

    void AiBrainUpdateObserver::SetStateGroupExecutionSink(
        StateGroupExecutionSink sink,
        void* context) noexcept
    {
        std::lock_guard<std::mutex> lease(stateGroupLeaseMutex_);
        if (sink == nullptr)
        {
            stateGroupExecutionSink_.store(nullptr, std::memory_order_release);
            stateGroupExecutionSinkContext_.store(
                nullptr, std::memory_order_release);
            return;
        }
        stateGroupExecutionSinkContext_.store(
            context, std::memory_order_release);
        stateGroupExecutionSink_.store(sink, std::memory_order_release);
    }

    unsigned int AiBrainUpdateObserver::ObservedBrainCount() const noexcept
    {
        return observedBrainCount_.load(std::memory_order_acquire);
    }

    bool AiBrainUpdateObserver::TrackFirstUpdate(
        void* brain,
        unsigned int& ordinal) noexcept
    {
        ordinal = 0;
        for (auto& tracked : trackedBrains_)
        {
            if (tracked.load(std::memory_order_acquire) == brain)
            {
                return false;
            }
        }
        for (auto& tracked : trackedBrains_)
        {
            void* expected = nullptr;
            if (tracked.compare_exchange_strong(
                    expected,
                    brain,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                ordinal = observedBrainCount_.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
                return true;
            }
            if (expected == brain)
            {
                return false;
            }
        }
        return false;
    }

    void* AiBrainUpdateObserver::ResolveOwnerThing(void* brain) noexcept
    {
        void* ownerThing = nullptr;
        __try
        {
            if (brain != nullptr)
            {
                ownerThing = *reinterpret_cast<void* const*>(
                    static_cast<const std::uint8_t*>(brain) +
                    native::AiBrainFunctions::ContextBeginOffset +
                    sizeof(void*));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ownerThing = nullptr;
        }
        return ownerThing;
    }

    bool AiBrainUpdateObserver::ShouldExecute(void* ownerThing) const noexcept
    {
        const ExecutionSink sink = executionSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr)
        {
            return true;
        }
        return sink(
            executionSinkContext_.load(std::memory_order_acquire),
            ownerThing);
    }

    bool AiBrainUpdateObserver::ShouldExecuteStateGroup(
        void* creature,
        int frameTime,
        void* nativeProposal) const noexcept
    {
        const StateGroupExecutionSink sink = stateGroupExecutionSink_.load(
            std::memory_order_acquire);
        if (sink == nullptr)
        {
            return true;
        }
        return sink(
            stateGroupExecutionSinkContext_.load(std::memory_order_acquire),
            creature,
            frameTime,
            nativeProposal);
    }

    void AiBrainUpdateObserver::Report(
        void* brain,
        void* ownerThing,
        unsigned int ordinal,
        bool executed) const
    {
        void* fiber = nullptr;
        void* fiberVtable = nullptr;
        void* fiberDispatch = nullptr;
        void* definition = nullptr;
        void* context0 = nullptr;
        void* context1 = nullptr;
        bool fiberPaused = false;
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(brain);
            fiber = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::FiberOffset);
            definition = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::DefinitionOffset);
            context0 = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::ContextBeginOffset);
            context1 = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::ContextBeginOffset + sizeof(void*));
            if (fiber != nullptr)
            {
                const auto* const fiberBytes = static_cast<const std::uint8_t*>(fiber);
                fiberPaused = fiberBytes[native::AiBrainFunctions::FiberPausedOffset] != 0;
                fiberVtable = *reinterpret_cast<void* const*>(fiberBytes);
                if (fiberVtable != nullptr)
                {
                    fiberDispatch = static_cast<void**>(fiberVtable)[
                        native::AiBrainFunctions::FiberDispatchSlot];
                }
            }
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "brain=%p ordinal=%u fiber=%p fiber_vtable=%p dispatch=%p paused=%s definition=%p context0=%p owner_thing=%p executed=%s readable=%s thread=%lu",
            brain,
            ordinal,
            fiber,
            fiberVtable,
            fiberDispatch,
            fiberPaused ? "true" : "false",
            definition,
            context0,
            ownerThing != nullptr ? ownerThing : context1,
            executed ? "true" : "false",
            readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("AiBrainFirstUpdateObserved", detail);
    }

    void __fastcall AiBrainUpdateObserver::Observe(void* brain, void*)
    {
        const auto original = processBrainOriginal_.load(std::memory_order_acquire);
        bool execute = true;
        {
            std::lock_guard<std::mutex> lease(stateGroupLeaseMutex_);
            AiBrainUpdateObserver* const observer = active_.load(
                std::memory_order_acquire);
            if (observer != nullptr)
            {
                unsigned int ordinal = 0;
                const bool firstUpdate = brain != nullptr &&
                    observer->TrackFirstUpdate(brain, ordinal);
                void* const ownerThing = ResolveOwnerThing(brain);
                execute = observer->ShouldExecute(ownerThing);
                if (firstUpdate)
                    observer->Report(brain, ownerThing, ordinal, execute);
            }
        }
        if (execute && original != nullptr) original(brain);
    }

    bool __fastcall AiBrainUpdateObserver::ObserveStateGroup(
        void* creature,
        void*,
        int frameTime,
        void* nativeProposal)
    {
        const auto passthrough = stateGroupProcessOriginal_.load(std::memory_order_acquire);
        if (passthrough == nullptr)
        {
            return false;
        }
        bool execute = true;
        {
            std::lock_guard<std::mutex> lease(stateGroupLeaseMutex_);
            AiBrainUpdateObserver* const observer = active_.load(
                std::memory_order_acquire);
            if (observer != nullptr)
            {
                execute = observer->ShouldExecuteStateGroup(
                    creature, frameTime, nativeProposal);
            }
        }
        if (!execute)
        {
            return false;
        }
        // Do not hold the observer lease while entering the native original;
        // native arbitration may re-enter unrelated callback paths.
        return passthrough(creature, frameTime, nativeProposal);
    }
}
