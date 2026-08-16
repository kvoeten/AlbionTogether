#include "EntityMovementReplication.h"

#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Creature/Look/Native/CreatureLookFunctions.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    float FacingDelta(float next, float previous) noexcept
    {
        float delta = next - previous;
        if (delta > 0.5f)
        {
            delta -= 1.0f;
        }
        else if (delta < -0.5f)
        {
            delta += 1.0f;
        }
        return delta;
    }

    float NormalizeFacing(float facing) noexcept
    {
        facing -= std::floor(facing);
        return facing < 0.0f ? facing + 1.0f : facing;
    }

    bool ReadNativeCapture(
        void* creature,
        std::uint64_t& entityUid,
        void*& navigator,
        fable::game::Vector3& position) noexcept
    {
        entityUid = 0;
        navigator = nullptr;
        position = {};
        bool valid = false;
        __try
        {
            entityUid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(creature) + 0x14);
            navigator = fable::game::entity::native::ThingComponentAccess::Find(
                creature,
                fable::game::entity::native::ThingComponentType::
                    PhysicsNavigator);
            if (entityUid != 0 && navigator != nullptr)
            {
                std::memcpy(
                    &position,
                    static_cast<const std::uint8_t*>(navigator) +
                        fable::game::creature::locomotion::native::
                            PhysicsNavigatorFunctions::WorldPositionOffset,
                    sizeof(position));
                valid = std::isfinite(position.x) &&
                    std::isfinite(position.y) &&
                    std::isfinite(position.z);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }
}

