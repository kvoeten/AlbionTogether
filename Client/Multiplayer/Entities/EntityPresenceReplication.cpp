#include "EntityPresenceReplication.h"

#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"

#include <cstdio>
#include <utility>

namespace fable::multiplayer::entities
{
    void EntityPresenceReplication::Initialize(
        EntityNetworkIdentityRegistry& identities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        identities_ = &identities;
        diagnostics_ = diagnostics;
        initialized_ = true;
        acceptingEvents_.store(true, std::memory_order_release);
    }

    bool EntityPresenceReplication::Attach(
        game::entity::presence::ThingPresenceObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled())
        {
            return false;
        }
        if (observer_ != nullptr && observer_ != &observer)
        {
            observer_->SetEventSink(nullptr, nullptr);
        }
        observer_ = &observer;
        observer_->SetEventSink(
            &EntityPresenceReplication::CaptureEvent,
            this);
        diagnostics_.Event(
            "MultiplayerThingPresenceAttached",
            "bounded native presence queue is active");
        return true;
    }

    bool EntityPresenceReplication::ProcessPending()
    {
        if (!initialized_)
        {
            return false;
        }

        std::deque<game::entity::presence::ThingPresenceEvent> pending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending.swap(pendingEvents_);
        }

        for (const auto& event : pending)
        {
            game::entity::presence::ThingPresenceEvent canonical = event;
            if (identities_ != nullptr)
            {
                canonical.thingUid = identities_->CanonicalizeLocalObservation(
                    event.thingUid);
            }
            if (canonical.thingUid == 0)
            {
                ++identityCollisionCount_;
                if (identityCollisionCount_ <= DiagnosticChangeLimit)
                {
                    char detail[224] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "local_uid=%016llX thing=%p phase=%u ignored=true",
                        static_cast<unsigned long long>(event.thingUid),
                        event.thing,
                        static_cast<unsigned int>(event.phase));
                    diagnostics_.Event(
                        "MultiplayerEntityLocalUidCollisionIgnored",
                        detail);
                }
                continue;
            }
            LiveEntityChange change;
            const bool applied = liveEntities_.Apply(canonical, change);
            if (event.phase == game::entity::presence::ThingPresencePhase::
                    Unregistered && identities_ != nullptr)
            {
                const auto retirement = pendingIdentityRetirements_.find(
                    event.thingUid);
                if (retirement != pendingIdentityRetirements_.end() &&
                    retirement->second == canonical.thingUid)
                {
                    identities_->ForgetLocal(event.thingUid);
                    pendingIdentityRetirements_.erase(retirement);
                }
                else if (event.destroyed)
                {
                    identities_->ForgetLocal(event.thingUid);
                }
            }
            if (!applied)
            {
                continue;
            }
            const bool replicable =
                LiveEntityRegistry::IsReplicable(change.record);
            if (replicable)
            {
                TrackChange(change);
            }
            else
            {
                ++filteredTransientCount_;
                continue;
            }

            ++diagnosticChangeCount_;
            if (diagnosticChangeCount_ > DiagnosticChangeLimit)
            {
                continue;
            }
            char detail[384] = {};
            const char* kind = "none";
            switch (change.kind)
            {
            case LiveEntityChangeKind::Registered:
                kind = "registered";
                break;
            case LiveEntityChangeKind::Rebound:
                kind = "rebound";
                break;
            case LiveEntityChangeKind::Unregistered:
                kind = "unregistered";
                break;
            default:
                break;
            }
            std::snprintf(
                detail,
                sizeof(detail),
                "kind=%s thing_uid=%016llX local_incarnation=%u map_id=%u definition_index=%u script_name=%s persistent=%s creature=%s has_hero_morph=%s replicable=%s live_count=%zu",
                kind,
                static_cast<unsigned long long>(change.record.thingUid),
                change.record.localIncarnation,
                static_cast<unsigned int>(change.record.mapId),
                static_cast<unsigned int>(change.record.definitionIndex),
                change.record.scriptName.c_str(),
                change.record.gamePersistent || change.record.levelPersistent
                    ? "true"
                    : "false",
                change.record.creature ? "true" : "false",
                change.record.hasHeroMorph ? "true" : "false",
                "true",
                liveEntities_.Size());
            diagnostics_.Event("MultiplayerLiveEntityChanged", detail);

        }

        if (filteredTransientCount_ != reportedFilteredTransientCount_ &&
            (reportedFilteredTransientCount_ == 0 ||
                filteredTransientCount_ - reportedFilteredTransientCount_ >=
                    256))
        {
            reportedFilteredTransientCount_ = filteredTransientCount_;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "filtered_total=%u reason=transient-non-creature-without-persistent-or-script-identity",
                filteredTransientCount_);
            diagnostics_.Event(
                "MultiplayerTransientEntityFiltered", detail);
        }

        const unsigned int dropped =
            droppedEvents_.load(std::memory_order_acquire);
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "dropped=%u capacity=%zu; a host baseline is required before publishing presence",
                dropped,
                PendingEventCapacity);
            diagnostics_.Event("MultiplayerThingPresenceOverflow", detail);
            baselineRequired_ = true;
            return false;
        }
        return true;
    }

    bool EntityPresenceReplication::RetireNetworkIdentity(
        std::uint64_t canonicalUid,
        std::uint64_t localUid) noexcept
    {
        if (!initialized_ || identities_ == nullptr || canonicalUid == 0 ||
            localUid == 0 || identities_->FindLocal(canonicalUid) != localUid)
        {
            return false;
        }

        if (liveEntities_.Find(canonicalUid) == nullptr)
        {
            identities_->ForgetLocal(localUid);
            return true;
        }
        const auto existing = pendingIdentityRetirements_.find(localUid);
        if (existing != pendingIdentityRetirements_.end())
        {
            return existing->second == canonicalUid;
        }
        if (pendingIdentityRetirements_.size() >= PendingEventCapacity)
        {
            diagnostics_.Event(
                "MultiplayerEntityIdentityRetirementOverflow",
                "bounded presentation-alias retirement table is full");
            return false;
        }
        pendingIdentityRetirements_.emplace(localUid, canonicalUid);
        return true;
    }

    void EntityPresenceReplication::TakeChanges(
        std::vector<LiveEntityChange>& changes,
        bool& baselineRequired)
    {
        changes.clear();
        changes.reserve(pendingChanges_.size());
        while (!pendingChanges_.empty())
        {
            changes.push_back(std::move(pendingChanges_.front()));
            pendingChanges_.pop_front();
        }
        baselineRequired = baselineRequired_;
        baselineRequired_ = false;
    }

    bool EntityPresenceReplication::BindNetworkIdentity(
        std::uint64_t canonicalUid,
        std::uint64_t localUid)
    {
        if (!initialized_ || identities_ == nullptr)
        {
            return false;
        }
        if (canonicalUid != 0 && canonicalUid == localUid)
        {
            return identities_->Bind(canonicalUid, localUid);
        }
        if (!identities_->Bind(canonicalUid, localUid) ||
            !liveEntities_.Remap(localUid, canonicalUid))
        {
            if (identities_ != nullptr)
            {
                identities_->ForgetLocal(localUid);
            }
            return false;
        }

        for (LiveEntityChange& change : pendingChanges_)
        {
            if (change.record.thingUid == localUid)
            {
                change.record.thingUid = canonicalUid;
            }
        }
        return true;
    }

    bool EntityPresenceReplication::UnregisterLocalPresence(
        std::uint64_t canonicalUid) noexcept
    {
        if (!initialized_ || observer_ == nullptr || canonicalUid == 0)
        {
            return false;
        }
        const LiveEntityRecord* const live =
            liveEntities_.Find(canonicalUid);
        return live != nullptr && live->mapwhoComponent != nullptr &&
            observer_->RequestUnregister(live->mapwhoComponent);
    }

    const LiveEntityRegistry& EntityPresenceReplication::LiveEntities()
        const noexcept
    {
        return liveEntities_;
    }

    void EntityPresenceReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (observer_ != nullptr)
        {
            observer_->SetEventSink(nullptr, nullptr);
        }
        observer_ = nullptr;
        identities_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pendingEvents_.clear();
        }
        pendingChanges_.clear();
        pendingIdentityRetirements_.clear();
        liveEntities_.Clear();
        diagnostics_ = {};
        droppedEvents_.store(0, std::memory_order_release);
        reportedDroppedEvents_ = 0;
        diagnosticChangeCount_ = 0;
        filteredTransientCount_ = 0;
        reportedFilteredTransientCount_ = 0;
        identityCollisionCount_ = 0;
        baselineRequired_ = true;
        initialized_ = false;
    }

    void EntityPresenceReplication::CaptureEvent(
        void* context,
        const game::entity::presence::ThingPresenceEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityPresenceReplication*>(context)->Enqueue(event);
        }
    }

    void EntityPresenceReplication::Enqueue(
        const game::entity::presence::ThingPresenceEvent& event) noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (pendingEvents_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        pendingEvents_.push_back(event);
    }

    void EntityPresenceReplication::TrackChange(
        const LiveEntityChange& change)
    {
        if (pendingChanges_.size() >= PendingEventCapacity)
        {
            baselineRequired_ = true;
            return;
        }
        pendingChanges_.push_back(change);
    }
}
