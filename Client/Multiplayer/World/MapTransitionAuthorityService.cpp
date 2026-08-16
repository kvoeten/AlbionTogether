#include "MapTransitionAuthorityService.h"

#include "Game/World/Travel/Hooks/WorldTravelObserver.h"
#include "Multiplayer/Authority/AuthorityReplication.h"

#include <cstdio>

namespace fable::multiplayer::world
{
    void MapTransitionAuthorityService::Initialize(
        authority::AuthorityReplication& authority,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        authority_ = &authority;
        diagnostics_ = diagnostics;
    }

    bool MapTransitionAuthorityService::Attach(
        game::world::travel::WorldTravelObserver& observer) noexcept
    {
        if (authority_ == nullptr || !observer.IsInstalled())
        {
            return false;
        }
        observer_ = &observer;
        observer_->SetPreparationSink(
            &MapTransitionAuthorityService::ObservePreparation,
            this);
        observer_->SetDepartureSink(
            &MapTransitionAuthorityService::ObserveDeparture,
            this);
        diagnostics_.Event(
            "MultiplayerMapTransitionAuthorityReady",
            "retail map construction is held until the host reservation and saved-map baseline are ready");
        return true;
    }

    bool MapTransitionAuthorityService::Process()
    {
        if (authority_ == nullptr)
        {
            return false;
        }
        bool succeeded = true;
        for (;;)
        {
            const std::size_t read = readIndex_.load(
                std::memory_order_relaxed);
            const std::size_t write = writeIndex_.load(
                std::memory_order_acquire);
            if (read == write)
            {
                break;
            }
            auto& preparation = queue_[read % QueueCapacity];
            if (preparation.destinationMapId == 0)
            {
                preparation.destinationMapId = authority_->ResolveMapId(
                    preparation.destinationMapName.data());
                if (preparation.destinationMapId == 0)
                {
                    if (authority_->IsHost() && observer_ != nullptr &&
                        observer_->ResumeDeferredMapChange(
                            preparation.destinationMapName.data(),
                            0))
                    {
                        readIndex_.store(read + 1, std::memory_order_release);
                        continue;
                    }
                    // Initial save loading can reach PrepareMapChange before
                    // the host's first PlayerState supplies the native map ID.
                    return true;
                }
            }
            if (!authority_->RequestMapPreparation(
                    preparation.destinationMapName.data(),
                    preparation.destinationMapId))
            {
                succeeded = false;
                break;
            }
            if (!authority_->IsMapPreparationReady(
                    preparation.destinationMapName.data(),
                    preparation.destinationMapId))
            {
                return true;
            }
            if (observer_ == nullptr ||
                !observer_->ResumeDeferredMapChange(
                    preparation.destinationMapName.data(),
                    preparation.destinationMapId))
            {
                diagnostics_.Event(
                    "MultiplayerMapConstructionReleaseFailed",
                    "the prepared retail map call could not be replayed");
                return false;
            }
            readIndex_.store(read + 1, std::memory_order_release);
        }
        return succeeded;
    }

    bool MapTransitionAuthorityService::ConsumeSourceDeparture(
        std::uint16_t& sourceMapId) noexcept
    {
        sourceMapId = static_cast<std::uint16_t>(
            sourceDepartureMapId_.exchange(0, std::memory_order_acq_rel));
        return sourceMapId != 0;
    }

    void MapTransitionAuthorityService::Shutdown() noexcept
    {
        if (observer_ != nullptr)
        {
            observer_->SetPreparationSink(nullptr, nullptr);
            observer_->SetDepartureSink(nullptr, nullptr);
        }
        authority_ = nullptr;
        observer_ = nullptr;
        diagnostics_ = {};
        writeIndex_.store(0, std::memory_order_release);
        readIndex_.store(0, std::memory_order_release);
        droppedCount_.store(0, std::memory_order_release);
        sourceDepartureMapId_.store(0, std::memory_order_release);
        queue_ = {};
    }

    bool MapTransitionAuthorityService::ObservePreparation(
        void* context,
        const game::world::travel::WorldTravelPreparation& preparation)
        noexcept
    {
        auto* const service = static_cast<MapTransitionAuthorityService*>(
            context);
        if (service != nullptr)
        {
            return !service->Enqueue(preparation);
        }
        return true;
    }

    void MapTransitionAuthorityService::ObserveDeparture(
        void* context,
        const game::world::travel::WorldTravelPreparation& preparation)
        noexcept
    {
        auto* const service = static_cast<MapTransitionAuthorityService*>(
            context);
        if (service != nullptr && preparation.sourceMapId != 0)
        {
            service->sourceDepartureMapId_.store(
                preparation.sourceMapId,
                std::memory_order_release);
        }
    }

    bool MapTransitionAuthorityService::Enqueue(
        const game::world::travel::WorldTravelPreparation& preparation)
        noexcept
    {
        if (preparation.destinationMapName[0] == '\0')
        {
            return false;
        }
        const std::size_t write = writeIndex_.load(
            std::memory_order_relaxed);
        const std::size_t read = readIndex_.load(
            std::memory_order_acquire);
        if (write - read >= QueueCapacity)
        {
            const unsigned int dropped = droppedCount_.fetch_add(
                1,
                std::memory_order_acq_rel) + 1;
            if (dropped <= 4)
            {
                diagnostics_.Event(
                    "MultiplayerMapPreparationDropped",
                    "bounded pre-load transition queue was full");
            }
            return false;
        }
        queue_[write % QueueCapacity] = preparation;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }
}
