#include "LiveEntityRegistry.h"

#include <utility>

namespace fable::multiplayer::entities
{
    bool LiveEntityRegistry::Apply(
        const game::entity::presence::ThingPresenceEvent& event,
        LiveEntityChange& change)
    {
        change = {};
        if (event.thingUid == 0 || event.thing == nullptr)
        {
            return false;
        }

        auto existing = records_.find(event.thingUid);
        if (event.phase ==
            game::entity::presence::ThingPresencePhase::Registered)
        {
            const bool wasAbsent = existing == records_.end();
            if (existing != records_.end() &&
                existing->second.thing == event.thing)
            {
                existing->second.mapId = event.mapId;
                existing->second.villageUid = event.villageUid;
                existing->second.definitionIndex = event.definitionIndex;
                existing->second.scriptName = event.scriptName.data();
                existing->second.position = event.position;
                existing->second.facing = event.facing;
                existing->second.hasTransform = event.hasTransform;
                existing->second.gamePersistent = event.gamePersistent;
                existing->second.levelPersistent = event.levelPersistent;
                existing->second.creature = event.creature;
                existing->second.hasHeroMorph = event.hasHeroMorph;
                existing->second.hasVillageMembership =
                    event.hasVillageMembership;
                existing->second.summonedCreature =
                    existing->second.summonedCreature ||
                    event.summonedCreature;
                existing->second.abilityOwnedTransient =
                    existing->second.abilityOwnedTransient ||
                    event.abilityOwnedTransient;
                existing->second.mapwhoComponent = event.component;
                change.kind = LiveEntityChangeKind::Rebound;
                change.record = existing->second;
                MarkChanged();
                return true;
            }

            LiveEntityRecord record;
            record.thingUid = event.thingUid;
            record.villageUid = event.villageUid;
            record.localIncarnation = NextIncarnation();
            record.mapId = event.mapId;
            record.definitionIndex = event.definitionIndex;
            record.scriptName = event.scriptName.data();
            record.position = event.position;
            record.facing = event.facing;
            record.hasTransform = event.hasTransform;
            record.gamePersistent = event.gamePersistent;
            record.levelPersistent = event.levelPersistent;
            record.creature = event.creature;
            record.hasHeroMorph = event.hasHeroMorph;
            record.hasVillageMembership = event.hasVillageMembership;
            record.summonedCreature = event.summonedCreature;
            record.abilityOwnedTransient = event.abilityOwnedTransient;
            record.thing = event.thing;
            record.mapwhoComponent = event.component;
            records_[event.thingUid] = record;
            change.kind = wasAbsent
                ? LiveEntityChangeKind::Registered
                : LiveEntityChangeKind::Rebound;
            change.record = record;
            MarkChanged();
            return true;
        }

        if (existing == records_.end() ||
            existing->second.thing != event.thing)
        {
            return false;
        }
        // The retail crossing path can assign CThing+0x9A to the destination
        // immediately before CTCMapwho unregisters the source incarnation.
        // Preserve that final synchronous context instead of returning the
        // older registration snapshot.
        if (event.mapId != 0)
        {
            existing->second.mapId = event.mapId;
        }
        existing->second.definitionIndex = event.definitionIndex;
        existing->second.villageUid = event.villageUid;
        existing->second.scriptName = event.scriptName.data();
        if (event.hasTransform)
        {
            existing->second.position = event.position;
            existing->second.facing = event.facing;
            existing->second.hasTransform = true;
        }
        existing->second.gamePersistent = event.gamePersistent;
        existing->second.levelPersistent = event.levelPersistent;
        existing->second.creature = event.creature;
        existing->second.hasHeroMorph = event.hasHeroMorph;
        existing->second.hasVillageMembership = event.hasVillageMembership;
        existing->second.summonedCreature =
            existing->second.summonedCreature || event.summonedCreature;
        existing->second.abilityOwnedTransient =
            existing->second.abilityOwnedTransient ||
            event.abilityOwnedTransient;
        existing->second.mapwhoComponent = event.component;
        change.kind = LiveEntityChangeKind::Unregistered;
        change.record = existing->second;
        records_.erase(existing);
        MarkChanged();
        return true;
    }

    const LiveEntityRecord* LiveEntityRegistry::Find(
        std::uint64_t thingUid) const noexcept
    {
        const auto match = records_.find(thingUid);
        return match != records_.end() ? &match->second : nullptr;
    }

    std::uint64_t LiveEntityRegistry::Revision() const noexcept
    {
        return revision_;
    }

    bool LiveEntityRegistry::Remap(
        std::uint64_t localUid,
        std::uint64_t canonicalUid) noexcept
    {
        if (localUid == 0 || canonicalUid == 0)
        {
            return false;
        }
        if (localUid == canonicalUid)
        {
            return true;
        }
        const auto local = records_.find(localUid);
        if (local == records_.end())
        {
            // The alias may be installed synchronously while CreateCreature's
            // native registration event is still waiting in the bridge queue.
            return true;
        }
        if (records_.find(canonicalUid) != records_.end())
        {
            return false;
        }
        LiveEntityRecord record = std::move(local->second);
        records_.erase(local);
        record.thingUid = canonicalUid;
        records_.emplace(canonicalUid, std::move(record));
        MarkChanged();
        return true;
    }

    std::vector<LiveEntityRecord> LiveEntityRegistry::Snapshot() const
    {
        std::vector<LiveEntityRecord> result;
        result.reserve(records_.size());
        for (const auto& entry : records_)
        {
            result.push_back(entry.second);
        }
        return result;
    }

    std::size_t LiveEntityRegistry::Size() const noexcept
    {
        return records_.size();
    }

    bool LiveEntityRegistry::IsReplicable(
        const LiveEntityRecord& record) noexcept
    {
        const bool networkIdentityBearing = record.creature ||
            record.gamePersistent || record.levelPersistent ||
            record.hasVillageMembership || !record.scriptName.empty();
        return record.thingUid != 0 && record.thing != nullptr &&
            networkIdentityBearing && !record.summonedCreature &&
            !record.abilityOwnedTransient &&
            record.scriptName != "HeroSummonnedCreature" &&
            record.scriptName != "HeroSummonedCreature" &&
            !IsPlayerPresentation(record);
    }

    bool LiveEntityRegistry::IsPlayerPresentation(
        const LiveEntityRecord& record) noexcept
    {
        return record.scriptName == "SCRIPT_NAME_HERO" ||
            record.scriptName == "SCRIPT_NAME_FABLE_TOGETHER_REMOTE_PLAYER";
    }

    void LiveEntityRegistry::Clear() noexcept
    {
        if (!records_.empty())
        {
            MarkChanged();
        }
        records_.clear();
        nextIncarnation_ = 0;
    }

    std::uint32_t LiveEntityRegistry::NextIncarnation() noexcept
    {
        ++nextIncarnation_;
        if (nextIncarnation_ == 0)
        {
            ++nextIncarnation_;
        }
        return nextIncarnation_;
    }

    void LiveEntityRegistry::MarkChanged() noexcept
    {
        ++revision_;
        if (revision_ == 0)
        {
            ++revision_;
        }
    }
}
