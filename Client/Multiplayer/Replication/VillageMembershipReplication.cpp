#include "VillageMembershipReplication.h"

#include "Game/NPC/Village/VillageMembershipService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"

#include <Windows.h>

#include <cstdio>

namespace fable::multiplayer::replication
{
    void VillageMembershipReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        game::npc::village::VillageMembershipService& villages,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        villages_ = &villages;
        diagnostics_ = diagnostics;
        initialized_ = localActorId != 0;
        acceptingEvents_.store(initialized_, std::memory_order_release);
        villages_->SetMutationSink(
            &VillageMembershipReplication::CaptureMutation, this);
        diagnostics_.Event(
            "MultiplayerVillageMembershipReady",
            "durable NPC VillageUID mutations are fenced by current map ownership");
    }

    bool VillageMembershipReplication::Process(
        const entities::LiveEntityRegistry& liveEntities)
    {
        if (!initialized_ || authority_ == nullptr || lifecycle_ == nullptr ||
            identities_ == nullptr)
        {
            return false;
        }

        std::deque<game::npc::village::VillageMembershipMutationEvent>
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
                // Native save hydration invokes the same setter before the
                // Thing is registered. It is observation, not a gameplay
                // mutation, and must never replace the host's durable value.
                continue;
            }
            if (!authority_->IsMapPublisher(
                    world->mapName,
                    localActorId_,
                    world->mapEpoch))
            {
                if (now >= event.observedAt &&
                    now - event.observedAt <= AuthorityGraceMilliseconds &&
                    deferred_.size() < EventCapacity)
                {
                    deferred_.push_back(event);
                }
                continue;
            }
            if (!lifecycle_->SubmitVillageMembershipMutation(
                    canonicalUid, event.villageUid))
            {
                return false;
            }

            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX village_uid=%016llX role=%s",
                static_cast<unsigned long long>(canonicalUid),
                static_cast<unsigned long long>(event.villageUid),
                role_ == PeerRole::Host ? "host" : "guest");
            diagnostics_.Event(
                "MultiplayerVillageMembershipPublished", detail);
        }

        const unsigned int dropped =
            droppedEvents_.load(std::memory_order_acquire);
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            diagnostics_.Event(
                "MultiplayerVillageMembershipOverflow",
                "native VillageUID mutations exceeded the bounded queue");
            return false;
        }
        return true;
    }

    void VillageMembershipReplication::CaptureMutation(
        void* context,
        const game::npc::village::VillageMembershipMutationEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<VillageMembershipReplication*>(context)->Enqueue(
                event);
        }
    }

    void VillageMembershipReplication::Enqueue(
        const game::npc::village::VillageMembershipMutationEvent& event)
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

    void VillageMembershipReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (villages_ != nullptr)
        {
            villages_->SetMutationSink(nullptr, nullptr);
        }
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            events_.clear();
        }
        deferred_.clear();
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        villages_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        droppedEvents_.store(0, std::memory_order_release);
        reportedDroppedEvents_ = 0;
        initialized_ = false;
    }
}