namespace fable::multiplayer::movement
{
    void EntityMovementReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        locomotion_ = &locomotion;
        look_ = &look;
        gameModule_ = look.GameModule();
        diagnostics_ = diagnostics;
        initialized_.store(true, std::memory_order_release);
        look_->SetCreatureFrameObserver(
            &EntityMovementReplication::ObserveCreatureFrame,
            this);
        diagnostics_.Event(
            "MultiplayerEntityMovementReady",
            "map-owner creature frames use an unreliable generation-fenced channel");
    }

    bool EntityMovementReplication::Process(
        const entities::LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        bool ownerRosterReady)
    {
        if (!initialized_.load(std::memory_order_acquire) ||
            transport_ == nullptr || authority_ == nullptr ||
            lifecycle_ == nullptr || identities_ == nullptr)
        {
            return false;
        }

        const authority::MapAuthorityLease* const lease =
            authority_->FindMapLease(localMap);
        const bool ownsMap = lease != nullptr && !localMap.empty() &&
            lease->actorId == localActorId_ && lease->epoch != 0;
        const std::uint32_t mapEpoch = lease != nullptr ? lease->epoch : 0;
        const bool ownsEntityAction = lease != nullptr &&
            authority_->HasEntityActionPublisher(
                localMap,
                localActorId_,
                mapEpoch);
        const bool canPublish = ownerRosterReady &&
            (ownsMap || ownsEntityAction);
        if (!canPublish || ownedMap_ != localMap ||
            ownedMapEpoch_ != mapEpoch)
        {
            localStates_.clear();
            std::lock_guard<std::mutex> lock(captureMutex_);
            pendingCaptures_.clear();
        }
        ownedMap_ = canPublish ? localMap : std::string{};
        ownedMapEpoch_ = mapEpoch;
        captureEnabled_.store(canPublish, std::memory_order_release);

        if (!ProcessInbound())
        {
            return false;
        }
        RetryDeferredInbound();
        PruneCurrentSamples();
        if (canPublish &&
            !ProcessLocalCaptures(liveEntities, localMap, mapEpoch))
        {
            return false;
        }

        if (role_ == PeerRole::Host)
        {
            const std::uint64_t peerRevision = transport_->PeerSetRevision();
            if (peerRevision != knownPeerRevision_)
            {
                for (auto& entry : currentSamples_)
                {
                    CurrentSample& current = entry.second;
                    std::array<std::uint8_t, protocol::MaximumDatagramBytes>
                        payload = {};
                    std::size_t payloadSize = 0;
                    if (!protocol::EncodeEntityMovementMessage(
                            current.message,
                            payload.data(),
                            protocol::MaximumPayloadBytes(),
                            payloadSize) ||
                        !transport_->RelayUnreliable(
                            current.message.ownerActorId,
                            protocol::PacketType::EntityMovement,
                            payload.data(),
                            payloadSize))
                    {
                        return false;
                    }
                }
            }
            knownPeerRevision_ = peerRevision;
        }

        ReconcilePlaybacks(liveEntities, localMap);
        return true;
    }

    void EntityMovementReplication::ObserveCreatureFrame(
        void* context,
        void* creature)
    {
        if (context != nullptr)
        {
            static_cast<EntityMovementReplication*>(context)->Capture(
                creature);
        }
    }

    bool EntityMovementReplication::ReadMovement(
        void* context,
        void* creature,
        ReplicatedActorMovement::NativeInput& input)
    {
        auto* const playback = static_cast<Playback*>(context);
        return playback != nullptr &&
            playback->movement.Provide(creature, input);
    }

    void EntityMovementReplication::Capture(void* creature) noexcept
    {
        if (!captureEnabled_.load(std::memory_order_acquire) ||
            creature == nullptr || gameModule_ == nullptr)
        {
            return;
        }

        NativeCapture capture;
        capture.nativeThing = creature;
        void* navigator = nullptr;
        if (!ReadNativeCapture(
                creature,
                capture.entityUid,
                navigator,
                capture.position) ||
            !game::creature::look::native::CreatureLookFunctions::
                ReadNavigatorFacing(gameModule_, navigator, capture.facing))
        {
            return;
        }
        capture.capturedAt = GetTickCount64();

        std::lock_guard<std::mutex> lock(captureMutex_);
        if (!captureEnabled_.load(std::memory_order_relaxed))
        {
            return;
        }
        const auto existing = pendingCaptures_.find(capture.entityUid);
        if (existing != pendingCaptures_.end())
        {
            existing->second = capture;
        }
        else if (pendingCaptures_.size() < CaptureCapacity)
        {
            pendingCaptures_.emplace(capture.entityUid, capture);
        }
    }

    bool EntityMovementReplication::ProcessInbound()
    {
        TransportMessage transportMessage;
        while (transport_->TryConsumeUnreliable(transportMessage))
        {
            if (transportMessage.type !=
                protocol::PacketType::EntityMovement)
            {
                continue;
            }
            protocol::EntityMovementMessage message;
            if (!protocol::DecodeEntityMovementMessage(
                    transportMessage.payload.data(),
                    transportMessage.payloadSize,
                    message))
            {
                diagnostics_.Event(
                    "MultiplayerEntityMovementRejected",
                    "invalid movement payload");
                continue;
            }
            const std::uint64_t receivedAt = GetTickCount64();
            if (!Accept(
                    message,
                    transportMessage.sourceActorId,
                    receivedAt,
                    role_ == PeerRole::Host))
            {
                if (role_ == PeerRole::Guest)
                {
                    const auto deferred = deferredInbound_.find(
                        message.entityUid);
                    if (deferred != deferredInbound_.end())
                    {
                        if (IsNewerSequence(
                                message.sequence,
                                deferred->second.message.sequence))
                        {
                            deferred->second = {message, receivedAt};
                        }
                    }
                    else if (deferredInbound_.size() < CaptureCapacity)
                    {
                        deferredInbound_.emplace(
                            message.entityUid,
                            CurrentSample{message, receivedAt});
                    }
                }
            }
        }
        return true;
    }

    void EntityMovementReplication::RetryDeferredInbound()
    {
        const std::uint64_t now = GetTickCount64();
        for (auto iterator = deferredInbound_.begin();
             iterator != deferredInbound_.end();)
        {
            CurrentSample& deferred = iterator->second;
            if (now - deferred.receivedAt >
                DeferredSampleLifetimeMilliseconds)
            {
                iterator = deferredInbound_.erase(iterator);
                continue;
            }
            if (Accept(
                    deferred.message,
                    deferred.message.ownerActorId,
                    deferred.receivedAt,
                    false))
            {
                iterator = deferredInbound_.erase(iterator);
                continue;
            }
            ++iterator;
        }
    }

    void EntityMovementReplication::PruneCurrentSamples()
    {
        for (auto iterator = currentSamples_.begin();
             iterator != currentSamples_.end();)
        {
            const entities::WorldEntityRecord* const record =
                lifecycle_->Directory().Find(iterator->first);
            const protocol::EntityMovementMessage& message =
                iterator->second.message;
            const authority::EntityAuthorityKey key{
                message.entityUid,
                message.entityGeneration};
            if (record == nullptr || !record->live || !record->available ||
                record->generation != message.entityGeneration ||
                record->mapName != message.mapName ||
                record->mapEpoch != message.mapEpoch ||
                !authority_->IsEntityPublisher(
                    key,
                    message.mapName,
                    message.ownerActorId,
                    message.mapEpoch))
            {
                RetirePlayback(iterator->first);
                iterator = currentSamples_.erase(iterator);
                continue;
            }
            ++iterator;
        }
    }

    bool EntityMovementReplication::ProcessLocalCaptures(
        const entities::LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        std::unordered_map<std::uint64_t, NativeCapture> captures;
        {
            std::lock_guard<std::mutex> lock(captureMutex_);
            captures.swap(pendingCaptures_);
        }
        const std::uint64_t now = GetTickCount64();
        for (const auto& entry : captures)
        {
            const NativeCapture& capture = entry.second;
            const std::uint64_t canonicalUid =
                identities_->CanonicalizeLocalObservation(
                capture.entityUid);
            const entities::LiveEntityRecord* const live =
                liveEntities.Find(canonicalUid);
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(canonicalUid);
            if (live == nullptr || live->thing != capture.nativeThing ||
                !live->creature || !entities::LiveEntityRegistry::IsReplicable(
                    *live) || world == nullptr || !world->live ||
                !world->available || !world->creature ||
                world->mapName != localMap || world->mapEpoch != mapEpoch ||
                !authority_->IsEntityPublisher(
                    {world->thingUid, world->generation},
                    localMap,
                    localActorId_,
                    mapEpoch))
            {
                localStates_.erase(canonicalUid);
                continue;
            }

            LocalPublishState& state = localStates_[canonicalUid];
            if (!state.hasPrevious ||
                state.current.entityGeneration != world->generation ||
                state.current.mapEpoch != mapEpoch)
            {
                state = {};
                state.previous = capture;
                state.hasPrevious = true;
            }
            const std::uint64_t elapsedMilliseconds =
                capture.capturedAt > state.previous.capturedAt
                    ? capture.capturedAt - state.previous.capturedAt
                    : 0;
            const float seconds = elapsedMilliseconds != 0
                ? static_cast<float>((std::min<std::uint64_t>)(
                    elapsedMilliseconds, 250)) / 1000.0f
                : 0.0f;

            protocol::EntityMovementMessage current;
            current.entityUid = canonicalUid;
            current.entityGeneration = world->generation;
            current.ownerActorId = localActorId_;
            current.mapEpoch = mapEpoch;
            current.mapName = localMap;
            current.position = capture.position;
            current.facing = NormalizeFacing(capture.facing);
            if (seconds > 0.0f)
            {
                current.velocity = {
                    (capture.position.x - state.previous.position.x) / seconds,
                    (capture.position.y - state.previous.position.y) / seconds,
                    (capture.position.z - state.previous.position.z) / seconds};
                current.angularVelocity = FacingDelta(
                    current.facing,
                    state.previous.facing) / seconds;
            }
            const float speedSquared =
                current.velocity.x * current.velocity.x +
                current.velocity.y * current.velocity.y +
                current.velocity.z * current.velocity.z;
            current.moving = speedSquared >= 0.0025f;
            state.previous = capture;
            state.current = current;
            state.hasCurrent = true;
        }

        std::vector<std::uint64_t> stale;
        for (auto& entry : localStates_)
        {
            const std::uint64_t entityUid = entry.first;
            LocalPublishState& state = entry.second;
            const entities::LiveEntityRecord* const live =
                liveEntities.Find(entityUid);
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(entityUid);
            if (!state.hasCurrent || live == nullptr || world == nullptr ||
                !world->live || world->generation !=
                    state.current.entityGeneration ||
                world->mapEpoch != mapEpoch || world->mapName != localMap ||
                !authority_->IsEntityPublisher(
                    {world->thingUid, world->generation},
                    localMap,
                    localActorId_,
                    mapEpoch))
            {
                stale.push_back(entityUid);
                continue;
            }

            const bool stopped = state.hasPublished &&
                state.published.moving && !state.current.moving;
            const bool rotated = state.hasPublished &&
                std::fabs(FacingDelta(
                    state.current.facing,
                    state.published.facing)) >= 0.0005f;
            const bool due = !state.hasPublished || stopped ||
                (now - state.lastPublishedAt >=
                    PublishIntervalMilliseconds &&
                    (state.current.moving || rotated));
            if (!due)
            {
                continue;
            }
            state.current.sequence = NextSequence(state.nextSequence);
            if (role_ == PeerRole::Host &&
                !lifecycle_->HostAcceptMovement(state.current))
            {
                diagnostics_.Event(
                    "MultiplayerEntityMovementRejected",
                    "host could not checkpoint its authoritative entity transform");
                return false;
            }
            if (!Publish(state.current))
            {
                return false;
            }
            if (state.current.moving &&
                publishedMovingEntities_.insert(entityUid).second)
            {
                char detail[384] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "thing_uid=%016llX generation=%u owner_actor_id=%llu map=%s epoch=%u sequence=%u velocity=(%.6f,%.6f,%.6f) angular_velocity=%.6f",
                    static_cast<unsigned long long>(entityUid),
                    state.current.entityGeneration,
                    static_cast<unsigned long long>(localActorId_),
                    localMap.c_str(),
                    mapEpoch,
                    state.current.sequence,
                    state.current.velocity.x,
                    state.current.velocity.y,
                    state.current.velocity.z,
                    state.current.angularVelocity);
                diagnostics_.Event(
                    "MultiplayerEntityMovementPublishedMoving",
                    detail);
            }
            state.nextSequence = state.current.sequence;
            state.published = state.current;
            state.hasPublished = true;
            state.lastPublishedAt = now;
            currentSamples_[entityUid] = {state.current, now};
        }
        for (const std::uint64_t entityUid : stale)
        {
            localStates_.erase(entityUid);
        }
        return true;
    }

    bool EntityMovementReplication::Accept(
        const protocol::EntityMovementMessage& message,
        std::uint64_t sourceActorId,
        std::uint64_t receivedAt,
        bool relay)
    {
        const authority::EntityAuthorityKey key{
            message.entityUid,
            message.entityGeneration};
        if (message.ownerActorId != sourceActorId ||
            !authority_->IsEntityPublisher(
                key,
                message.mapName,
                sourceActorId,
                message.mapEpoch))
        {
            return false;
        }
        const entities::WorldEntityRecord* const record =
            lifecycle_->Directory().Find(message.entityUid);
        if (record == nullptr || !record->live || !record->available ||
            !record->creature || record->generation !=
                message.entityGeneration || record->mapName !=
                message.mapName || record->mapEpoch != message.mapEpoch)
        {
            return false;
        }
        const auto existing = currentSamples_.find(message.entityUid);
        if (existing != currentSamples_.end())
        {
            const protocol::EntityMovementMessage& previous =
                existing->second.message;
            const bool sameChannel = previous.entityGeneration ==
                    message.entityGeneration &&
                previous.ownerActorId == message.ownerActorId &&
                previous.mapEpoch == message.mapEpoch;
            if (sameChannel &&
                !IsNewerSequence(message.sequence, previous.sequence))
            {
                return false;
            }
        }
        if (relay && !lifecycle_->HostAcceptMovement(message))
        {
            return false;
        }
        currentSamples_[message.entityUid] = {message, receivedAt};
        if (message.moving &&
            acceptedMovingEntities_.insert(message.entityUid).second)
        {
            char detail[384] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u owner_actor_id=%llu map=%s epoch=%u sequence=%u relay=%s velocity=(%.6f,%.6f,%.6f) angular_velocity=%.6f",
                static_cast<unsigned long long>(message.entityUid),
                message.entityGeneration,
                static_cast<unsigned long long>(message.ownerActorId),
                message.mapName.c_str(),
                message.mapEpoch,
                message.sequence,
                relay ? "true" : "false",
                message.velocity.x,
                message.velocity.y,
                message.velocity.z,
                message.angularVelocity);
            diagnostics_.Event(
                "MultiplayerEntityMovementAcceptedMoving",
                detail);
        }
        if (!relay)
        {
            return true;
        }

        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        return protocol::EncodeEntityMovementMessage(
                message,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize) &&
            transport_->RelayUnreliable(
                sourceActorId,
                protocol::PacketType::EntityMovement,
                payload.data(),
                payloadSize);
    }

    bool EntityMovementReplication::Publish(
        protocol::EntityMovementMessage& message)
    {
        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        return protocol::EncodeEntityMovementMessage(
                message,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize) &&
            transport_->SubmitUnreliable(
                protocol::PacketType::EntityMovement,
                payload.data(),
                payloadSize);
    }

    void EntityMovementReplication::ReconcilePlaybacks(
        const entities::LiveEntityRegistry& liveEntities,
        const std::string& localMap)
    {
        std::vector<std::uint64_t> stale;
        for (const auto& entry : playbacks_)
        {
            const std::uint64_t entityUid = entry.first;
            const Playback& playback = *entry.second;
            const auto current = currentSamples_.find(entityUid);
            const entities::LiveEntityRecord* const live =
                liveEntities.Find(entityUid);
            if (current == currentSamples_.end() || live == nullptr ||
                live->thing != playback.nativeThing ||
                current->second.message.mapName != localMap ||
                current->second.message.ownerActorId == localActorId_)
            {
                stale.push_back(entityUid);
            }
        }
        for (const std::uint64_t entityUid : stale)
        {
            RetirePlayback(entityUid);
        }

        for (const auto& entry : currentSamples_)
        {
            const std::uint64_t entityUid = entry.first;
            const CurrentSample& current = entry.second;
            const protocol::EntityMovementMessage& message = current.message;
            if (message.mapName != localMap ||
                message.ownerActorId == localActorId_)
            {
                continue;
            }
            const entities::LiveEntityRecord* const live =
                liveEntities.Find(entityUid);
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(entityUid);
            if (live == nullptr || !live->creature ||
                !entities::LiveEntityRegistry::IsReplicable(*live) ||
                world == nullptr || !world->live || !world->available ||
                world->generation != message.entityGeneration ||
                world->mapEpoch != message.mapEpoch ||
                !authority_->IsEntityPublisher(
                    {world->thingUid, world->generation},
                    message.mapName,
                    message.ownerActorId,
                    message.mapEpoch))
            {
                continue;
            }

            auto playback = playbacks_.find(entityUid);
            const bool rebind = playback == playbacks_.end() ||
                playback->second->nativeThing != live->thing ||
                playback->second->generation != message.entityGeneration ||
                playback->second->ownerActorId != message.ownerActorId ||
                playback->second->mapEpoch != message.mapEpoch;
            if (rebind)
            {
                RetirePlayback(entityUid);
                auto created = std::make_unique<Playback>();
                created->nativeThing = live->thing;
                created->generation = message.entityGeneration;
                created->ownerActorId = message.ownerActorId;
                created->mapEpoch = message.mapEpoch;
                created->sequence = message.sequence;
                created->movement.Initialize(*locomotion_, diagnostics_);
                created->movement.BindNative(
                    live->thing,
                    ToSample(current),
                    localMap);
                Playback* const context = created.get();
                if (!look_->RouteReplicatedNativeMovement(
                        live->thing,
                        &EntityMovementReplication::ReadMovement,
                        context))
                {
                    created->movement.Detach();
                    continue;
                }
                playbacks_.emplace(entityUid, std::move(created));
                diagnostics_.Event(
                    "MultiplayerRemoteEntityMovementBound",
                    "retail creature now consumes the current map owner's smoothed movement");
                continue;
            }
            Playback& active = *playback->second;
            if (IsNewerSequence(message.sequence, active.sequence))
            {
                active.movement.Update(ToSample(current), localMap);
                active.sequence = message.sequence;
            }
        }
    }

    void EntityMovementReplication::RetirePlayback(
        std::uint64_t entityUid) noexcept
    {
        const auto playback = playbacks_.find(entityUid);
        if (playback == playbacks_.end())
        {
            return;
        }
        if (look_ != nullptr)
        {
            look_->StopRoutingNative(playback->second->nativeThing);
        }
        playback->second->movement.Detach();
        playbacks_.erase(playback);
    }

    ReplicatedMovementSample EntityMovementReplication::ToSample(
        const CurrentSample& sample)
    {
        ReplicatedMovementSample result;
        result.actorId = sample.message.ownerActorId;
        result.authorityEpoch = sample.message.mapEpoch;
        result.sequence = sample.message.sequence;
        result.mapName = sample.message.mapName;
        result.position = sample.message.position;
        result.velocity = sample.message.velocity;
        result.facing = sample.message.facing;
        result.angularVelocity = sample.message.angularVelocity;
        result.moving = sample.message.moving;
        result.receivedAt = sample.receivedAt;
        return result;
    }

    bool EntityMovementReplication::IsNewerSequence(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return current == 0 || (candidate != current &&
            static_cast<std::int32_t>(candidate - current) > 0);
    }

    std::uint32_t EntityMovementReplication::NextSequence(
        std::uint32_t current) noexcept
    {
        return current == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : current + 1u;
    }

    void EntityMovementReplication::Drive()
    {
        if (!initialized_.load(std::memory_order_acquire) || look_ == nullptr)
        {
            return;
        }
        for (const auto& entry : playbacks_)
        {
            look_->DriveReplicatedNativeMovement(
                entry.second->nativeThing);
        }
    }

    void EntityMovementReplication::Shutdown() noexcept
    {
        captureEnabled_.store(false, std::memory_order_release);
        initialized_.store(false, std::memory_order_release);
        if (look_ != nullptr)
        {
            look_->SetCreatureFrameObserver(nullptr, nullptr);
        }
        while (!playbacks_.empty())
        {
            RetirePlayback(playbacks_.begin()->first);
        }
        {
            std::lock_guard<std::mutex> lock(captureMutex_);
            pendingCaptures_.clear();
        }
        localStates_.clear();
        currentSamples_.clear();
        deferredInbound_.clear();
        publishedMovingEntities_.clear();
        acceptedMovingEntities_.clear();
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        locomotion_ = nullptr;
        look_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        knownPeerRevision_ = 0;
        ownedMap_.clear();
        ownedMapEpoch_ = 0;
    }
}
