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
        if (active_ == this)
        {
            diagnostics_.Log(
                "Hook: population installation is partially active; shutdown is required before retrying.");
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
                processAlbionDetour_.Original());
        if (!InstallDetour(
                highDetailTarget,
                reinterpret_cast<void*>(
                    &PopulationSimulationHook::ProcessHighDetail),
                highDetailDetour_))
        {
            const bool rollbackRestored = RestoreDetour(processAlbionDetour_);
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: population rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalProcessAlbion_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalProcessHighDetail_ = reinterpret_cast<
            native::PopulationSimulationFunctions::SimulationPointer>(
                highDetailDetour_.Original());

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "albion=%p albion_trampoline=%p high_detail=%p high_detail_trampoline=%p",
            processAlbionTarget,
            processAlbionDetour_.Original(),
            highDetailTarget,
            highDetailDetour_.Original());
        diagnostics_.Event("PopulationSimulationHookReady", detail);
        return true;
#endif
    }

    void PopulationSimulationHook::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(highDetailDetour_) && allRestored;
        allRestored = RestoreDetour(processAlbionDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: population shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetStateSink(nullptr, nullptr);
        SetExecutionSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalProcessHighDetail_ = nullptr;
        originalProcessAlbion_ = nullptr;
        diagnostics_ = {};
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
            processAlbionDetour_.IsInstalled() &&
            highDetailDetour_.IsInstalled();
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
        core::hooking::InlineHook& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::PopulationSimulationFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr ||
            detour.IsInstalled())
        {
            return false;
        }
        return detour.Install(
            target,
            target,
            displacedBytes,
            replacement,
            displacedBytes);
    }

    bool PopulationSimulationHook::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
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
