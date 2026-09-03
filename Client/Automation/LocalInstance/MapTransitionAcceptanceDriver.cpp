#include "MapTransitionAcceptanceDriver.h"

#include "Automation/Multiplayer/Transition/NativeRegionRoute.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/World/Travel/Native/WorldTravelFunctions.h"
#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    constexpr std::uint64_t PreparationDelayMilliseconds = 3'000;
    constexpr std::uint64_t MaximumTransitionMilliseconds = 90'000;
    constexpr std::uint64_t TravelRetryMilliseconds = 250;
    constexpr unsigned int TravelInvocationLimit = 8;
}

namespace fable::automation::local_instance
{
    void MapTransitionAcceptanceDriver::Initialize(
        const bool enabled,
        const bool returnToSource,
        game::EntityService& entities,
        ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        multiplayer_ = &multiplayer;
        diagnostics_ = diagnostics;
        enabled_ = enabled;
        returnToSource_ = enabled && returnToSource;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceArmed",
                returnToSource_
                    ? "waiting for a native remote Hero presentation before retail teleport out and back"
                    : "waiting for a native remote Hero presentation before retail teleport to a connected map");
        }
    }

    void MapTransitionAcceptanceDriver::Tick(
        const bool remotePresentationReady) noexcept
    {
        if (!enabled_ || completed_ ||
            (!remotePresentationReady && sourceMapId_ == 0) ||
            entities_ == nullptr || multiplayer_ == nullptr)
        {
            return;
        }

        const auto& localHero = multiplayer_->Contexts().players.localHero;
        const std::uint16_t mapId = localHero.MapId();
        if (!localHero.IsWorldReady() || mapId == 0)
        {
            return;
        }

        game::Entity* const hero = entities_->GetHero();
        if (hero == nullptr || !hero->IsValid())
        {
            if (hero != nullptr)
            {
                hero->Release();
            }
            return;
        }
        const std::string mapName = hero->GetCurrentMapName();
        hero->Release();

        const std::uint64_t now = GetTickCount64();
        if (sourceMapId_ == 0)
        {
            sourceMapId_ = mapId;
            sourceMap_ = mapName;
            phaseStartedAt_ = now;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s source_map_id=%u mode=native-retail-teleport focus_required=false",
                sourceMap_.c_str(),
                static_cast<unsigned int>(sourceMapId_));
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceStarted", detail);
        }
        else if (destinationMapId_ == 0 && mapId != sourceMapId_)
        {
            destinationMapId_ = mapId;
            destinationMap_ = mapName;
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s source_map_id=%u destination_map=%s destination_map_id=%u requests=%u",
                sourceMap_.c_str(),
                static_cast<unsigned int>(sourceMapId_),
                destinationMap_.c_str(),
                static_cast<unsigned int>(destinationMapId_),
                requestCount_);
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceBoundaryCrossed", detail);
            if (!returnToSource_)
            {
                completed_ = true;
                return;
            }
            outboundRequestCount_ = requestCount_;
            requestCount_ = 0;
            travelInvocationCount_ = 0;
            nextTravelAttemptAt_ = 0;
            requestedDestinationMapId_ = 0;
            routeRequested_ = false;
            travelQueued_.store(false, std::memory_order_release);
            phaseStartedAt_ = now;
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceReturnArmed",
                "destination is stable; the host will return through a retail route to the numeric source map");
            return;
        }
        else if (destinationMapId_ != 0 && mapId == sourceMapId_)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s source_map_id=%u destination_map=%s destination_map_id=%u outbound_requests=%u return_requests=%u",
                sourceMap_.c_str(),
                static_cast<unsigned int>(sourceMapId_),
                destinationMap_.c_str(),
                static_cast<unsigned int>(destinationMapId_),
                outboundRequestCount_,
                requestCount_);
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceReturned", detail);
            completed_ = true;
            return;
        }
        else if (destinationMapId_ != 0 && mapId != destinationMapId_)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-unexpected-map");
            completed_ = true;
            return;
        }

        if (phaseStartedAt_ != 0 &&
            now - phaseStartedAt_ > MaximumTransitionMilliseconds)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-boundary-timeout");
            completed_ = true;
            return;
        }
        if (phaseStartedAt_ == 0 ||
            now - phaseStartedAt_ < PreparationDelayMilliseconds ||
            routeRequested_ || travelQueued_.load(std::memory_order_acquire))
        {
            return;
        }

        ::fable::automation::multiplayer::transition::native_route::Descriptor
            route;
        const std::uint16_t preferredDestination =
            destinationMapId_ == 0 ? 0 : sourceMapId_;
        if (!::fable::automation::multiplayer::transition::native_route::
                SelectFirst(
                    *entities_,
                    *multiplayer_,
                    mapId,
                    route,
                    preferredDestination))
        {
            return;
        }

        travelPosition_ = route.destinationPosition;
        travelFacing_ = route.destinationFacing;
        requestedDestinationMapId_ = route.exit.destinationMapId;
        travelInvocationCount_ = 0;
        nextTravelAttemptAt_ = 0;
        travelQueued_.store(true, std::memory_order_release);
        ++requestCount_;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "request=%u source_map_id=%u destination_map_id=%u exit_uid=%016llX mode=native-retail-teleport scheduling=game-thread-idle",
            requestCount_,
            static_cast<unsigned int>(mapId),
            static_cast<unsigned int>(requestedDestinationMapId_),
            static_cast<unsigned long long>(route.exit.exitUid));
        diagnostics_.Event(
            "MultiplayerTransitionAcceptanceRegionExitRequested", detail);
    }

    bool MapTransitionAcceptanceDriver::ProcessGameThreadIdle() noexcept
    {
        if (!enabled_ || completed_ || entities_ == nullptr ||
            multiplayer_ == nullptr ||
            !travelQueued_.load(std::memory_order_acquire))
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        if (now < nextTravelAttemptAt_)
        {
            return false;
        }

        bool heroReady = false;
        std::uint16_t heroMapId = 0;
        std::uint32_t requestState = 0;
        const bool heroStateReadable = game::world::travel::native::
            WorldTravelFunctions::ReadRegionExitHeroState(
                entities_->GameModule(), heroReady, heroMapId);
        const bool requestStateReadable = game::world::travel::native::
            WorldTravelFunctions::ReadRegionTravelRequestState(
                entities_->GameModule(), requestState);
        const std::uint16_t expectedSource =
            destinationMapId_ == 0 ? sourceMapId_ : destinationMapId_;
        if (!heroStateReadable || !heroReady || heroMapId != expectedSource ||
            !requestStateReadable || (requestState != 0 && requestState < 10))
        {
            return false;
        }

        game::Entity* const hero = entities_->GetHero();
        const bool invoked = hero != nullptr && hero->IsValid() &&
            hero->Teleport(travelPosition_, travelFacing_, false);
        if (hero != nullptr)
        {
            hero->Release();
        }
        ++travelInvocationCount_;

        std::uint32_t resultingState = requestState;
        const bool resultReadable = game::world::travel::native::
            WorldTravelFunctions::ReadRegionTravelRequestState(
                entities_->GameModule(), resultingState);
        const bool accepted = invoked && resultReadable &&
            resultingState != requestState;
        if (accepted)
        {
            routeRequested_ = true;
            travelQueued_.store(false, std::memory_order_release);
        }
        else if (travelInvocationCount_ >= TravelInvocationLimit)
        {
            travelQueued_.store(false, std::memory_order_release);
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-native-teleport-rejected");
            completed_ = true;
        }
        else
        {
            nextTravelAttemptAt_ = now + TravelRetryMilliseconds;
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_map_id=%u destination_map_id=%u invocation=%u request_state_before=%u request_state_after=%u invoked=%s accepted=%s",
            static_cast<unsigned int>(expectedSource),
            static_cast<unsigned int>(requestedDestinationMapId_),
            travelInvocationCount_,
            requestState,
            resultingState,
            invoked ? "true" : "false",
            accepted ? "true" : "false");
        diagnostics_.Event(
            "MultiplayerTransitionAcceptanceTeleportInvoked", detail);
        return invoked;
    }

    void MapTransitionAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        multiplayer_ = nullptr;
        diagnostics_ = {};
        sourceMap_.clear();
        destinationMap_.clear();
        travelPosition_ = {};
        travelFacing_ = 0.0f;
        phaseStartedAt_ = 0;
        nextTravelAttemptAt_ = 0;
        requestCount_ = 0;
        outboundRequestCount_ = 0;
        travelInvocationCount_ = 0;
        sourceMapId_ = 0;
        destinationMapId_ = 0;
        requestedDestinationMapId_ = 0;
        travelQueued_.store(false, std::memory_order_release);
        routeRequested_ = false;
        returnToSource_ = false;
        enabled_ = false;
        completed_ = false;
    }
}
