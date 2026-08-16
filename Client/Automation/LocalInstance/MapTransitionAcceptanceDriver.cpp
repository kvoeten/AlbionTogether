#include "MapTransitionAcceptanceDriver.h"

#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr float Tau = 6.28318530717958647692f;
    constexpr float BackwardSpeed = 5.0f;
    constexpr std::uint64_t PreparationDelayMilliseconds = 3'000;
    constexpr std::uint64_t MaximumDriveMilliseconds = 15'000;
}

namespace fable::automation::local_instance
{
    void MapTransitionAcceptanceDriver::Initialize(
        bool enabled,
        bool returnToSource,
        game::EntityService& entities,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        locomotion_ = &locomotion;
        diagnostics_ = diagnostics;
        enabled_ = enabled;
        returnToSource_ = enabled && returnToSource;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceArmed",
                returnToSource_
                    ? "waiting for a native remote Hero presentation before driving the local Hero out and back through its physics navigator"
                    : "waiting for a native remote Hero presentation before driving the local Hero backwards through its physics navigator");
        }
    }

    void MapTransitionAcceptanceDriver::Tick(
        float deltaSeconds,
        bool remotePresentationReady)
    {
        if (!enabled_ || completed_ ||
            (!remotePresentationReady && sourceMap_.empty()) ||
            entities_ == nullptr || locomotion_ == nullptr ||
            !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
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

        const std::string map = hero->GetCurrentMapName();
        if (map.empty())
        {
            hero->Release();
            return;
        }
        if (sourceMap_.empty())
        {
            sourceMap_ = map;
            phaseStartedAt_ = GetTickCount64();
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s mode=native_physics_component focus_required=false",
                sourceMap_.c_str());
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceStarted",
                detail);
        }
        else if (destinationMap_.empty() && map != sourceMap_)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s destination_map=%s requests=%u",
                sourceMap_.c_str(),
                map.c_str(),
                requestCount_);
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceBoundaryCrossed",
                detail);
            if (!returnToSource_)
            {
                completed_ = true;
                hero->Release();
                return;
            }
            destinationMap_ = map;
            outboundRequestCount_ = requestCount_;
            requestCount_ = 0;
            phaseStartedAt_ = GetTickCount64();
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceReturnArmed",
                "destination is stable; the host will return so sticky source-map ownership can be verified");
            hero->Release();
            return;
        }
        else if (!destinationMap_.empty() && map == sourceMap_)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_map=%s destination_map=%s outbound_requests=%u return_requests=%u",
                sourceMap_.c_str(),
                destinationMap_.c_str(),
                outboundRequestCount_,
                requestCount_);
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceReturned",
                detail);
            completed_ = true;
            hero->Release();
            return;
        }
        else if (!destinationMap_.empty() && map != destinationMap_)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-unexpected-map");
            completed_ = true;
            hero->Release();
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (phaseStartedAt_ != 0 &&
            now - phaseStartedAt_ < PreparationDelayMilliseconds)
        {
            hero->Release();
            return;
        }
        if (phaseStartedAt_ != 0 &&
            now - phaseStartedAt_ > MaximumDriveMilliseconds)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-boundary-timeout");
            completed_ = true;
            hero->Release();
            return;
        }

        const game::Vector3 position = hero->GetPosition();
        const float facing = hero->GetFacing();
        const float radians = facing * Tau;
        const float distance = BackwardSpeed *
            std::clamp(deltaSeconds, 0.001f, 0.05f);
        const game::Vector3 desired = {
            position.x - std::sin(radians) * distance,
            position.y - std::cos(radians) * distance,
            position.z,
        };
        const bool requested =
            locomotion_->RequestPosition(hero, desired);
        hero->Release();
        if (!requested)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-transition-acceptance-movement-request");
            completed_ = true;
            return;
        }

        ++requestCount_;
        if (requestCount_ == 1 || requestCount_ == 60)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "request=%u desired=(%.3f,%.3f,%.3f) facing=%.6f speed=%.2f",
                requestCount_,
                desired.x,
                desired.y,
                desired.z,
                facing,
                BackwardSpeed);
            diagnostics_.Event(
                "MultiplayerTransitionAcceptanceMovementRequested",
                detail);
        }
    }

    void MapTransitionAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        locomotion_ = nullptr;
        diagnostics_ = {};
        sourceMap_.clear();
        destinationMap_.clear();
        phaseStartedAt_ = 0;
        requestCount_ = 0;
        outboundRequestCount_ = 0;
        returnToSource_ = false;
        enabled_ = false;
        completed_ = false;
    }
}
