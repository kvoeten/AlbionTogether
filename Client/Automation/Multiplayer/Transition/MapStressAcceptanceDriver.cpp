#include "MapStressAcceptanceDriver.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/World/Travel/Native/WorldTravelFunctions.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr std::uint64_t InitialSettleMilliseconds = 3'000;
    constexpr std::uint64_t InterTransitionSettleMilliseconds = 2'000;
    constexpr std::uint64_t SplitObservationMilliseconds = 1'000;
    // Both peers must leave enough time for the other process to finish its
    // native Hero appearance/equipment graph before either starts the next
    // shared-map transition. Without this distributed grace period, the
    // faster peer can leave while the slower peer is still validating the
    // preceding reconstruction.
    constexpr std::uint64_t SharedObservationMilliseconds = 15'000;
    constexpr std::uint64_t RouteDiscoveryTimeoutMilliseconds = 15'000;
    constexpr std::uint64_t TransitionTimeoutMilliseconds = 90'000;
    constexpr std::uint64_t TravelReadinessRetryMilliseconds = 250;
    constexpr unsigned int TravelInvocationLimit = 8;
    constexpr std::size_t StableMapHistoryLimit = 16;

    std::uint64_t Mix(std::uint64_t value) noexcept
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }
}

namespace fable::automation::multiplayer::transition
{
    void MapStressAcceptanceDriver::Initialize(
        const bool enabled,
        const bool host,
        const std::uint32_t seed,
        const unsigned int transitionCount,
        game::EntityService& entities,
        ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        enabled_ = enabled;
        if (!enabled_)
        {
            return;
        }
        entities_ = &entities;
        multiplayer_ = &multiplayer;
        diagnostics_ = diagnostics;
        host_ = host;
        seed_ = seed != 0 ? seed : 1;
        transitionLimit_ = transitionCount != 0 ? transitionCount : 12;

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s seed=%u transitions=%u schedule=connected-region-exits",
            host_ ? "host" : "guest",
            seed_,
            transitionLimit_);
        diagnostics_.Event("MultiplayerMapStressArmed", detail);
    }

    void MapStressAcceptanceDriver::Tick() noexcept
    {
        if (!enabled_ || completed_ || failed_ ||
            entities_ == nullptr || multiplayer_ == nullptr)
        {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        PeerMap local;
        PeerMap remote;
        if (!ReadStableMaps(local, remote))
        {
            settledAt_ = 0;
            if (transitionRequestedAt_ != 0 &&
                now - transitionRequestedAt_ > TransitionTimeoutMilliseconds)
            {
                Fail("peer-lifecycle-did-not-recover-after-map-request");
            }
            return;
        }

        if (!started_)
        {
            started_ = true;
            phaseReadyAt_ = now + InitialSettleMilliseconds;
            ReportStarted(local);
            return;
        }

        if (transitionRequestedAt_ == 0)
        {
            if (now < phaseReadyAt_)
            {
                return;
            }
            if (!BeginTransition(local, remote, now))
            {
                if (now - phaseReadyAt_ > RouteDiscoveryTimeoutMilliseconds)
                {
                    Fail("no-usable-region-exit-for-scheduled-phase");
                }
            }
            return;
        }

        if (!TransitionSettled(local, remote))
        {
            settledAt_ = 0;
            if (now - transitionRequestedAt_ > TransitionTimeoutMilliseconds)
            {
                Fail("map-transition-or-remote-reconstruction-timeout");
            }
            return;
        }
        if (settledAt_ == 0)
        {
            settledAt_ = now;
            return;
        }
        const std::uint64_t observationWindow =
            local.id == remote.id
                ? SharedObservationMilliseconds
                : SplitObservationMilliseconds;
        if (now - settledAt_ < observationWindow)
        {
            return;
        }
        CompleteTransition(local, remote, now);
    }

    bool MapStressAcceptanceDriver::ProcessGameThreadIdle() noexcept
    {
        if (!enabled_ || failed_ || entities_ == nullptr ||
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
        const bool heroStateReadable =
            game::world::travel::native::WorldTravelFunctions::
                ReadRegionExitHeroState(
                    entities_->GameModule(), heroReady, heroMapId);
        const bool requestStateReadable =
            game::world::travel::native::WorldTravelFunctions::
                ReadRegionTravelRequestState(
                    entities_->GameModule(), requestState);
        if (heroStateReadable && heroMapId == localDestinationMapId_)
        {
            travelQueued_.store(false, std::memory_order_release);
            return false;
        }
        const bool requestStateAcceptsTravel =
            requestState == 0 || requestState >= 10;
        if (!heroStateReadable || !heroReady ||
            heroMapId != sourceMapId_ || !requestStateReadable ||
            !requestStateAcceptsTravel)
        {
            nextTravelAttemptAt_ = now + TravelReadinessRetryMilliseconds;
            return false;
        }

        game::Entity* const hero = entities_->GetHero();
        const bool invoked = hero != nullptr && hero->IsValid() &&
            hero->Teleport(
                destinationPosition_, destinationFacing_, false);
        if (hero != nullptr)
        {
            hero->Release();
        }

        std::uint32_t resultingState = requestState;
        const bool resultingStateReadable =
            game::world::travel::native::WorldTravelFunctions::
                ReadRegionTravelRequestState(
                    entities_->GameModule(), resultingState);
        ++travelInvocations_;
        const bool accepted = invoked && resultingStateReadable &&
            resultingState != requestState;

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_map_id=%u destination_map_id=%u request_state_before=%u request_state_after=%u invocation=%u invoked=%s accepted=%s boundary=game-thread-idle",
            static_cast<unsigned int>(sourceMapId_),
            static_cast<unsigned int>(localDestinationMapId_),
            requestState,
            resultingState,
            travelInvocations_,
            invoked ? "true" : "false",
            accepted ? "true" : "false");
        diagnostics_.Event("MultiplayerMapStressTeleportInvoked", detail);

        if (accepted)
        {
            travelQueued_.store(false, std::memory_order_release);
            return true;
        }
        if (travelInvocations_ >= TravelInvocationLimit)
        {
            travelQueued_.store(false, std::memory_order_release);
            Fail("native-script-teleport-was-not-accepted");
            return invoked;
        }
        nextTravelAttemptAt_ = now + TravelReadinessRetryMilliseconds;
        return invoked;
    }

    bool MapStressAcceptanceDriver::IsStableSameMap() const noexcept
    {
        if (!enabled_ || failed_ || !started_)
        {
            return false;
        }
        PeerMap local;
        PeerMap remote;
        if (!ReadStableMaps(local, remote) || local.id != remote.id)
        {
            return false;
        }
        if (completed_)
        {
            return true;
        }
        // A same-map transition is deliberately observed for fifteen seconds
        // before the next request. Expose that settled window to companion
        // acceptance drivers; the zero-request gap is consumed by Tick before
        // they run and therefore is not an observable checkpoint.
        return transitionRequestedAt_ != 0 && settledAt_ != 0;
    }

    bool MapStressAcceptanceDriver::IsComplete() const noexcept
    {
        return completed_;
    }

    bool MapStressAcceptanceDriver::HasFailed() const noexcept
    {
        return failed_;
    }

    unsigned int MapStressAcceptanceDriver::TransitionOrdinal() const noexcept
    {
        return transitionOrdinal_;
    }

    bool MapStressAcceptanceDriver::ReadStableMaps(
        PeerMap& local,
        PeerMap& remote) const
    {
        local = {};
        remote = {};
        const auto& contexts = multiplayer_->Contexts();
        const auto& hero = contexts.players.localHero;
        const auto* const current = hero.CurrentState();
        if (!hero.IsWorldReady() || current == nullptr ||
            hero.MapName().empty() || hero.MapId() == 0 ||
            current->mapEpoch == 0)
        {
            return false;
        }
        local.name = hero.MapName();
        local.id = hero.MapId();
        local.epoch = current->mapEpoch;
        local.position = current->position;
        local.facing = current->facing;

        const auto snapshots =
            contexts.transport.remotePlayerChannels.Snapshots();
        const auto match = std::find_if(
            snapshots.begin(),
            snapshots.end(),
            [](const auto& snapshot)
            {
                return snapshot.lifecycle.active &&
                    !snapshot.state.mapName.empty() &&
                    snapshot.state.mapId != 0 &&
                    snapshot.state.mapEpoch != 0;
            });
        if (match == snapshots.end())
        {
            return false;
        }
        remote.name = match->state.mapName;
        remote.id = match->state.mapId;
        remote.epoch = match->state.mapEpoch;
        remote.position = match->state.position;
        remote.facing = match->state.facing;
        RememberStableMap(local);
        RememberStableMap(remote);
        return true;
    }

    std::vector<game::world::travel::native::RegionExitDescriptor>
        MapStressAcceptanceDriver::AvailableDestinations(
            const PeerMap& local)
    {
        std::vector<game::world::travel::native::RegionExitDescriptor> exits;
        const auto records = multiplayer_->Contexts().entities.entityPresence.
            LiveEntities().Snapshot();
        std::size_t regionExitCount = 0;
        for (const auto& record : records)
        {
            if (record.mapId != local.id || record.thing == nullptr)
            {
                continue;
            }
            game::world::travel::native::RegionExitDescriptor exit;
            if (game::world::travel::native::RegionExitFunctions::Describe(
                    record.thing,
                    record.thingUid,
                    entities_->GameModule(),
                    exit) &&
                exit.sourceMapId == local.id)
            {
                ++regionExitCount;
                exits.push_back(exit);
            }
        }
        if (exits.empty())
        {
            ReportRouteUnavailable(
                local,
                records.size(),
                regionExitCount);
        }
        std::sort(
            exits.begin(),
            exits.end(),
            [](const auto& left, const auto& right)
            {
                if (left.destinationMapId != right.destinationMapId)
                {
                    return left.destinationMapId < right.destinationMapId;
                }
                if (left.destinationEntranceUid !=
                    right.destinationEntranceUid)
                {
                    return left.destinationEntranceUid <
                        right.destinationEntranceUid;
                }
                return left.exitUid < right.exitUid;
            });
        exits.erase(
            std::unique(
                exits.begin(),
                exits.end(),
                [](const auto& left, const auto& right)
                {
                    return left.destinationMapId == right.destinationMapId;
                }),
            exits.end());
        return exits;
    }

    bool MapStressAcceptanceDriver::BeginTransition(
        const PeerMap& local,
        const PeerMap& remote,
        const std::uint64_t now)
    {
        if (local.id != remote.id)
        {
            sourceMapId_ = local.id;
            sourceMapEpoch_ = local.epoch;
            localDestinationMapId_ = host_ ? local.id : remote.id;
            holdingForReunion_ = host_;
            if (!host_ && !RequestPeerReunion(local, remote))
            {
                sourceMapId_ = 0;
                sourceMapEpoch_ = 0;
                localDestinationMapId_ = 0;
                return false;
            }

            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u role=%s source_map_id=%u destination_map_id=%u mode=%s",
                transitionOrdinal_ + 1,
                host_ ? "host" : "guest",
                static_cast<unsigned int>(local.id),
                static_cast<unsigned int>(localDestinationMapId_),
                host_ ? "hold-reunion-authority" : "join-authority-peer");
            diagnostics_.Event(
                "MultiplayerMapStressTransitionRequested",
                detail);
            transitionRequestedAt_ = now;
            settledAt_ = 0;
            return true;
        }

        const auto exits = AvailableDestinations(local);
        if (exits.empty())
        {
            sourceMapId_ = local.id;
            sourceMapEpoch_ = local.epoch;
            if (!RequestObservedFallback(local))
            {
                sourceMapId_ = 0;
                sourceMapEpoch_ = 0;
                return false;
            }
            transitionRequestedAt_ = now;
            settledAt_ = 0;
            return true;
        }

        const std::uint32_t roleSalt = host_ ? 0x51u : 0xD3u;
        const std::size_t localIndex = SharedChoice(
            exits.size(),
            roleSalt ^ static_cast<std::uint32_t>(local.id));
        const auto& selected = exits[localIndex];
        sourceMapId_ = local.id;
        sourceMapEpoch_ = local.epoch;
        localDestinationMapId_ = selected.destinationMapId;
        if (!RequestTravel(selected))
        {
            sourceMapId_ = 0;
            sourceMapEpoch_ = 0;
            localDestinationMapId_ = 0;
            return false;
        }
        ReportRequest(selected);
        transitionRequestedAt_ = now;
        settledAt_ = 0;
        return true;
    }

    bool MapStressAcceptanceDriver::RequestPeerReunion(
        const PeerMap& local,
        const PeerMap& remote) noexcept
    {
        if (travelQueued_.load(std::memory_order_acquire) || remote.id == 0 ||
            !std::isfinite(remote.position.x) ||
            !std::isfinite(remote.position.y) ||
            !std::isfinite(remote.position.z) ||
            !std::isfinite(remote.facing))
        {
            ReportTravelRequestFailure(
                "peer-reunion-destination-unavailable");
            return false;
        }
        destinationPosition_ = remote.position;
        destinationFacing_ = remote.facing;
        travelInvocations_ = 0;
        nextTravelAttemptAt_ = 0;
        travelQueued_.store(true, std::memory_order_release);
        routeDiagnosticReported_ = false;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_map_id=%u destination_map_id=%u target=(%.3f,%.3f,%.3f) facing=%.3f scheduling=game-thread-idle-peer-reunion",
            static_cast<unsigned int>(local.id),
            static_cast<unsigned int>(remote.id),
            destinationPosition_.x,
            destinationPosition_.y,
            destinationPosition_.z,
            destinationFacing_);
        diagnostics_.Event("MultiplayerMapStressTravelQueued", detail);
        return true;
    }

    bool MapStressAcceptanceDriver::RequestObservedFallback(
        const PeerMap& local) noexcept
    {
        if (travelQueued_.load(std::memory_order_acquire))
        {
            ReportTravelRequestFailure("script-teleport-already-pending");
            return false;
        }

        std::vector<const PeerMap*> candidates;
        candidates.reserve(observedStableMaps_.size());
        for (const auto& observed : observedStableMaps_)
        {
            if (observed.id == 0 || observed.id == local.id ||
                observed.epoch == 0 || !std::isfinite(observed.position.x) ||
                !std::isfinite(observed.position.y) ||
                !std::isfinite(observed.position.z) ||
                !std::isfinite(observed.facing))
            {
                continue;
            }
            candidates.push_back(&observed);
        }
        if (candidates.empty())
        {
            ReportTravelRequestFailure("no-observed-safe-map-fallback");
            return false;
        }

        const auto* const selected = candidates[SharedChoice(
            candidates.size(),
            0xFA11BACCu ^ static_cast<std::uint32_t>(local.id))];
        destinationPosition_ = selected->position;
        destinationFacing_ = selected->facing;
        localDestinationMapId_ = selected->id;
        travelInvocations_ = 0;
        nextTravelAttemptAt_ = 0;
        travelQueued_.store(true, std::memory_order_release);
        routeDiagnosticReported_ = false;

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_map_id=%u destination_map_id=%u target=(%.3f,%.3f,%.3f) facing=%.3f scheduling=game-thread-idle-observed-fallback reason=no-live-region-exit",
            static_cast<unsigned int>(local.id),
            static_cast<unsigned int>(selected->id),
            destinationPosition_.x,
            destinationPosition_.y,
            destinationPosition_.z,
            destinationFacing_);
        diagnostics_.Event("MultiplayerMapStressFallbackQueued", detail);
        return true;
    }

    bool MapStressAcceptanceDriver::RequestTravel(
        const game::world::travel::native::RegionExitDescriptor& exit) noexcept
    {
        if (travelQueued_.load(std::memory_order_acquire))
        {
            ReportTravelRequestFailure("script-teleport-already-pending");
            return false;
        }
        game::Entity* const destination = entities_->FindByUid(
            exit.destinationEntranceUid);
        if (destination == nullptr || !destination->IsValid())
        {
            if (destination != nullptr)
            {
                destination->Release();
            }
            ReportTravelRequestFailure(
                "destination-region-entrance-unavailable");
            return false;
        }

        game::Vector3 destinationPosition;
        float destinationFacing = 0.0f;
        const bool transformReady = entities_->ReadPosition(
                destination->NativeHandle(), destinationPosition) &&
            entities_->ReadFacing(
                destination->NativeHandle(), destinationFacing);
        destination->Release();
        if (!transformReady)
        {
            ReportTravelRequestFailure(
                "destination-region-entrance-transform-unavailable");
            return false;
        }

        destinationPosition_ = destinationPosition;
        destinationFacing_ = destinationFacing;
        travelInvocations_ = 0;
        nextTravelAttemptAt_ = 0;
        travelQueued_.store(true, std::memory_order_release);
        routeDiagnosticReported_ = false;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "exit_uid=%016llX entrance_uid=%016llX destination_map_id=%u target=(%.3f,%.3f,%.3f) facing=%.3f scheduling=game-thread-idle-script-teleport",
            static_cast<unsigned long long>(exit.exitUid),
            static_cast<unsigned long long>(exit.destinationEntranceUid),
            static_cast<unsigned int>(exit.destinationMapId),
            destinationPosition.x,
            destinationPosition.y,
            destinationPosition.z,
            destinationFacing);
        diagnostics_.Event("MultiplayerMapStressTravelQueued", detail);
        return true;
    }

    void MapStressAcceptanceDriver::ReportRouteUnavailable(
        const PeerMap& local,
        const std::size_t liveRecords,
        const std::size_t regionExits) noexcept
    {
        if (routeDiagnosticReported_)
        {
            return;
        }
        routeDiagnosticReported_ = true;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map=%s map_id=%u live_records=%zu region_exits=%zu",
            local.name.c_str(),
            static_cast<unsigned int>(local.id),
            liveRecords,
            regionExits);
        diagnostics_.Event("MultiplayerMapStressRouteUnavailable", detail);
    }

    void MapStressAcceptanceDriver::ReportTravelRequestFailure(
        const char* const reason) noexcept
    {
        if (routeDiagnosticReported_)
        {
            return;
        }
        routeDiagnosticReported_ = true;
        diagnostics_.Event(
            "MultiplayerMapStressTravelRequestFailed",
            reason != nullptr ? reason : "unknown");
    }

    bool MapStressAcceptanceDriver::TransitionSettled(
        const PeerMap& local,
        const PeerMap& remote) const noexcept
    {
        if (holdingForReunion_)
        {
            return local.id == sourceMapId_ && remote.id == local.id &&
                multiplayer_->Contexts().players.remotePlayers.ActiveCount() != 0;
        }
        if (local.id != localDestinationMapId_ ||
            (local.id == sourceMapId_ && local.epoch == sourceMapEpoch_))
        {
            return false;
        }
        const std::size_t activePresentations =
            multiplayer_->Contexts().players.remotePlayers.ActiveCount();
        const bool shouldShareMap = local.id == remote.id;
        return shouldShareMap
            ? activePresentations != 0
            : activePresentations == 0;
    }

    void MapStressAcceptanceDriver::CompleteTransition(
        const PeerMap& local,
        const PeerMap& remote,
        const std::uint64_t now) noexcept
    {
        ++transitionOrdinal_;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u local_map=%s local_map_id=%u remote_map=%s remote_map_id=%u together=%s",
            transitionOrdinal_,
            local.name.c_str(),
            static_cast<unsigned int>(local.id),
            remote.name.c_str(),
            static_cast<unsigned int>(remote.id),
            local.id == remote.id ? "true" : "false");
        diagnostics_.Event("MultiplayerMapStressTransitionCompleted", detail);

        transitionRequestedAt_ = 0;
        sourceMapId_ = 0;
        sourceMapEpoch_ = 0;
        localDestinationMapId_ = 0;
        settledAt_ = 0;
        holdingForReunion_ = false;
        if (transitionOrdinal_ >= transitionLimit_)
        {
            completed_ = true;
            diagnostics_.Event(
                "MultiplayerMapStressCompleted",
                detail);
            return;
        }

        phaseReadyAt_ = now + InterTransitionSettleMilliseconds;
    }

    void MapStressAcceptanceDriver::Fail(const char* reason) noexcept
    {
        failed_ = true;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s seed=%u ordinal=%u reason=%s",
            host_ ? "host" : "guest",
            seed_,
            transitionOrdinal_ + 1,
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event("MultiplayerMapStressFailed", detail);
        diagnostics_.Event("ClientFailed", "multiplayer-map-stress");
    }

    void MapStressAcceptanceDriver::ReportStarted(
        const PeerMap& local) noexcept
    {
        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s seed=%u transitions=%u initial_map=%s initial_map_id=%u",
            host_ ? "host" : "guest",
            seed_,
            transitionLimit_,
            local.name.c_str(),
            static_cast<unsigned int>(local.id));
        diagnostics_.Event("MultiplayerMapStressStarted", detail);
    }

    void MapStressAcceptanceDriver::ReportRequest(
        const game::world::travel::native::RegionExitDescriptor& local) noexcept
    {
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u role=%s source_map_id=%u destination_map_id=%u exit_uid=%016llX entrance_uid=%016llX",
            transitionOrdinal_ + 1,
            host_ ? "host" : "guest",
            static_cast<unsigned int>(local.sourceMapId),
            static_cast<unsigned int>(local.destinationMapId),
            static_cast<unsigned long long>(local.exitUid),
            static_cast<unsigned long long>(local.destinationEntranceUid));
        diagnostics_.Event("MultiplayerMapStressTransitionRequested", detail);
    }

    std::size_t MapStressAcceptanceDriver::SharedChoice(
        const std::size_t count,
        const std::uint32_t salt) const noexcept
    {
        if (count <= 1)
        {
            return 0;
        }
        const std::uint64_t value =
            (static_cast<std::uint64_t>(seed_) << 32) ^
            static_cast<std::uint64_t>(transitionOrdinal_ + 1) ^
            static_cast<std::uint64_t>(salt);
        return static_cast<std::size_t>(Mix(value) % count);
    }

    void MapStressAcceptanceDriver::RememberStableMap(
        const PeerMap& map) const noexcept
    {
        if (map.id == 0 || map.epoch == 0 || map.name.empty() ||
            !std::isfinite(map.position.x) ||
            !std::isfinite(map.position.y) ||
            !std::isfinite(map.position.z) || !std::isfinite(map.facing))
        {
            return;
        }
        const auto existing = std::find_if(
            observedStableMaps_.begin(),
            observedStableMaps_.end(),
            [&map](const PeerMap& observed)
            {
                return observed.id == map.id && observed.epoch == map.epoch;
            });
        if (existing != observedStableMaps_.end())
        {
            *existing = map;
            return;
        }
        if (observedStableMaps_.size() >= StableMapHistoryLimit)
        {
            observedStableMaps_.erase(observedStableMaps_.begin());
        }
        observedStableMaps_.push_back(map);
    }

    void MapStressAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        multiplayer_ = nullptr;
        diagnostics_ = {};
        seed_ = 0;
        transitionLimit_ = 0;
        transitionOrdinal_ = 0;
        sourceMapId_ = 0;
        sourceMapEpoch_ = 0;
        localDestinationMapId_ = 0;
        destinationPosition_ = {};
        destinationFacing_ = 0.0f;
        phaseReadyAt_ = 0;
        transitionRequestedAt_ = 0;
        settledAt_ = 0;
        nextTravelAttemptAt_ = 0;
        travelInvocations_ = 0;
        travelQueued_.store(false, std::memory_order_release);
        host_ = false;
        enabled_ = false;
        started_ = false;
        completed_ = false;
        failed_ = false;
        routeDiagnosticReported_ = false;
        holdingForReunion_ = false;
        observedStableMaps_.clear();
    }
}
