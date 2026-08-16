#include "EntityLowSimulationReplication.h"

#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Protocol/EntityLowSimulationMessageCodec.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    fable::game::npc::simulation::DummyVillagerState ToNative(
        const fable::multiplayer::protocol::EntityLowSimulationMessage&
            message) noexcept
    {
        fable::game::npc::simulation::DummyVillagerState state;
        state.recreationDay = message.recreationDay;
        state.recreationFrame = message.recreationFrame;
        state.respawnable = message.respawnable;
        state.guard = message.guard;
        state.componentPresent = true;
        return state;
    }
}

namespace fable::multiplayer::replication
{
    void EntityLowSimulationReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        game::npc::simulation::DummyVillagerService& dummyVillagers,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        dummyVillagers_ = &dummyVillagers;
        diagnostics_ = diagnostics;
        initialized_ = localActorId_ != 0;
        RefreshHostProjectionSnapshot();
        acceptingEvents_.store(initialized_, std::memory_order_release);
        dummyVillagers_->SetMutationSink(
            &EntityLowSimulationReplication::CaptureMutation, this);
        dummyVillagers_->SetProjectionSink(
            &EntityLowSimulationReplication::ResolveHostProjection, this);
        diagnostics_.Event(
            "MultiplayerEntityLowSimulationReady",
            "host-revisioned CTCDummyVillager recreation state is fenced by current map ownership");
    }

    bool EntityLowSimulationReplication::Process(
        const entities::LiveEntityRegistry& liveEntities)
    {
        if (!initialized_ || transport_ == nullptr || authority_ == nullptr ||
            lifecycle_ == nullptr || identities_ == nullptr ||
            dummyVillagers_ == nullptr)
        {
            return false;
        }
        std::deque<game::npc::simulation::DummyVillagerMutationEvent>
            captured;
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
            bool deferred = false;
            if (!Author(event, liveEntities, now, deferred))
            {
                return false;
            }
            if (deferred && deferred_.size() < EventCapacity)
            {
                deferred_.push_back(event);
            }
        }
        if (!PublishBaselines(liveEntities) || !PublishPeerBaseline())
        {
            return false;
        }
        const unsigned int dropped = droppedEvents_.load(
            std::memory_order_acquire);
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            diagnostics_.Event(
                "MultiplayerEntityLowSimulationOverflow",
                "native CTCDummyVillager mutations exceeded the bounded queue");
            return false;
        }
        ApplyLatest(liveEntities);
        RefreshHostProjectionSnapshot();
        return PublishPending();
    }

    bool EntityLowSimulationReplication::Author(
        const game::npc::simulation::DummyVillagerMutationEvent& event,
        const entities::LiveEntityRegistry& liveEntities,
        std::uint64_t now,
        bool& deferred)
    {
        deferred = false;
        const std::uint64_t canonicalUid =
            identities_->CanonicalizeLocalObservation(event.thingUid);
        const entities::LiveEntityRecord* const live =
            liveEntities.Find(canonicalUid);
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(canonicalUid);
        if (live == nullptr || live->thing != event.thing ||
            !entities::LiveEntityRegistry::IsReplicable(*live) ||
            world == nullptr || !world->live || !world->available)
        {
            return true;
        }
        if (!authority_->IsMapPublisher(
                world->mapName, localActorId_, world->mapEpoch))
        {
            deferred = now >= event.observedAt &&
                now - event.observedAt <= AuthorityGraceMilliseconds;
            return true;
        }
        protocol::EntityLowSimulationMessage message;
        message.entityUid = canonicalUid;
        message.entityGeneration = world->generation;
        message.ownerActorId = localActorId_;
        message.mapEpoch = world->mapEpoch;
        message.revision = NextLocalRevision();
        message.recreationDay = event.current.recreationDay;
        message.recreationFrame = event.current.recreationFrame;
        message.respawnable = event.current.respawnable;
        message.guard = event.current.guard;
        message.mapName = world->mapName;
        if (role_ == PeerRole::Host)
        {
            message.revision = NextHostRevision(canonicalUid);
            latest_[canonicalUid] = message;
            RefreshHostProjectionSnapshot();
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u owner=%llu map=%s epoch=%u revision=%u day=%d frame=%d respawnable=%s guard=%s",
            static_cast<unsigned long long>(canonicalUid),
            message.entityGeneration,
            static_cast<unsigned long long>(message.ownerActorId),
            message.mapName.c_str(),
            message.mapEpoch,
            message.revision,
            message.recreationDay,
            message.recreationFrame,
            message.respawnable ? "true" : "false",
            message.guard ? "true" : "false");
        diagnostics_.Event("MultiplayerEntityLowSimulationPublished", detail);
        return Publish(std::move(message));
    }

    bool EntityLowSimulationReplication::PublishBaselines(
        const entities::LiveEntityRegistry& liveEntities)
    {
        std::unordered_set<std::uint64_t> retained;
        const std::vector<entities::LiveEntityRecord> snapshot =
            liveEntities.Snapshot();
        retained.reserve(snapshot.size());
        for (const entities::LiveEntityRecord& local : snapshot)
        {
            if (local.thing == nullptr ||
                !entities::LiveEntityRegistry::IsReplicable(local))
            {
                continue;
            }
            const std::uint64_t canonicalUid = identities_->Canonicalize(
                local.thingUid);
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(canonicalUid);
            if (world == nullptr || !world->live || !world->available ||
                !authority_->IsMapPublisher(
                    world->mapName, localActorId_, world->mapEpoch))
            {
                continue;
            }
            game::npc::simulation::DummyVillagerState state;
            if (!dummyVillagers_->Read(local.thing, state) ||
                !state.componentPresent)
            {
                continue;
            }
            retained.insert(canonicalUid);
            const auto existing = authoredBaselines_.find(canonicalUid);
            if (existing != authoredBaselines_.end() &&
                existing->second.thing == local.thing &&
                existing->second.generation == world->generation &&
                existing->second.mapEpoch == world->mapEpoch &&
                existing->second.publisherActorId == localActorId_)
            {
                continue;
            }

            const auto retainedState = latest_.find(canonicalUid);
            if (retainedState != latest_.end() &&
                retainedState->second.entityGeneration == world->generation)
            {
                bool changed = false;
                if (!dummyVillagers_->ApplyAuthoritative(
                        local.thing, ToNative(retainedState->second), changed))
                {
                    continue;
                }
                if (!dummyVillagers_->Read(local.thing, state))
                {
                    continue;
                }
                if (changed)
                {
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "thing_uid=%016llX generation=%u map=%s epoch=%u retained_revision=%u",
                        static_cast<unsigned long long>(canonicalUid),
                        world->generation,
                        world->mapName.c_str(),
                        world->mapEpoch,
                        retainedState->second.revision);
                    diagnostics_.Event(
                        "MultiplayerEntityLowSimulationRestored", detail);
                }
            }

            game::npc::simulation::DummyVillagerMutationEvent baseline;
            baseline.thing = local.thing;
            baseline.thingUid = canonicalUid;
            baseline.current = state;
            baseline.observedAt = GetTickCount64();
            bool deferred = false;
            if (!Author(
                    baseline,
                    liveEntities,
                    baseline.observedAt,
                    deferred))
            {
                return false;
            }
            authoredBaselines_[canonicalUid] = {
                local.thing,
                world->generation,
                world->mapEpoch,
                localActorId_,
            };
        }
        for (auto current = authoredBaselines_.begin();
             current != authoredBaselines_.end();)
        {
            if (retained.find(current->first) == retained.end())
            {
                current = authoredBaselines_.erase(current);
            }
            else
            {
                ++current;
            }
        }
        return true;
    }

    bool EntityLowSimulationReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ || transportMessage.type !=
                protocol::PacketType::EntityLowSimulation)
        {
            return false;
        }
        protocol::EntityLowSimulationMessage message;
        if (!protocol::DecodeEntityLowSimulationMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerEntityLowSimulationRejected",
                "invalid payload");
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            return HostAccept(
                std::move(message), transportMessage.sourceActorId) &&
                PublishPending();
        }
        return AcceptAuthoritative(message);
    }

    bool EntityLowSimulationReplication::HostAccept(
        protocol::EntityLowSimulationMessage message,
        std::uint64_t sourceActorId)
    {
        if (sourceActorId == 0 || sourceActorId == localActorId_ ||
            message.ownerActorId != sourceActorId)
        {
            return true;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(message.entityUid);
        if (world == nullptr || !world->live || !world->available ||
            world->generation != message.entityGeneration ||
            world->mapName != message.mapName ||
            world->mapEpoch != message.mapEpoch ||
            !authority_->IsMapPublisher(
                message.mapName, sourceActorId, message.mapEpoch))
        {
            diagnostics_.Event(
                "MultiplayerEntityLowSimulationRejected",
                "mutation was fenced by lifecycle or current map ownership");
            return true;
        }
        message.revision = NextHostRevision(message.entityUid);
        latest_[message.entityUid] = message;
        RefreshHostProjectionSnapshot();
        diagnostics_.Event(
            "MultiplayerEntityLowSimulationAccepted",
            "guest-owned CTCDummyVillager mutation received a host revision");
        return Publish(std::move(message));
    }

    bool EntityLowSimulationReplication::AcceptAuthoritative(
        const protocol::EntityLowSimulationMessage& message)
    {
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(message.entityUid);
        if (world == nullptr || !world->available ||
            world->generation != message.entityGeneration)
        {
            return true;
        }
        const auto existing = latest_.find(message.entityUid);
        if (existing == latest_.end() ||
            IsNewer(message.revision, existing->second.revision))
        {
            latest_[message.entityUid] = message;
        }
        return true;
    }

    bool EntityLowSimulationReplication::Publish(
        protocol::EntityLowSimulationMessage message)
    {
        if (pending_.size() >= MessageCapacity)
        {
            return false;
        }
        pending_.push_back(std::move(message));
        return PublishPending();
    }

    bool EntityLowSimulationReplication::PublishPending()
    {
        while (!pending_.empty())
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload =
                {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodeEntityLowSimulationMessage(
                    pending_.front(),
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    protocol::PacketType::EntityLowSimulation,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                if (!publishBackpressured_)
                {
                    diagnostics_.Event(
                        "MultiplayerEntityLowSimulationPublishDeferred",
                        "ordered baseline traffic is draining before low-sim state");
                    publishBackpressured_ = true;
                }
                return true;
            }
            pending_.pop_front();
        }
        if (publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityLowSimulationPublishResumed",
                "queued low-sim state entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    bool EntityLowSimulationReplication::PublishPeerBaseline()
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
        for (const auto& [entityUid, message] : latest_)
        {
            (void)entityUid;
            if (!Publish(message))
            {
                return false;
            }
        }
        knownPeerRevision_ = peerRevision;
        diagnostics_.Event(
            "MultiplayerEntityLowSimulationBaselinePublished",
            "current bounded CTCDummyVillager table was replayed for the changed peer set");
        return true;
    }

    void EntityLowSimulationReplication::ApplyLatest(
        const entities::LiveEntityRegistry& liveEntities)
    {
        std::vector<std::uint64_t> stale;
        for (const auto& [entityUid, message] : latest_)
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
                authority_->IsMapPublisher(
                    world->mapName, localActorId_, world->mapEpoch))
            {
                continue;
            }
            AppliedState& applied = applied_[entityUid];
            if (applied.thing == live->thing &&
                applied.revision == message.revision)
            {
                continue;
            }
            bool changed = false;
            if (dummyVillagers_->ApplyAuthoritative(
                    live->thing, ToNative(message), changed))
            {
                applied.thing = live->thing;
                applied.revision = message.revision;
                if (changed)
                {
                    diagnostics_.Event(
                        "MultiplayerEntityLowSimulationApplied",
                        "host-revisioned CTCDummyVillager schedule applied to local native state");
                }
            }
        }
        for (const std::uint64_t entityUid : stale)
        {
            latest_.erase(entityUid);
            applied_.erase(entityUid);
            hostRevisions_.erase(entityUid);
        }
    }

    void EntityLowSimulationReplication::RefreshHostProjectionSnapshot()
    {
        auto snapshot = std::make_shared<HostProjectionTable>();
        if (role_ == PeerRole::Host && identities_ != nullptr &&
            lifecycle_ != nullptr)
        {
            snapshot->reserve(latest_.size() * 2u);
            for (const auto& [canonicalUid, message] : latest_)
            {
                const entities::WorldEntityRecord* const world =
                    lifecycle_->Directory().Find(canonicalUid);
                if (world == nullptr || !world->available ||
                    world->generation != message.entityGeneration)
                {
                    continue;
                }
                const game::npc::simulation::DummyVillagerState state =
                    ToNative(message);
                snapshot->insert_or_assign(canonicalUid, state);
                const std::uint64_t localUid =
                    identities_->FindLocal(canonicalUid);
                if (localUid != 0)
                {
                    snapshot->insert_or_assign(localUid, state);
                }
            }
        }
        std::shared_ptr<const HostProjectionTable> published =
            std::move(snapshot);
        std::atomic_store_explicit(
            &hostProjectionSnapshot_,
            std::move(published),
            std::memory_order_release);
    }

    std::uint32_t EntityLowSimulationReplication::NextHostRevision(
        std::uint64_t entityUid) noexcept
    {
        std::uint32_t& revision = hostRevisions_[entityUid];
        revision = revision == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : revision + 1u;
        return revision;
    }

    std::uint32_t EntityLowSimulationReplication::NextLocalRevision() noexcept
    {
        nextLocalRevision_ =
            nextLocalRevision_ == (std::numeric_limits<std::uint32_t>::max)()
                ? 1u
                : nextLocalRevision_ + 1u;
        return nextLocalRevision_;
    }

    bool EntityLowSimulationReplication::IsNewer(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return current == 0 || (candidate != current &&
            static_cast<std::int32_t>(candidate - current) > 0);
    }

    void EntityLowSimulationReplication::CaptureMutation(
        void* context,
        const game::npc::simulation::DummyVillagerMutationEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityLowSimulationReplication*>(context)->Enqueue(
                event);
        }
    }

    bool EntityLowSimulationReplication::ResolveHostProjection(
        void* context,
        std::uint64_t thingUid,
        game::npc::simulation::DummyVillagerState& state)
    {
        auto* const replication = static_cast<
            EntityLowSimulationReplication*>(context);
        if (replication == nullptr)
        {
            return false;
        }
        const std::shared_ptr<const HostProjectionTable> snapshot =
            std::atomic_load_explicit(
                &replication->hostProjectionSnapshot_,
                std::memory_order_acquire);
        if (snapshot == nullptr)
        {
            return false;
        }
        const auto match = snapshot->find(thingUid);
        if (match == snapshot->end())
        {
            return false;
        }
        state = match->second;
        return true;
    }

    void EntityLowSimulationReplication::Enqueue(
        const game::npc::simulation::DummyVillagerMutationEvent& event)
        noexcept
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

    void EntityLowSimulationReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (dummyVillagers_ != nullptr)
        {
            dummyVillagers_->SetMutationSink(nullptr, nullptr);
            dummyVillagers_->SetProjectionSink(nullptr, nullptr);
        }
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            events_.clear();
        }
        deferred_.clear();
        pending_.clear();
        latest_.clear();
        hostRevisions_.clear();
        applied_.clear();
        authoredBaselines_.clear();
        std::shared_ptr<const HostProjectionTable> empty =
            std::make_shared<HostProjectionTable>();
        std::atomic_store_explicit(
            &hostProjectionSnapshot_,
            std::move(empty),
            std::memory_order_release);
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        dummyVillagers_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextLocalRevision_ = 0;
        knownPeerRevision_ = 0;
        publishBackpressured_ = false;
        droppedEvents_.store(0, std::memory_order_release);
        reportedDroppedEvents_ = 0;
        initialized_ = false;
    }
}
