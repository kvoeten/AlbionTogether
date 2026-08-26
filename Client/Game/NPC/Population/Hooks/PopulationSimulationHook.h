#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/NPC/Population/Native/PopulationSimulationFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::npc::population
{
    enum class PopulationSimulationKind : std::uint8_t
    {
        AlbionWorld = 1,
        HighDetailMap = 2,
    };

    struct PopulationSimulationState final
    {
        static constexpr std::size_t RegionCount = 4;
        static constexpr std::size_t PopulationKindCount = 3;

        std::uint8_t region = 0;
        bool active = false;
        std::array<std::int32_t, PopulationKindCount> targetCounts = {};
        std::array<float, RegionCount> regionFactors = {};
    };

    // Fences retail population callbacks without replacing their behavior.
    // The multiplayer policy decides which peer may enter each native pass.
    class PopulationSimulationHook final
    {
    public:
        using ExecutionSink = bool(*)(
            void* context,
            PopulationSimulationKind kind);
        using StateSink = void(*)(
            void* context,
            const PopulationSimulationState& state);
        using StateSource = bool(*)(
            void* context,
            PopulationSimulationState& state);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetExecutionSink(
            ExecutionSink sink,
            void* context) noexcept;
        void SetStateSink(StateSink sink, void* context) noexcept;
        void SetStateSource(StateSource source, void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 16;

        static void __fastcall ProcessAlbion(void* scriptObject, void* unused);
        static void __fastcall ProcessHighDetail(
            void* scriptObject,
            void* unused);
        bool ShouldExecute(PopulationSimulationKind kind) const noexcept;
        void CaptureState(void* scriptObject) noexcept;
        bool ApplyState(void* scriptObject) noexcept;
        [[nodiscard]] static bool ReadState(
            void* scriptObject,
            PopulationSimulationState& state) noexcept;
        [[nodiscard]] static bool WriteState(
            void* scriptObject,
            const PopulationSimulationState& state) noexcept;
        [[nodiscard]] static bool IsSane(
            const PopulationSimulationState& state) noexcept;
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            core::hooking::InlineHook& detour) noexcept;
        bool RestoreDetour(core::hooking::InlineHook& detour) noexcept;
        void ReportSuppressed(PopulationSimulationKind kind) noexcept;

        static PopulationSimulationHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::PopulationSimulationFunctions::SimulationPointer
            originalProcessAlbion_ = nullptr;
        native::PopulationSimulationFunctions::SimulationPointer
            originalProcessHighDetail_ = nullptr;
        core::hooking::InlineHook processAlbionDetour_;
        core::hooking::InlineHook highDetailDetour_;
        std::atomic<ExecutionSink> sink_{nullptr};
        std::atomic<void*> sinkContext_{nullptr};
        std::atomic<StateSink> stateSink_{nullptr};
        std::atomic<void*> stateSinkContext_{nullptr};
        std::atomic<StateSource> stateSource_{nullptr};
        std::atomic<void*> stateSourceContext_{nullptr};
        std::atomic_uint suppressedCount_{0};
    };
}
