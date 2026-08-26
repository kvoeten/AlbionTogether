#include "WorldTravelObserver.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
    constexpr std::array<std::uint8_t, 4> RegionExitPrefix = {
        0x83, 0xEC, 0x34, 0xA1};
    constexpr std::array<std::uint8_t, 3> PrepareMapChangePrefix = {
        0x6A, 0xFF, 0x68};
}

namespace fable::game::world::travel
{
    WorldTravelObserver* WorldTravelObserver::active_ = nullptr;

    bool WorldTravelObserver::Install(
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
            "Hook: world travel observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        if (active_ == this)
        {
            diagnostics_.Log(
                "Hook: world-travel installation is partially active; shutdown is required before retrying.");
            return false;
        }
        std::uint8_t* regionExitTarget = nullptr;
        std::uint8_t* prepareMapChangeTarget = nullptr;
        if (!native::WorldTravelFunctions::ResolveRegionExitTrigger(
                gameModule,
                regionExitTarget) ||
            !native::WorldTravelFunctions::ResolveConnectedThing(
                gameModule,
                resolveConnectedThing_) ||
            !native::WorldTravelFunctions::ResolvePrepareMapChange(
                gameModule,
                prepareMapChangeTarget))
        {
            diagnostics_.Log(
                "Hook: region-exit or UE3 prepare-map definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!regionExitDetour_.Install(
                regionExitTarget,
                RegionExitPrefix.data(),
                RegionExitPrefix.size(),
                reinterpret_cast<void*>(&WorldTravelObserver::RegionExitTriggered),
                native::WorldTravelFunctions::RegionExitDisplacedBytes))
        {
            active_ = nullptr;
            return false;
        }
        originalRegionExitTrigger_ = reinterpret_cast<
            native::WorldTravelFunctions::RegionExitTriggerPointer>(
                regionExitDetour_.Original());

        if (!prepareMapChangeDetour_.Install(
                prepareMapChangeTarget,
                PrepareMapChangePrefix.data(),
                PrepareMapChangePrefix.size(),
                reinterpret_cast<void*>(
                    &WorldTravelObserver::PrepareMapChangeObserved),
                native::WorldTravelFunctions::PrepareMapChangeDisplacedBytes))
        {
            const bool regionRestored = regionExitDetour_.Shutdown();
            if (regionRestored)
            {
                originalRegionExitTrigger_ = nullptr;
                if (active_ == this) active_ = nullptr;
            }
            return false;
        }
        originalPrepareMapChange_ = reinterpret_cast<
            native::WorldTravelFunctions::PrepareMapChangePointer>(
                prepareMapChangeDetour_.Original());

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "region_exit=%p region_exit_trampoline=%p prepare_map=%p prepare_map_trampoline=%p source_map_offset=0x9A connected_uid_offset=0x1C connected_handle_offset=0x26",
            reinterpret_cast<void*>(regionExitTarget),
            regionExitDetour_.Original(),
            reinterpret_cast<void*>(prepareMapChangeTarget),
            prepareMapChangeDetour_.Original());
        diagnostics_.Event("WorldTravelObserverReady", detail);
        return true;
#endif
    }

    void WorldTravelObserver::Shutdown() noexcept
    {
        const bool prepareRestored = prepareMapChangeDetour_.Shutdown();
        const bool regionRestored = regionExitDetour_.Shutdown();
        if (!prepareRestored || !regionRestored)
        {
            diagnostics_.Log(
                "Hook: world-travel shutdown deferred because a target is owned by another hook.");
            return;
        }
        if (prepareMapChangeDetour_.ProtectionRestoreFailed() ||
            regionExitDetour_.ProtectionRestoreFailed())
        {
            diagnostics_.Event(
                "WorldTravelHookProtectionRestoreWarning",
                "original-bytes-restored");
        }
        SetDepartureSink(nullptr, nullptr);
        SetPreparationSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalPrepareMapChange_ = nullptr;
        originalRegionExitTrigger_ = nullptr;
        resolveConnectedThing_ = nullptr;
        gameModule_ = nullptr;
        pending_ = {};
        deferred_ = {};
        diagnostics_ = {};
    }

    void WorldTravelObserver::SetPreparationSink(
        PreparationSink sink,
        void* context) noexcept
    {
        sinkContext_.store(context, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }

    void WorldTravelObserver::SetDepartureSink(
        DepartureSink sink,
        void* context) noexcept
    {
        departureSinkContext_.store(context, std::memory_order_release);
        departureSink_.store(sink, std::memory_order_release);
    }

    bool WorldTravelObserver::ResumeDeferredMapChange(
        const char* destinationMapName,
        std::uint16_t destinationMapId) noexcept
    {
        if (!deferred_.active || destinationMapName == nullptr ||
            destinationMapName[0] == '\0' ||
            std::strcmp(
                deferred_.preparation.destinationMapName.data(),
                destinationMapName) != 0 ||
            (deferred_.preparation.destinationMapId != 0 &&
                destinationMapId != 0 &&
                deferred_.preparation.destinationMapId != destinationMapId) ||
            originalPrepareMapChange_ == nullptr)
        {
            return false;
        }

        DeferredMapChange replay = deferred_;
        deferred_ = {};
        native::NativeNameArray levelNames;
        levelNames.data = replay.levelNames.data();
        levelNames.count = replay.levelNameCount;
        levelNames.capacity = replay.levelNameCount;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "destination_map=%s destination_map_id=%u level_name_count=%d",
            destinationMapName,
            static_cast<unsigned int>(destinationMapId),
            replay.levelNameCount);
        diagnostics_.Event("WorldTravelConstructionReleased", detail);
        originalPrepareMapChange_(replay.worldInfo, &levelNames);
        return true;
    }

    bool WorldTravelObserver::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr &&
            resolveConnectedThing_ != nullptr &&
            originalRegionExitTrigger_ != nullptr &&
            originalPrepareMapChange_ != nullptr &&
            regionExitDetour_.IsInstalled() &&
            prepareMapChangeDetour_.IsInstalled();
    }

    void __fastcall WorldTravelObserver::RegionExitTriggered(
        void* component,
        void*)
    {
        WorldTravelObserver* const observer = active_;
        if (observer == nullptr || observer->originalRegionExitTrigger_ == nullptr)
        {
            return;
        }
        observer->ObserveRegionExit(component);
        observer->originalRegionExitTrigger_(component);
    }

    void __fastcall WorldTravelObserver::PrepareMapChangeObserved(
        void* worldInfo,
        void*,
        const native::NativeNameArray* levelNames)
    {
        WorldTravelObserver* const observer = active_;
        if (observer == nullptr || observer->originalPrepareMapChange_ == nullptr)
        {
            return;
        }
        if (observer->ObservePrepareMapChange(worldInfo, levelNames))
        {
            observer->originalPrepareMapChange_(worldInfo, levelNames);
        }
    }

    void WorldTravelObserver::ObserveRegionExit(void* component) noexcept
    {
        WorldTravelPreparation preparation;
        __try
        {
            if (component == nullptr || resolveConnectedThing_ == nullptr)
            {
                return;
            }
            const auto* const componentBytes =
                static_cast<const std::uint8_t*>(component);
            void* const sourceThing = *reinterpret_cast<void* const*>(
                componentBytes + 0x04);
            void* const destinationThing = resolveConnectedThing_(
                const_cast<std::uint8_t*>(componentBytes + 0x26));
            if (sourceThing == nullptr || destinationThing == nullptr)
            {
                return;
            }
            const auto* const sourceBytes =
                static_cast<const std::uint8_t*>(sourceThing);
            const auto* const destinationBytes =
                static_cast<const std::uint8_t*>(destinationThing);
            preparation.sourceExitUid =
                *reinterpret_cast<const std::uint64_t*>(sourceBytes + 0x14);
            preparation.destinationEntranceUid =
                *reinterpret_cast<const std::uint64_t*>(
                    destinationBytes + 0x14);
            preparation.sourceMapId =
                *reinterpret_cast<const std::uint16_t*>(sourceBytes + 0x9A);
            preparation.destinationMapId =
                *reinterpret_cast<const std::uint16_t*>(
                    destinationBytes + 0x9A);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            preparation = {};
        }
        if (preparation.sourceMapId == 0 ||
            preparation.destinationMapId == 0)
        {
            return;
        }
        pending_ = preparation;
        ReportRegionExit(preparation);
        const DepartureSink sink = departureSink_.load(
            std::memory_order_acquire);
        void* const context = departureSinkContext_.load(
            std::memory_order_acquire);
        if (sink != nullptr)
        {
            sink(context, preparation);
        }
    }

    bool WorldTravelObserver::ObservePrepareMapChange(
        void* worldInfo,
        const native::NativeNameArray* levelNames) noexcept
    {
        WorldTravelPreparation preparation = pending_;
        std::int32_t count = 0;
        bool valid = false;
        __try
        {
            if (levelNames != nullptr)
            {
                count = levelNames->count;
                valid = levelNames->data != nullptr && count > 0 &&
                    count <= static_cast<std::int32_t>(MaximumLevelNames) &&
                    levelNames->capacity >= count;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid ||
            !native::WorldTravelFunctions::ResolveName(
                gameModule_,
                levelNames->data[0],
                preparation.destinationMapName.data(),
                preparation.destinationMapName.size()))
        {
            return true;
        }

        ReportPrepareMapChange(preparation, count);
        pending_ = {};
        const PreparationSink sink = sink_.load(std::memory_order_acquire);
        void* const context = sinkContext_.load(std::memory_order_acquire);
        if (sink == nullptr || sink(context, preparation))
        {
            return true;
        }
        if (deferred_.active || worldInfo == nullptr)
        {
            diagnostics_.Event(
                "WorldTravelConstructionGateBypassed",
                "another deferred PrepareMapChange was already active");
            return true;
        }
        deferred_ = {};
        deferred_.worldInfo = worldInfo;
        deferred_.preparation = preparation;
        deferred_.levelNameCount = count;
        for (std::int32_t index = 0; index < count; ++index)
        {
            deferred_.levelNames[static_cast<std::size_t>(index)] =
                levelNames->data[index];
        }
        deferred_.active = true;
        diagnostics_.Event(
            "WorldTravelConstructionHeld",
            "retail map construction waits for the host saved-map baseline");
        return false;
    }

    void WorldTravelObserver::ReportRegionExit(
        const WorldTravelPreparation& preparation) noexcept
    {
        const unsigned int ordinal = eventCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "phase=region-exit ordinal=%u source_map_id=%u destination_map_id=%u source_exit_uid=%016llX destination_entrance_uid=%016llX",
            ordinal,
            static_cast<unsigned int>(preparation.sourceMapId),
            static_cast<unsigned int>(preparation.destinationMapId),
            static_cast<unsigned long long>(preparation.sourceExitUid),
            static_cast<unsigned long long>(
                preparation.destinationEntranceUid));
        diagnostics_.Event("WorldTravelPreparing", detail);
    }

    void WorldTravelObserver::ReportPrepareMapChange(
        const WorldTravelPreparation& preparation,
        std::int32_t levelNameCount) noexcept
    {
        const unsigned int ordinal = eventCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "phase=prepare-map ordinal=%u destination_map=%s destination_map_id=%u level_name_count=%d paired_region_exit=%s",
            ordinal,
            preparation.destinationMapName.data(),
            static_cast<unsigned int>(preparation.destinationMapId),
            levelNameCount,
            preparation.destinationMapId != 0 ? "true" : "false");
        diagnostics_.Event("WorldTravelPreparing", detail);
    }
}
