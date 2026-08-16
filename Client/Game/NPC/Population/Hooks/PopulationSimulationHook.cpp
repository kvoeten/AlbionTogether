#include "PopulationSimulationHook.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fable::game::npc::population
{
    PopulationSimulationHook* PopulationSimulationHook::active_ = nullptr;

    bool PopulationSimulationHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: population simulation fencing is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        std::uint8_t* processAlbionTarget = nullptr;
        std::uint8_t* highDetailTarget = nullptr;
        if (!native::PopulationSimulationFunctions::ResolveProcessAlbion(
                gameModule,
                processAlbionTarget) ||
            !native::PopulationSimulationFunctions::ResolveHighDetail(
                gameModule,
                highDetailTarget))
        {
            diagnostics_.Log(
                "Hook: population simulation definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                processAlbionTarget,
                reinterpret_cast<void*>(
                    &PopulationSimulationHook::ProcessAlbion),
                processAlbionDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalProcessAlbion_ = reinterpret_cast<
            native::PopulationSimulationFunctions::SimulationPointer>(
                processAlbionDetour_.trampoline);
        if (!InstallDetour(
                highDetailTarget,
                reinterpret_cast<void*>(
                    &PopulationSimulationHook::ProcessHighDetail),
                highDetailDetour_))
        {
            RestoreDetour(processAlbionDetour_);
            originalProcessAlbion_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalProcessHighDetail_ = reinterpret_cast<
            native::PopulationSimulationFunctions::SimulationPointer>(
                highDetailDetour_.trampoline);

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "albion=%p albion_trampoline=%p high_detail=%p high_detail_trampoline=%p",
            processAlbionDetour_.target,
            processAlbionDetour_.trampoline,
            highDetailDetour_.target,
            highDetailDetour_.trampoline);
        diagnostics_.Event("PopulationSimulationHookReady", detail);
        return true;
#endif
    }

    void PopulationSimulationHook::SetExecutionSink(
        ExecutionSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            sink_.store(nullptr, std::memory_order_release);
            sinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        sinkContext_.store(context, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }

    void PopulationSimulationHook::SetStateSink(
        StateSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            stateSink_.store(nullptr, std::memory_order_release);
            stateSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        stateSinkContext_.store(context, std::memory_order_release);
        stateSink_.store(sink, std::memory_order_release);
    }

    void PopulationSimulationHook::SetStateSource(
        StateSource source,
        void* context) noexcept
    {
        if (source == nullptr)
        {
            stateSource_.store(nullptr, std::memory_order_release);
            stateSourceContext_.store(nullptr, std::memory_order_release);
            return;
        }
        stateSourceContext_.store(context, std::memory_order_release);
        stateSource_.store(source, std::memory_order_release);
    }

    bool PopulationSimulationHook::IsInstalled() const noexcept
    {
        return active_ == this && originalProcessAlbion_ != nullptr &&
            originalProcessHighDetail_ != nullptr &&
            processAlbionDetour_.target != nullptr &&
            highDetailDetour_.target != nullptr;
    }

    void __fastcall PopulationSimulationHook::ProcessAlbion(
        void* scriptObject,
        void*)
    {
        PopulationSimulationHook* const hook = active_;
        if (hook == nullptr || hook->originalProcessAlbion_ == nullptr)
        {
            return;
        }
        if (hook->ShouldExecute(PopulationSimulationKind::AlbionWorld))
        {
            hook->originalProcessAlbion_(scriptObject);
            hook->CaptureState(scriptObject);
        }
        else
        {
            hook->ReportSuppressed(PopulationSimulationKind::AlbionWorld);
        }
    }

    void __fastcall PopulationSimulationHook::ProcessHighDetail(
        void* scriptObject,
        void*)
    {
        PopulationSimulationHook* const hook = active_;
        if (hook == nullptr || hook->originalProcessHighDetail_ == nullptr)
        {
            return;
        }
        if (hook->ShouldExecute(PopulationSimulationKind::HighDetailMap) &&
            hook->ApplyState(scriptObject))
        {
            hook->originalProcessHighDetail_(scriptObject);
        }
        else
        {
            hook->ReportSuppressed(PopulationSimulationKind::HighDetailMap);
        }
    }

    bool PopulationSimulationHook::ShouldExecute(
        PopulationSimulationKind kind) const noexcept
    {
        const ExecutionSink sink = sink_.load(std::memory_order_acquire);
        return sink == nullptr || sink(
            sinkContext_.load(std::memory_order_acquire),
            kind);
    }

    void PopulationSimulationHook::CaptureState(void* scriptObject) noexcept
    {
        const StateSink sink = stateSink_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        PopulationSimulationState state;
        if (ReadState(scriptObject, state))
        {
            sink(
                stateSinkContext_.load(std::memory_order_acquire),
                state);
        }
    }

    bool PopulationSimulationHook::ApplyState(void* scriptObject) noexcept
    {
        const StateSource source = stateSource_.load(
            std::memory_order_acquire);
        if (source == nullptr)
        {
            return true;
        }
        PopulationSimulationState state;
        return source(
                stateSourceContext_.load(std::memory_order_acquire),
                state) &&
            WriteState(scriptObject, state);
    }

    bool PopulationSimulationHook::ReadState(
        void* scriptObject,
        PopulationSimulationState& state) noexcept
    {
        state = {};
        if (scriptObject == nullptr)
        {
            return false;
        }
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(
                scriptObject);
            std::uint32_t region = 0;
            std::memcpy(
                state.regionFactors.data(),
                bytes + 0x5C,
                sizeof(float) * state.regionFactors.size());
            state.active = bytes[0x6C] != 0;
            std::memcpy(&region, bytes + 0x70, sizeof(region));
            state.region = static_cast<std::uint8_t>(region);
            std::memcpy(
                state.targetCounts.data(),
                bytes + 0x8C,
                sizeof(std::int32_t) * state.targetCounts.size());
            readable = region < PopulationSimulationState::RegionCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        return readable && IsSane(state);
    }

    bool PopulationSimulationHook::WriteState(
        void* scriptObject,
        const PopulationSimulationState& state) noexcept
    {
        if (scriptObject == nullptr || !IsSane(state))
        {
            return false;
        }
        bool written = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(scriptObject);
            const std::uint32_t region = state.region;
            std::memcpy(
                bytes + 0x5C,
                state.regionFactors.data(),
                sizeof(float) * state.regionFactors.size());
            bytes[0x6C] = state.active ? 1u : 0u;
            std::memcpy(bytes + 0x70, &region, sizeof(region));
            std::memcpy(
                bytes + 0x8C,
                state.targetCounts.data(),
                sizeof(std::int32_t) * state.targetCounts.size());
            written = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            written = false;
        }
        return written;
    }

    bool PopulationSimulationHook::IsSane(
        const PopulationSimulationState& state) noexcept
    {
        if (state.region >= PopulationSimulationState::RegionCount)
        {
            return false;
        }
        for (const std::int32_t count : state.targetCounts)
        {
            if (count < 0 || count > 10000)
            {
                return false;
            }
        }
        for (const float factor : state.regionFactors)
        {
            if (!std::isfinite(factor) || factor < -1000.0f ||
                factor > 1000.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool PopulationSimulationHook::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        Detour& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::PopulationSimulationFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr ||
            detour.target != nullptr)
        {
            return false;
        }
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(detour.originalBytes.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t replacementDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            replacementDisplacement < INT32_MIN ||
            replacementDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        trampoline[displacedBytes] = 0xE9;
        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t replacementRelative =
            static_cast<std::int32_t>(replacementDisplacement);
        std::memcpy(
            patch.data() + 1,
            &replacementRelative,
            sizeof(replacementRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        detour.target = target;
        detour.trampoline = trampoline;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(target, patch.size(), previousProtection, &discarded);
        return true;
    }

    void PopulationSimulationHook::RestoreDetour(Detour& detour) noexcept
    {
        if (detour.target == nullptr)
        {
            return;
        }
        DWORD previousProtection = 0;
        if (VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            std::memcpy(
                detour.target,
                detour.originalBytes.data(),
                detour.originalBytes.size());
            FlushInstructionCache(
                GetCurrentProcess(),
                detour.target,
                detour.originalBytes.size());
            DWORD discarded = 0;
            VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                previousProtection,
                &discarded);
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
    }

    void PopulationSimulationHook::ReportSuppressed(
        PopulationSimulationKind kind) noexcept
    {
        const unsigned int ordinal = suppressedCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u pass=%s",
            ordinal,
            kind == PopulationSimulationKind::AlbionWorld
                ? "albion-world"
                : "high-detail-map");
        diagnostics_.Event("PopulationSimulationSuppressed", detail);
    }
}
