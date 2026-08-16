#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/World/Travel/Native/WorldTravelFunctions.h"
#include "Game/World/Travel/WorldTravelPreparation.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::world::travel
{
    // Pairs Fable's connected region entrance with UE3's subsequent level-name
    // preparation. Observation occurs before either native transition routine
    // mutates world state.
    class WorldTravelObserver final
    {
    public:
        // Returns true when retail loading may continue immediately. False
        // asks the observer to retain and later replay PrepareMapChange.
        using PreparationSink = bool(*)(
            void* context,
            const WorldTravelPreparation& preparation) noexcept;
        using DepartureSink = void(*)(
            void* context,
            const WorldTravelPreparation& preparation) noexcept;

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetPreparationSink(
            PreparationSink sink,
            void* context) noexcept;
        void SetDepartureSink(
            DepartureSink sink,
            void* context) noexcept;
        bool ResumeDeferredMapChange(
            const char* destinationMapName,
            std::uint16_t destinationMapId) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 32;
        static constexpr std::size_t MaximumLevelNames = 32;
        static constexpr std::size_t MaximumDisplacedBytes =
            native::WorldTravelFunctions::RegionExitDisplacedBytes;

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::size_t displacedBytes = 0;
            std::array<std::uint8_t, MaximumDisplacedBytes> originalBytes = {};
        };

        struct DeferredMapChange final
        {
            void* worldInfo = nullptr;
            WorldTravelPreparation preparation = {};
            std::array<native::NativeName, MaximumLevelNames> levelNames = {};
            std::int32_t levelNameCount = 0;
            bool active = false;
        };

        static void __fastcall RegionExitTriggered(
            void* component,
            void* unused);
        static void __fastcall PrepareMapChangeObserved(
            void* worldInfo,
            void* unused,
            const native::NativeNameArray* levelNames);
        void ObserveRegionExit(void* component) noexcept;
        bool ObservePrepareMapChange(
            void* worldInfo,
            const native::NativeNameArray* levelNames) noexcept;
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            std::size_t displacedBytes,
            Detour& detour) noexcept;
        static void RestoreDetour(Detour& detour) noexcept;
        void ReportRegionExit(
            const WorldTravelPreparation& preparation) noexcept;
        void ReportPrepareMapChange(
            const WorldTravelPreparation& preparation,
            std::int32_t levelNameCount) noexcept;

        static WorldTravelObserver* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        native::WorldTravelFunctions::ResolveConnectedThingPointer
            resolveConnectedThing_ = nullptr;
        native::WorldTravelFunctions::RegionExitTriggerPointer
            originalRegionExitTrigger_ = nullptr;
        native::WorldTravelFunctions::PrepareMapChangePointer
            originalPrepareMapChange_ = nullptr;
        Detour regionExitDetour_ = {};
        Detour prepareMapChangeDetour_ = {};
        WorldTravelPreparation pending_ = {};
        DeferredMapChange deferred_ = {};
        std::atomic<PreparationSink> sink_{nullptr};
        std::atomic<void*> sinkContext_{nullptr};
        std::atomic<DepartureSink> departureSink_{nullptr};
        std::atomic<void*> departureSinkContext_{nullptr};
        std::atomic_uint eventCount_{0};
    };
}
