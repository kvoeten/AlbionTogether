#include "EntityVitalsReplication.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Protocol/EntityVitalsMessageCodec.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fable::multiplayer::replication
{
    void EntityVitalsReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        RemotePlayerChannels& remotePlayers,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        remotePlayerChannels_ = &remotePlayers;
        combat_ = &combat;
        diagnostics_ = diagnostics;
        initialized_ = true;
        acceptingEvents_.store(true, std::memory_order_release);
        combat_->SetHealthMutationSink(
            &EntityVitalsReplication::CaptureMutation, this);
        diagnostics_.Event(
            "MultiplayerEntityVitalsReady",
            "reliable player/NPC health replication is bound to the native mutation boundary");
    }

    bool EntityVitalsReplication::Process(
        const LocalHeroReplication& localHero,
        const entities::LiveEntityRegistry& liveEntities,
        presentation::RemotePlayerRegistry& remotePlayers)
    {
        if (!initialized_)
        {
            return false;
        }
        processingLocalHero_ = &localHero;
        processingLiveEntities_ = &liveEntities;
        std::deque<game::creature::combat::CombatHealthMutationEvent> captured;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            captured.swap(events_);
        }
        while (!deferred_.empty())
        {
            captured.push_front(deferred_.back());
            deferred_.pop_back();
        }

        const std::uint64_t now = GetTickCount64();
        for (const auto& event : captured)
        {
            if (event.creature == localHero.NativeHero())
            {
                if (!AuthorPlayer(event))
                {
                    return false;
                }
                continue;
            }
            bool deferred = false;
            if (!AuthorEntity(event, liveEntities, now, deferred))
            {
                return false;
            }
            if (deferred && deferred_.size() < EventCapacity)
            {
                deferred_.push_back(event);
            }
        }
        if (!PublishBaselines(localHero, liveEntities))
        {
            return false;
        }
        if (!PublishPeerBaseline())
        {
            return false;
        }
        const unsigned int dropped =
            droppedEvents_.load(std::memory_order_acquire);
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            diagnostics_.Event(
                "MultiplayerEntityVitalsOverflow",
                "native health mutations exceeded the bounded queue");
            return false;
        }
        if (!PublishPending())
        {
            return false;
        }
        ApplyLatest(liveEntities, remotePlayers);
        return true;
    }

    bool EntityVitalsReplication::AuthorPlayer(
        const game::creature::combat::CombatHealthMutationEvent& event)
    {
        const PlayerState* const state = processingLocalHero_ != nullptr
            ? processingLocalHero_->CurrentState()
            : nullptr;
        if (state == nullptr || state->actorId != localActorId_ ||
            state->actorGeneration == 0 || state->mapEpoch == 0)
        {
            return true;
        }
        protocol::EntityVitalsMessage message;
        message.subject = protocol::EntityVitalsSubject::Player;
        message.playerActorId = localActorId_;
        message.ownerActorId = localActorId_;
        message.playerAuthorityEpoch = state->authorityEpoch;
        message.playerActorGeneration = state->actorGeneration;
        message.playerMapEpoch = state->mapEpoch;
        message.revision = NextLocalRevision();
        message.currentHealth = event.currentHealth;
        message.maximumHealth = event.maximumHealth;
        if (role_ == PeerRole::Host)
        {
            message.revision = NextHostRevision(message);
            latestPlayers_[localActorId_] = message;
            latestPlayerConnectionNonces_[localActorId_] =
                transport_ != nullptr ? transport_->ConnectionNonce() : 0;
        }
        const float publishedCurrent = message.currentHealth;
        const float publishedMaximum = message.maximumHealth;
        const std::uint32_t publishedRevision = message.revision;
        if (!Publish(std::move(message)))
        {
            return false;
        }
        authoredPlayerCreature_ = event.creature;
        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "subject=player actor=%llu revision=%u health=%.3f maximum=%.3f",
            static_cast<unsigned long long>(localActorId_),
            publishedRevision,
            publishedCurrent,
            publishedMaximum);
        diagnostics_.Event("MultiplayerEntityVitalsPublished", detail);
        return true;
    }

    bool EntityVitalsReplication::AuthorEntity(
        const game::creature::combat::CombatHealthMutationEvent& event,
        const entities::LiveEntityRegistry& liveEntities,
        std::uint64_t now,
        bool& deferred)
    {
        deferred = false;
        if (event.thingUid == 0 || identities_ == nullptr ||
            lifecycle_ == nullptr || authority_ == nullptr)
        {
            return true;
        }
        const std::uint64_t canonicalUid =
            identities_->CanonicalizeLocalObservation(event.thingUid);
        const entities::LiveEntityRecord* const live =
            liveEntities.Find(canonicalUid);
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(canonicalUid);
        if (live == nullptr || live->thing != event.creature ||
            !live->creature ||
            !entities::LiveEntityRegistry::IsReplicable(*live) ||
            world == nullptr || !world->live || !world->available ||
            !world->creature)
        {
            return true;
        }
        const authority::EntityAuthorityKey key{
            world->thingUid, world->generation};
        if (!authority_->IsEntityPublisher(
                key,
                world->mapName,
                localActorId_,
                world->mapEpoch))
        {
            deferred = now >= event.observedAt &&
                now - event.observedAt <= AuthorityGraceMilliseconds;
            return true;
        }

        protocol::EntityVitalsMessage message;
        message.subject = protocol::EntityVitalsSubject::WorldEntity;
        message.entityUid = world->thingUid;
        message.entityGeneration = world->generation;
        message.ownerActorId = localActorId_;
        message.mapEpoch = world->mapEpoch;
        message.revision = NextLocalRevision();
        message.currentHealth = event.currentHealth;
        message.maximumHealth = event.maximumHealth;
        message.mapName = world->mapName;
        if (role_ == PeerRole::Host)
        {
            message.revision = NextHostRevision(message);
            latestEntities_[message.entityUid] = message;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "subject=entity thing_uid=%016llX generation=%u owner=%llu map=%s epoch=%u revision=%u health=%.3f maximum=%.3f",
            static_cast<unsigned long long>(message.entityUid),
            message.entityGeneration,
            static_cast<unsigned long long>(message.ownerActorId),
            message.mapName.c_str(),
            message.mapEpoch,
            message.revision,
            message.currentHealth,
            message.maximumHealth);
        if (!Publish(std::move(message)))
        {
            return false;
        }
        authoredEntityBaselines_[world->thingUid] = {
            live->thing,
            world->generation,
            world->mapEpoch,
            localActorId_,
        };
        diagnostics_.Event("MultiplayerEntityVitalsPublished", detail);
        return true;
    }

    bool EntityVitalsReplication::PublishBaselines(
        const LocalHeroReplication& localHero,
        const entities::LiveEntityRegistry& liveEntities)
    {
        void* const hero = localHero.NativeHero();
        if (hero != nullptr && hero != authoredPlayerCreature_)
        {
            float currentHealth = 0.0f;
            float maximumHealth = 0.0f;
            if (combat_->ReadCombatHealth(
                    hero, currentHealth, maximumHealth))
            {
                game::creature::combat::CombatHealthMutationEvent baseline;
                baseline.creature = hero;
                baseline.currentHealth = currentHealth;
                baseline.maximumHealth = maximumHealth;
                baseline.observedAt = GetTickCount64();
                if (!AuthorPlayer(baseline))
                {
                    return false;
                }
            }
        }

        std::unordered_set<std::uint64_t> retained;
        const std::vector<entities::LiveEntityRecord> liveSnapshot =
            liveEntities.Snapshot();
        retained.reserve(liveSnapshot.size());
        for (const entities::LiveEntityRecord& local : liveSnapshot)
        {
            if (local.thing == nullptr || !local.creature ||
                !entities::LiveEntityRegistry::IsReplicable(local))
            {
                continue;
            }
            const std::uint64_t canonicalUid = identities_->Canonicalize(
                local.thingUid);
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(canonicalUid);
            if (world == nullptr || !world->live || !world->available ||
                !world->creature ||
                !authority_->IsEntityPublisher(
                    {world->thingUid, world->generation},
                    world->mapName,
                    localActorId_,
                    world->mapEpoch))
            {
                continue;
            }
            retained.insert(world->thingUid);
            const auto existing = authoredEntityBaselines_.find(
                world->thingUid);
            if (existing != authoredEntityBaselines_.end() &&
                existing->second.creature == local.thing &&
                existing->second.generation == world->generation &&
                existing->second.mapEpoch == world->mapEpoch &&
                existing->second.publisherActorId == localActorId_)
            {
                continue;
            }

            // A cross-map transfer destroys the source CThing and later
            // creates a new native incarnation without changing the canonical
            // entity generation. Restore the last host-revisioned health
            // before the new map owner publishes its baseline, otherwise the
            // definition's default health would silently replace combat state.
            const auto retainedVitals = latestEntities_.find(
                world->thingUid);
            if (retainedVitals != latestEntities_.end() &&
                retainedVitals->second.entityGeneration ==
                    world->generation)
            {
                AppliedEntity& applied = appliedEntities_[world->thingUid];
                if (applied.creature != local.thing ||
                    applied.revision != retainedVitals->second.revision)
                {
                    if (!combat_->ApplyAuthoritativeCombatHealth(
                            local.thing,
                            retainedVitals->second.currentHealth,
                            retainedVitals->second.maximumHealth))
                    {
                        continue;
                    }
                    applied.creature = local.thing;
                    applied.revision = retainedVitals->second.revision;
                    char detail[320] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "thing_uid=%016llX generation=%u script_name=%s map=%s epoch=%u retained_revision=%u health=%.3f maximum=%.3f",
                        static_cast<unsigned long long>(world->thingUid),
                        world->generation,
                        world->scriptName.c_str(),
                        world->mapName.c_str(),
                        world->mapEpoch,
                        retainedVitals->second.revision,
                        retainedVitals->second.currentHealth,
                        retainedVitals->second.maximumHealth);
                    diagnostics_.Event(
                        "MultiplayerEntityVitalsRestored",
                        detail);
                }
            }

            float currentHealth = 0.0f;
            float maximumHealth = 0.0f;
            if (!combat_->ReadCombatHealth(
                    local.thing, currentHealth, maximumHealth))
            {
                continue;
            }
            game::creature::combat::CombatHealthMutationEvent baseline;
            baseline.creature = local.thing;
            baseline.thingUid = world->thingUid;
            baseline.currentHealth = currentHealth;
            baseline.maximumHealth = maximumHealth;
            baseline.observedAt = GetTickCount64();
            bool deferred = false;
            if (!AuthorEntity(
                    baseline,
                    liveEntities,
                    baseline.observedAt,
                    deferred))
            {
                return false;
            }
        }

        for (auto current = authoredEntityBaselines_.begin();
             current != authoredEntityBaselines_.end();)
        {
            if (retained.find(current->first) == retained.end())
            {
                current = authoredEntityBaselines_.erase(current);
            }
            else
            {
                ++current;
            }
        }
        return true;
    }

    bool EntityVitalsReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ ||
            transportMessage.type != protocol::PacketType::EntityVitals)
        {
            return false;
        }
        protocol::EntityVitalsMessage message;
        if (!protocol::DecodeEntityVitalsMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerEntityVitalsRejected", "invalid payload");
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            return HostAccept(
                std::move(message),
                transportMessage.sourceActorId,
                transportMessage.connectionNonce,
                processingLiveEntities_) && PublishPending();
        }
        return AcceptAuthoritative(
            message, transportMessage.connectionNonce);
    }

    bool EntityVitalsReplication::HostAccept(
        protocol::EntityVitalsMessage message,
        std::uint64_t sourceActorId,
        const std::uint64_t sourceConnectionNonce,
        const entities::LiveEntityRegistry*)
    {
        if (sourceActorId == 0 || sourceActorId == localActorId_ ||
            message.ownerActorId != sourceActorId)
        {
            return true;
        }
        if (message.subject == protocol::EntityVitalsSubject::Player)
        {
            if (message.playerActorId != sourceActorId)
            {
                return true;
            }
            const PlayerState* const player =
                remotePlayerChannels_ != nullptr
                    ? remotePlayerChannels_->Find(message.playerActorId)
                    : nullptr;
            const RemotePlayerLifecycle* const playerLifecycle =
                remotePlayerChannels_ != nullptr
                    ? remotePlayerChannels_->FindLifecycle(
                        message.playerActorId)
                    : nullptr;
            if (player == nullptr ||
                player->authorityEpoch != message.playerAuthorityEpoch ||
                playerLifecycle == nullptr ||
                (playerLifecycle->connectionNonce != 0 &&
                    sourceConnectionNonce !=
                        playerLifecycle->connectionNonce) ||
                !remotePlayerChannels_->IsLifecycleActive(
                    message.playerActorId,
                    message.playerActorGeneration,
                    message.playerMapEpoch))
            {
                diagnostics_.Event(
                    "MultiplayerEntityVitalsRejected",
                    "player health mutation was fenced by actor lifecycle");
                return true;
            }
            auto existing = latestPlayers_.find(message.playerActorId);
            const auto existingSession = latestPlayerConnectionNonces_.find(
                message.playerActorId);
            if (existing != latestPlayers_.end() &&
                existingSession != latestPlayerConnectionNonces_.end() &&
                existingSession->second != 0 &&
                existingSession->second != sourceConnectionNonce)
            {
                latestPlayers_.erase(existing);
                latestPlayerConnectionNonces_.erase(existingSession);
                hostPlayerRevisions_.erase(message.playerActorId);
                existing = latestPlayers_.end();
            }
            if (existing != latestPlayers_.end() &&
                message.playerAuthorityEpoch ==
                    existing->second.playerAuthorityEpoch &&
                (message.playerActorGeneration <
                        existing->second.playerActorGeneration ||
                    (message.playerActorGeneration ==
                            existing->second.playerActorGeneration &&
                        message.playerMapEpoch <
                            existing->second.playerMapEpoch)))
            {
                diagnostics_.Event(
                    "MultiplayerEntityVitalsRejected",
                    "player health mutation was from a stale actor lifecycle");
                return true;
            }
            message.revision = NextHostRevision(message);
            latestPlayers_[message.playerActorId] = message;
            latestPlayerConnectionNonces_[message.playerActorId] =
                sourceConnectionNonce;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "subject=player actor=%llu owner=%llu revision=%u health=%.3f maximum=%.3f",
                static_cast<unsigned long long>(message.playerActorId),
                static_cast<unsigned long long>(message.ownerActorId),
                message.revision,
                message.currentHealth,
                message.maximumHealth);
            diagnostics_.Event("MultiplayerEntityVitalsAccepted", detail);
            return Publish(std::move(message));
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(message.entityUid);
        const authority::EntityAuthorityKey key{
            message.entityUid, message.entityGeneration};
        if (world == nullptr || !world->live || !world->available ||
            !world->creature || world->generation != message.entityGeneration ||
            world->mapName != message.mapName ||
            world->mapEpoch != message.mapEpoch ||
            !authority_->IsEntityPublisher(
                key,
                message.mapName,
                sourceActorId,
                message.mapEpoch))
        {
            diagnostics_.Event(
                "MultiplayerEntityVitalsRejected",
                "world-entity mutation was fenced by lifecycle or authority");
            return true;
        }
        message.revision = NextHostRevision(message);
        latestEntities_[message.entityUid] = message;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "subject=entity thing_uid=%016llX generation=%u owner=%llu map=%s epoch=%u revision=%u health=%.3f maximum=%.3f",
            static_cast<unsigned long long>(message.entityUid),
            message.entityGeneration,
            static_cast<unsigned long long>(message.ownerActorId),
            message.mapName.c_str(),
            message.mapEpoch,
            message.revision,
            message.currentHealth,
            message.maximumHealth);
        diagnostics_.Event("MultiplayerEntityVitalsAccepted", detail);
        return Publish(std::move(message));
    }

    bool EntityVitalsReplication::AcceptAuthoritative(
        const protocol::EntityVitalsMessage& message,
        const std::uint64_t sourceConnectionNonce)
    {
        if (message.subject == protocol::EntityVitalsSubject::Player)
        {
            if (message.playerActorId != localActorId_)
            {
                const RemotePlayerLifecycle* const lifecycle =
                    remotePlayerChannels_ != nullptr
                        ? remotePlayerChannels_->FindLifecycle(
                            message.playerActorId)
                        : nullptr;
                if (lifecycle == nullptr ||
                    (lifecycle->connectionNonce != 0 &&
                        lifecycle->connectionNonce != sourceConnectionNonce))
                {
                    diagnostics_.Event(
                        "MultiplayerEntityVitalsRejected",
                        "authoritative player health was from a stale transport session");
                    return true;
                }
            }
            auto existing = latestPlayers_.find(message.playerActorId);
            const auto existingSession = latestPlayerConnectionNonces_.find(
                message.playerActorId);
            if (existing != latestPlayers_.end() &&
                existingSession != latestPlayerConnectionNonces_.end() &&
                existingSession->second != 0 &&
                existingSession->second != sourceConnectionNonce)
            {
                latestPlayers_.erase(existing);
                latestPlayerConnectionNonces_.erase(existingSession);
                existing = latestPlayers_.end();
            }
            if (existing != latestPlayers_.end() &&
                message.playerAuthorityEpoch ==
                    existing->second.playerAuthorityEpoch &&
                (message.playerActorGeneration <
                        existing->second.playerActorGeneration ||
                    (message.playerActorGeneration ==
                            existing->second.playerActorGeneration &&
                        message.playerMapEpoch <
                            existing->second.playerMapEpoch)))
            {
                diagnostics_.Event(
                    "MultiplayerEntityVitalsRejected",
                    "authoritative player health was from a stale actor lifecycle");
                return true;
            }
            if (existing == latestPlayers_.end() ||
                message.playerAuthorityEpoch !=
                    existing->second.playerAuthorityEpoch ||
                message.playerActorGeneration !=
                    existing->second.playerActorGeneration ||
                message.playerMapEpoch != existing->second.playerMapEpoch ||
                IsNewer(message.revision, existing->second.revision))
            {
                latestPlayers_[message.playerActorId] = message;
                latestPlayerConnectionNonces_[message.playerActorId] =
                    sourceConnectionNonce;
            }
            return true;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(message.entityUid);
        // The host already validated the mutation against the map/action
        // publisher before assigning this revision. Health belongs to the
        // canonical entity generation, not to one CTCMapwho incarnation, so a
        // guest may retain the last source-map value while that entity is
        // dormant or already fenced into its destination map.
        if (world == nullptr || !world->available ||
            world->generation != message.entityGeneration)
        {
            return true;
        }
        const auto existing = latestEntities_.find(message.entityUid);
        if (existing == latestEntities_.end() ||
            IsNewer(message.revision, existing->second.revision))
        {
            latestEntities_[message.entityUid] = message;
        }
        return true;
    }

    bool EntityVitalsReplication::Publish(
        protocol::EntityVitalsMessage message)
    {
        const std::uint64_t actorId = message.subject ==
                protocol::EntityVitalsSubject::Player
            ? message.playerActorId
            : message.ownerActorId;
        std::size_t actorMessages = 0;
        for (auto& pending : pending_)
        {
            const std::uint64_t pendingActorId = pending.subject ==
                    protocol::EntityVitalsSubject::Player
                ? pending.playerActorId
                : pending.ownerActorId;
            if (pendingActorId == actorId)
            {
                ++actorMessages;
            }
            const bool samePlayer = message.subject ==
                    protocol::EntityVitalsSubject::Player &&
                pending.subject == protocol::EntityVitalsSubject::Player &&
                pending.playerActorId == message.playerActorId &&
                pending.playerAuthorityEpoch == message.playerAuthorityEpoch &&
                pending.playerActorGeneration ==
                    message.playerActorGeneration &&
                pending.playerMapEpoch == message.playerMapEpoch;
            const bool sameEntity = message.subject ==
                    protocol::EntityVitalsSubject::WorldEntity &&
                pending.subject == protocol::EntityVitalsSubject::WorldEntity &&
                pending.entityUid == message.entityUid &&
                pending.entityGeneration == message.entityGeneration;
            if (samePlayer || sameEntity)
            {
                pending = std::move(message);
                return PublishPending();
            }
        }
        if (actorMessages >= MessageCapacity / 4)
        {
            diagnostics_.Event(
                "MultiplayerEntityVitalsOverflow",
                "bounded health publication queue is full for actor lifecycle");
            return false;
        }
        if (pending_.size() >= MessageCapacity)
        {
            return false;
        }
        pending_.push_back(std::move(message));
        return PublishPending();
    }

    bool EntityVitalsReplication::PublishPending()
    {
        const std::size_t scheduled = pending_.size();
        std::unordered_set<ReliableStreamId> attemptedStreams;
        bool deferred = false;
        for (std::size_t attempt = 0;
             attempt < scheduled && !pending_.empty(); ++attempt)
        {
            const protocol::EntityVitalsMessage& queued = pending_.front();
            if (queued.subject == protocol::EntityVitalsSubject::Player)
            {
                const auto latest = latestPlayers_.find(
                    queued.playerActorId);
                const PlayerState* const local =
                    queued.playerActorId == localActorId_ &&
                        processingLocalHero_ != nullptr
                    ? processingLocalHero_->CurrentState()
                    : nullptr;
                const bool latestMismatch = latest != latestPlayers_.end() &&
                    (latest->second.playerActorGeneration !=
                            queued.playerActorGeneration ||
                        latest->second.playerAuthorityEpoch !=
                            queued.playerAuthorityEpoch ||
                        latest->second.playerMapEpoch !=
                            queued.playerMapEpoch);
                const bool localMismatch = local != nullptr &&
                    (local->authorityEpoch != queued.playerAuthorityEpoch ||
                        local->actorGeneration != queued.playerActorGeneration ||
                        local->mapEpoch != queued.playerMapEpoch);
                if (latestMismatch || localMismatch)
                {
                    pending_.pop_front();
                    continue;
                }
            }
            const ReliableStreamId streamId = queued.subject ==
                    protocol::EntityVitalsSubject::Player
                ? reliable_stream::Actor(queued.playerActorId)
                : reliable_stream::Entity(queued.entityUid);
            if (!attemptedStreams.insert(streamId).second)
            {
                pending_.push_back(std::move(pending_.front()));
                pending_.pop_front();
                continue;
            }
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodeEntityVitalsMessage(
                    pending_.front(),
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    streamId,
                    protocol::PacketType::EntityVitals,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                deferred = true;
                pending_.push_back(std::move(pending_.front()));
                pending_.pop_front();
                continue;
            }
            pending_.pop_front();
        }
        if (deferred && !publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityVitalsPublishDeferred",
                "one or more health streams are waiting for transport capacity");
            publishBackpressured_ = true;
        }
        else if (!deferred && publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityVitalsPublishResumed",
                "queued health state has entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    bool EntityVitalsReplication::PublishPeerBaseline()
    {
        if (role_ != PeerRole::Host || transport_ == nullptr)
        {
            return true;
        }
        const std::uint64_t peerRevision = transport_->PeerSetRevision();
        if (peerRevision == knownPeerRevision_)
        {
            return true;
        }

        for (const auto& [actorId, message] : latestPlayers_)
        {
            (void)actorId;
            if (!Publish(message))
            {
                return false;
            }
        }
        for (const auto& [entityUid, message] : latestEntities_)
        {
            (void)entityUid;
            if (!Publish(message))
            {
                return false;
            }
        }
        knownPeerRevision_ = peerRevision;
        diagnostics_.Event(
            "MultiplayerEntityVitalsBaselinePublished",
            "current player and world-entity health was replayed for the changed peer set");
        return true;
    }

    void EntityVitalsReplication::RetirePlayer(
        std::uint64_t actorId) noexcept
    {
        if (actorId == 0 || actorId == localActorId_)
        {
            return;
        }
        const auto latest = latestPlayers_.find(actorId);
        const auto sourceSession = latestPlayerConnectionNonces_.find(actorId);
        const RemotePlayerLifecycle* const lifecycle =
            remotePlayerChannels_ != nullptr
                ? remotePlayerChannels_->FindLifecycle(actorId)
                : nullptr;
        if (latest != latestPlayers_.end() &&
            sourceSession != latestPlayerConnectionNonces_.end() &&
            lifecycle != nullptr && lifecycle->active &&
            latest->second.playerActorGeneration ==
                lifecycle->actorGeneration &&
            latest->second.playerMapEpoch == lifecycle->mapEpoch &&
            (lifecycle->connectionNonce == 0 ||
                (sourceSession->second != 0 &&
                    sourceSession->second == lifecycle->connectionNonce)))
        {
            return;
        }
        latestPlayers_.erase(actorId);
        latestPlayerConnectionNonces_.erase(actorId);
        hostPlayerRevisions_.erase(actorId);
        for (auto pending = pending_.begin(); pending != pending_.end();)
        {
            if (pending->subject == protocol::EntityVitalsSubject::Player &&
                pending->playerActorId == actorId)
            {
                pending = pending_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
    }

    void EntityVitalsReplication::ClearRemotePlayers() noexcept
    {
        for (auto player = latestPlayers_.begin();
             player != latestPlayers_.end();)
        {
            if (player->first == localActorId_)
            {
                ++player;
            }
            else
            {
                latestPlayerConnectionNonces_.erase(player->first);
                hostPlayerRevisions_.erase(player->first);
                player = latestPlayers_.erase(player);
            }
        }
        for (auto pending = pending_.begin(); pending != pending_.end();)
        {
            if (pending->subject == protocol::EntityVitalsSubject::Player &&
                pending->playerActorId != localActorId_)
            {
                pending = pending_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
    }

    void EntityVitalsReplication::ApplyLatest(
        const entities::LiveEntityRegistry& liveEntities,
        presentation::RemotePlayerRegistry& remotePlayers)
    {
        std::vector<std::uint64_t> stalePlayers;
        for (const auto& [actorId, message] : latestPlayers_)
        {
            if (actorId == localActorId_)
            {
                continue;
            }
            const PlayerState* const player =
                remotePlayerChannels_ != nullptr
                    ? remotePlayerChannels_->Find(actorId)
                    : nullptr;
            const RemotePlayerLifecycle* const lifecycle =
                remotePlayerChannels_ != nullptr
                    ? remotePlayerChannels_->FindLifecycle(actorId)
                    : nullptr;
            const auto sourceSession =
                latestPlayerConnectionNonces_.find(actorId);
            if (player == nullptr ||
                player->authorityEpoch != message.playerAuthorityEpoch ||
                lifecycle == nullptr ||
                (sourceSession != latestPlayerConnectionNonces_.end() &&
                    sourceSession->second != 0 &&
                    lifecycle->connectionNonce != 0 &&
                    sourceSession->second != lifecycle->connectionNonce) ||
                !remotePlayerChannels_->IsLifecycleActive(
                    actorId,
                    message.playerActorGeneration,
                    message.playerMapEpoch))
            {
                stalePlayers.push_back(actorId);
                continue;
            }
            remotePlayers.ApplyHealth(
                actorId,
                message.currentHealth,
                message.maximumHealth,
                message.revision);
        }
        for (const std::uint64_t actorId : stalePlayers)
        {
            RetirePlayer(actorId);
        }
        std::vector<std::uint64_t> stale;
        for (const auto& [entityUid, message] : latestEntities_)
        {
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(entityUid);
            const entities::LiveEntityRecord* const live =
                liveEntities.Find(entityUid);
            if (world == nullptr || !world->available ||
                world->generation != message.entityGeneration)
            {
                stale.push_back(entityUid);
                continue;
            }
            if (!world->live || live == nullptr || live->thing == nullptr ||
                message.ownerActorId == localActorId_)
            {
                continue;
            }
            AppliedEntity& applied = appliedEntities_[entityUid];
            if (applied.creature == live->thing &&
                applied.revision == message.revision)
            {
                continue;
            }
            if (combat_->ApplyAuthoritativeCombatHealth(
                    live->thing,
                    message.currentHealth,
                    message.maximumHealth))
            {
                applied.creature = live->thing;
                applied.revision = message.revision;
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "subject=entity thing_uid=%016llX generation=%u owner=%llu map=%s epoch=%u revision=%u health=%.3f maximum=%.3f",
                    static_cast<unsigned long long>(message.entityUid),
                    message.entityGeneration,
                    static_cast<unsigned long long>(message.ownerActorId),
                    message.mapName.c_str(),
                    message.mapEpoch,
                    message.revision,
                    message.currentHealth,
                    message.maximumHealth);
                diagnostics_.Event("MultiplayerEntityVitalsApplied", detail);
            }
        }
        for (const std::uint64_t entityUid : stale)
        {
            latestEntities_.erase(entityUid);
            appliedEntities_.erase(entityUid);
            hostEntityRevisions_.erase(entityUid);
        }
    }

    std::uint32_t EntityVitalsReplication::NextHostRevision(
        const protocol::EntityVitalsMessage& message) noexcept
    {
        std::uint32_t& revision =
            message.subject == protocol::EntityVitalsSubject::Player
                ? hostPlayerRevisions_[message.playerActorId]
                : hostEntityRevisions_[message.entityUid];
        revision = revision == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : revision + 1u;
        return revision;
    }

    std::uint32_t EntityVitalsReplication::NextLocalRevision() noexcept
    {
        nextLocalRevision_ =
            nextLocalRevision_ == (std::numeric_limits<std::uint32_t>::max)()
                ? 1u
                : nextLocalRevision_ + 1u;
        return nextLocalRevision_;
    }

    bool EntityVitalsReplication::IsNewer(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return current == 0 || (candidate != current &&
            static_cast<std::int32_t>(candidate - current) > 0);
    }

    void EntityVitalsReplication::CaptureMutation(
        void* context,
        const game::creature::combat::CombatHealthMutationEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityVitalsReplication*>(context)->Enqueue(event);
        }
    }

    void EntityVitalsReplication::Enqueue(
        const game::creature::combat::CombatHealthMutationEvent& event) noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        if (events_.size() >= EventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        events_.push_back(event);
    }

    void EntityVitalsReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (combat_ != nullptr)
        {
            combat_->SetHealthMutationSink(nullptr, nullptr);
        }
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            events_.clear();
        }
        deferred_.clear();
        pending_.clear();
        latestPlayers_.clear();
        latestPlayerConnectionNonces_.clear();
        latestEntities_.clear();
        hostPlayerRevisions_.clear();
        hostEntityRevisions_.clear();
        appliedEntities_.clear();
        authoredEntityBaselines_.clear();
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        remotePlayerChannels_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        processingLocalHero_ = nullptr;
        processingLiveEntities_ = nullptr;
        authoredPlayerCreature_ = nullptr;
        nextLocalRevision_ = 0;
        knownPeerRevision_ = 0;
        publishBackpressured_ = false;
        droppedEvents_.store(0, std::memory_order_release);
        reportedDroppedEvents_ = 0;
        initialized_ = false;
    }
}

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolveEntityVitalsSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.actions.entityVitals;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkEntityVitals,
    0x1005u,
    "entity-vitals",
    500u,
    "multiplayer-entity-vitals-dispatch",
    ResolveEntityVitalsSink);
