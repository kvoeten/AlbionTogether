#include "CombatActionLedger.h"

namespace fable::multiplayer::combat
{
    namespace
    {
        [[nodiscard]] std::uint64_t Mix(
            std::uint64_t value,
            std::uint64_t input) noexcept
        {
            value ^= input + UINT64_C(0x9e3779b97f4a7c15) +
                (value << 6) + (value >> 2);
            value ^= value >> 30;
            value *= UINT64_C(0xbf58476d1ce4e5b9);
            value ^= value >> 27;
            value *= UINT64_C(0x94d049bb133111eb);
            return value ^ (value >> 31);
        }

        [[nodiscard]] std::uint64_t HashLifecycle(
            const CombatLifecycle& value) noexcept
        {
            std::uint64_t hash = static_cast<std::uint64_t>(value.kind);
            hash = Mix(hash, value.subjectId);
            hash = Mix(hash, value.generation);
            return Mix(hash, value.mapEpoch);
        }

        [[nodiscard]] std::uint64_t HashSourceAction(
            const CombatSourceAction& value) noexcept
        {
            std::uint64_t hash = HashLifecycle(value.source);
            hash = Mix(hash, value.actionId);
            return Mix(hash, value.actionEpoch);
        }

        [[nodiscard]] std::uint64_t HashHitKey(
            const CombatHitKey& value) noexcept
        {
            std::uint64_t hash = HashSourceAction(value.sourceAction);
            hash = Mix(hash, HashLifecycle(value.target));
            return Mix(hash, value.hitOrdinal);
        }
    }

    std::size_t CombatActionLedger::SourceActionHash::operator()(
        const CombatSourceAction& value) const noexcept
    {
        return static_cast<std::size_t>(HashSourceAction(value));
    }

    std::size_t CombatActionLedger::HitKeyHash::operator()(
        const CombatHitKey& value) const noexcept
    {
        return static_cast<std::size_t>(HashHitKey(value));
    }

    std::size_t CombatActionLedger::SubjectHash::operator()(
        const SubjectKey& value) const noexcept
    {
        std::uint64_t hash = static_cast<std::uint64_t>(value.kind);
        return static_cast<std::size_t>(Mix(hash, value.subjectId));
    }

    bool CombatLifecycle::IsValid() const noexcept
    {
        return (kind == CombatSubjectKind::PlayerActor ||
                kind == CombatSubjectKind::WorldEntity) &&
            subjectId != 0 && generation != 0 && mapEpoch != 0;
    }

    bool CombatLifecycle::operator==(const CombatLifecycle& other)
        const noexcept
    {
        return kind == other.kind && subjectId == other.subjectId &&
            generation == other.generation && mapEpoch == other.mapEpoch;
    }

    bool CombatSourceAction::IsValid() const noexcept
    {
        return source.IsValid() && actionId != 0 && actionEpoch != 0;
    }

    bool CombatSourceAction::operator==(const CombatSourceAction& other)
        const noexcept
    {
        return source == other.source && actionId == other.actionId &&
            actionEpoch == other.actionEpoch;
    }

    bool CombatHitKey::IsValid() const noexcept
    {
        return sourceAction.IsValid() && target.IsValid() &&
            hitOrdinal != 0;
    }

    bool CombatHitKey::operator==(const CombatHitKey& other)
        const noexcept
    {
        return sourceAction == other.sourceAction && target == other.target &&
            hitOrdinal == other.hitOrdinal;
    }

    CombatActionLedger::CombatActionLedger()
    {
        currentActions_.reserve(MaxCurrentActions);
        hitKeys_.reserve(MaxHitKeys);
        fences_.reserve(MaxLifecycleFences);
        targetRevisions_.reserve(MaxLifecycleFences);
    }

    CombatActionLedger::SubjectKey CombatActionLedger::ToSubjectKey(
        const CombatLifecycle& lifecycle) noexcept
    {
        return {lifecycle.kind, lifecycle.subjectId};
    }

    bool CombatActionLedger::IsNewer(
        const CombatLifecycle& candidate,
        const CombatLifecycle& current) noexcept
    {
        if (candidate.kind != current.kind ||
            candidate.subjectId != current.subjectId)
        {
            return false;
        }
        if (candidate.mapEpoch != current.mapEpoch)
        {
            return candidate.mapEpoch > current.mapEpoch;
        }
        return candidate.generation > current.generation;
    }

    bool CombatActionLedger::AcceptLifecycle(
        const CombatLifecycle& lifecycle) noexcept
    {
        if (!lifecycle.IsValid())
        {
            return false;
        }

        const SubjectKey key = ToSubjectKey(lifecycle);
        const auto found = fences_.find(key);
        if (found != fences_.end())
        {
            if (found->second.lifecycle == lifecycle)
            {
                if (found->second.retired)
                {
                    return false;
                }
                found->second.serial = nextSerial_++;
                return true;
            }
            if (!IsNewer(lifecycle, found->second.lifecycle))
            {
                return false;
            }
            RemoveLifecycleRecords(found->second.lifecycle, true);
            found->second.lifecycle = lifecycle;
            found->second.serial = nextSerial_++;
            found->second.retired = false;
            targetRevisions_.erase(key);
            return true;
        }

        if (fences_.size() >= MaxLifecycleFences)
        {
            EvictOldestFence();
        }
        fences_.emplace(key, FenceRecord{lifecycle, nextSerial_++, false});
        return true;
    }

    bool CombatActionLedger::IsLifecycleAccepted(
        const CombatLifecycle& lifecycle) const noexcept
    {
        if (!lifecycle.IsValid())
        {
            return false;
        }
        const auto found = fences_.find(ToSubjectKey(lifecycle));
        return found == fences_.end() ||
            (found->second.lifecycle == lifecycle && !found->second.retired);
    }

    bool CombatActionLedger::Begin(
        const CombatSourceAction& action,
        const std::uint64_t observedAt)
    {
        if (!action.IsValid() || !AcceptLifecycle(action.source))
        {
            return false;
        }

        const auto existing = currentActions_.find(action);
        if (existing != currentActions_.end())
        {
            if (existing->second.finished)
            {
                return false;
            }
            existing->second.serial = nextSerial_++;
            if (existing->second.beganAt == 0)
            {
                existing->second.beganAt = observedAt;
            }
            return true;
        }

        if (currentActions_.size() >= MaxCurrentActions)
        {
            EvictOldestAction();
        }
        currentActions_.emplace(
            action, ActionRecord{false, nextSerial_++, observedAt, 0});
        return true;
    }

    bool CombatActionLedger::Touch(const CombatSourceAction& action) noexcept
    {
        const auto found = currentActions_.find(action);
        if (found == currentActions_.end() || found->second.finished ||
            !IsLifecycleAccepted(action.source))
        {
            return false;
        }
        found->second.serial = nextSerial_++;
        return true;
    }

    bool CombatActionLedger::Finish(
        const CombatSourceAction& action,
        const std::uint64_t observedAt) noexcept
    {
        const auto found = currentActions_.find(action);
        if (found == currentActions_.end() || found->second.finished)
        {
            return false;
        }
        found->second.finished = true;
        found->second.serial = nextSerial_++;
        found->second.finishedAt = observedAt;
        return true;
    }

    bool CombatActionLedger::IsCurrent(
        const CombatSourceAction& action) const noexcept
    {
        const auto found = currentActions_.find(action);
        return found != currentActions_.end() && !found->second.finished &&
            IsLifecycleAccepted(action.source);
    }

    bool CombatActionLedger::IsKnown(
        const CombatSourceAction& action) const noexcept
    {
        return currentActions_.find(action) != currentActions_.end() &&
            IsLifecycleAccepted(action.source);
    }

    bool CombatActionLedger::FindLatest(
        const CombatLifecycle& source,
        CombatSourceAction& action) const noexcept
    {
        action = {};
        if (!source.IsValid() || !IsLifecycleAccepted(source))
        {
            return false;
        }
        std::uint64_t latestSerial = 0;
        for (const auto& entry : currentActions_)
        {
            if (entry.first.source == source &&
                entry.second.serial > latestSerial)
            {
                action = entry.first;
                latestSerial = entry.second.serial;
            }
        }
        return latestSerial != 0;
    }

    bool CombatActionLedger::FindAt(
        const CombatLifecycle& source,
        const std::uint64_t observedAt,
        CombatSourceAction& action) const noexcept
    {
        action = {};
        if (!source.IsValid() || observedAt == 0 ||
            !IsLifecycleAccepted(source))
        {
            return false;
        }
        constexpr std::uint64_t FinishGraceMilliseconds = 250;
        std::uint64_t latestStart = 0;
        std::uint64_t latestSerial = 0;
        for (const auto& entry : currentActions_)
        {
            const ActionRecord& record = entry.second;
            if (entry.first.source != source || record.beganAt == 0 ||
                record.beganAt > observedAt ||
                (record.finishedAt != 0 &&
                    observedAt > record.finishedAt + FinishGraceMilliseconds))
            {
                continue;
            }
            if (record.beganAt > latestStart ||
                (record.beganAt == latestStart &&
                    record.serial > latestSerial))
            {
                action = entry.first;
                latestStart = record.beganAt;
                latestSerial = record.serial;
            }
        }
        return latestSerial != 0;
    }

    CombatHitAdmission CombatActionLedger::RecordHit(
        const CombatSourceAction& action,
        const CombatLifecycle& target,
        std::uint32_t hitOrdinal,
        std::uint64_t& resultRevision)
    {
        resultRevision = 0;
        if (!action.IsValid() || !target.IsValid() || hitOrdinal == 0)
        {
            return CombatHitAdmission::Invalid;
        }
        if (!IsKnown(action))
        {
            return CombatHitAdmission::UnknownAction;
        }
        if (!AcceptLifecycle(target))
        {
            return CombatHitAdmission::StaleLifecycle;
        }

        const CombatHitKey key{action, target, hitOrdinal};
        const auto existing = hitKeys_.find(key);
        if (existing != hitKeys_.end())
        {
            resultRevision = existing->second.resultRevision;
            existing->second.serial = nextSerial_++;
            return CombatHitAdmission::Duplicate;
        }

        if (hitKeys_.size() >= MaxHitKeys)
        {
            EvictOldestHit();
        }
        const SubjectKey targetKey = ToSubjectKey(target);
        auto revision = targetRevisions_.find(targetKey);
        if (revision == targetRevisions_.end())
        {
            if (targetRevisions_.size() >= MaxLifecycleFences)
            {
                // Revision metadata is bounded just like fences.  The target
                // can only restart its counter after its lifecycle record has
                // naturally fallen out of this bounded ledger.
                std::uint64_t oldestSerial = UINT64_MAX;
                SubjectKey oldestKey{};
                bool foundOldest = false;
                for (const auto& entry : targetRevisions_)
                {
                    if (entry.second.serial < oldestSerial)
                    {
                        oldestSerial = entry.second.serial;
                        oldestKey = entry.first;
                        foundOldest = true;
                    }
                }
                if (foundOldest)
                {
                    targetRevisions_.erase(oldestKey);
                }
            }
            revision = targetRevisions_.emplace(
                targetKey,
                TargetRevisionRecord{}).first;
        }
        ++revision->second.revision;
        if (revision->second.revision == 0)
        {
            revision->second.revision = 1;
        }
        revision->second.serial = nextSerial_++;
        resultRevision = revision->second.revision;
        hitKeys_.emplace(key, HitRecord{resultRevision, nextSerial_++});
        return CombatHitAdmission::Accepted;
    }

    bool CombatActionLedger::HasHit(const CombatHitKey& key) const noexcept
    {
        return key.IsValid() && hitKeys_.find(key) != hitKeys_.end();
    }

    std::uint64_t CombatActionLedger::TargetRevision(
        const CombatLifecycle& target) const noexcept
    {
        if (!target.IsValid() || !IsLifecycleAccepted(target))
        {
            return 0;
        }
        const auto found = targetRevisions_.find(ToSubjectKey(target));
        return found != targetRevisions_.end()
            ? found->second.revision
            : 0;
    }

    void CombatActionLedger::RemoveHitsForSource(
        const CombatSourceAction& action) noexcept
    {
        for (auto iterator = hitKeys_.begin(); iterator != hitKeys_.end();)
        {
            if (iterator->first.sourceAction == action)
            {
                iterator = hitKeys_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void CombatActionLedger::RemoveHitsForLifecycle(
        const CombatLifecycle& lifecycle) noexcept
    {
        const SubjectKey subject = ToSubjectKey(lifecycle);
        for (auto iterator = hitKeys_.begin(); iterator != hitKeys_.end();)
        {
            const CombatHitKey& key = iterator->first;
            if ((key.sourceAction.source.kind == subject.kind &&
                 key.sourceAction.source.subjectId == subject.subjectId &&
                 key.sourceAction.source == lifecycle) ||
                (key.target.kind == subject.kind &&
                 key.target.subjectId == subject.subjectId &&
                 key.target == lifecycle))
            {
                iterator = hitKeys_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void CombatActionLedger::RemoveSourceRecords(
        const CombatSourceAction& action) noexcept
    {
        currentActions_.erase(action);
        RemoveHitsForSource(action);
    }

    void CombatActionLedger::RemoveLifecycleRecords(
        const CombatLifecycle& lifecycle,
        bool exact) noexcept
    {
        for (auto iterator = currentActions_.begin();
             iterator != currentActions_.end();)
        {
            const CombatLifecycle& source = iterator->first.source;
            const bool sameSubject = source.kind == lifecycle.kind &&
                source.subjectId == lifecycle.subjectId;
            if (sameSubject && ((!exact && source != lifecycle) ||
                                (exact && source == lifecycle)))
            {
                RemoveHitsForSource(iterator->first);
                iterator = currentActions_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        for (auto iterator = hitKeys_.begin(); iterator != hitKeys_.end();)
        {
            const CombatHitKey& key = iterator->first;
            const bool sourceSubject =
                key.sourceAction.source.kind == lifecycle.kind &&
                key.sourceAction.source.subjectId == lifecycle.subjectId;
            const bool targetSubject = key.target.kind == lifecycle.kind &&
                key.target.subjectId == lifecycle.subjectId;
            const bool sourceMatch = sourceSubject &&
                key.sourceAction.source == lifecycle;
            const bool targetMatch = targetSubject && key.target == lifecycle;
            const bool sourceRemove = sourceSubject &&
                ((exact && sourceMatch) || (!exact && !sourceMatch));
            const bool targetRemove = targetSubject &&
                ((exact && targetMatch) || (!exact && !targetMatch));
            if (sourceRemove || targetRemove)
            {
                iterator = hitKeys_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void CombatActionLedger::EvictOldestAction() noexcept
    {
        auto oldest = currentActions_.end();
        for (auto iterator = currentActions_.begin();
             iterator != currentActions_.end(); ++iterator)
        {
            if (oldest == currentActions_.end() ||
                iterator->second.serial < oldest->second.serial)
            {
                oldest = iterator;
            }
        }
        if (oldest != currentActions_.end())
        {
            RemoveHitsForSource(oldest->first);
            currentActions_.erase(oldest);
        }
    }

    void CombatActionLedger::EvictOldestHit() noexcept
    {
        auto oldest = hitKeys_.end();
        for (auto iterator = hitKeys_.begin(); iterator != hitKeys_.end();
             ++iterator)
        {
            if (oldest == hitKeys_.end() ||
                iterator->second.serial < oldest->second.serial)
            {
                oldest = iterator;
            }
        }
        if (oldest != hitKeys_.end())
        {
            hitKeys_.erase(oldest);
        }
    }

    void CombatActionLedger::EvictOldestFence() noexcept
    {
        auto oldest = fences_.end();
        for (auto iterator = fences_.begin(); iterator != fences_.end();
             ++iterator)
        {
            if (oldest == fences_.end() ||
                iterator->second.serial < oldest->second.serial)
            {
                oldest = iterator;
            }
        }
        if (oldest != fences_.end())
        {
            RemoveLifecycleRecords(oldest->second.lifecycle, true);
            targetRevisions_.erase(oldest->first);
            fences_.erase(oldest);
        }
    }

    bool CombatActionLedger::FenceLifecycle(
        const CombatLifecycle& lifecycle) noexcept
    {
        if (!lifecycle.IsValid())
        {
            return false;
        }
        const SubjectKey key = ToSubjectKey(lifecycle);
        const auto found = fences_.find(key);
        if (found != fences_.end() && found->second.lifecycle == lifecycle)
        {
            // An explicit fence call is also the lifecycle activation point.
            // This permits a caller to materialize a lifecycle after first
            // clearing its old presentation, while ordinary packet paths
            // still reject the retired exact identity.
            found->second.retired = false;
            found->second.serial = nextSerial_++;
            return true;
        }
        if (!AcceptLifecycle(lifecycle))
        {
            return false;
        }
        RemoveLifecycleRecords(lifecycle, false);
        return true;
    }

    void CombatActionLedger::ClearSource(
        const CombatSourceAction& action) noexcept
    {
        if (action.IsValid())
        {
            RemoveSourceRecords(action);
        }
    }

    void CombatActionLedger::ClearLifecycle(
        const CombatLifecycle& lifecycle) noexcept
    {
        if (!lifecycle.IsValid())
        {
            return;
        }
        if (AcceptLifecycle(lifecycle))
        {
            RemoveLifecycleRecords(lifecycle, true);
            const SubjectKey key = ToSubjectKey(lifecycle);
            auto found = fences_.find(key);
            if (found != fences_.end() && found->second.lifecycle == lifecycle)
            {
                found->second.retired = true;
                found->second.serial = nextSerial_++;
                targetRevisions_.erase(key);
            }
        }
    }

    bool CombatActionLedger::FenceActor(
        std::uint64_t actorId,
        std::uint32_t generation,
        std::uint32_t mapEpoch) noexcept
    {
        return FenceLifecycle({CombatSubjectKind::PlayerActor, actorId,
                               generation, mapEpoch});
    }

    bool CombatActionLedger::FenceEntity(
        std::uint64_t entityUid,
        std::uint32_t generation,
        std::uint32_t mapEpoch) noexcept
    {
        return FenceLifecycle({CombatSubjectKind::WorldEntity, entityUid,
                               generation, mapEpoch});
    }

    void CombatActionLedger::ClearActor(
        std::uint64_t actorId,
        std::uint32_t generation,
        std::uint32_t mapEpoch) noexcept
    {
        ClearLifecycle({CombatSubjectKind::PlayerActor, actorId, generation,
                        mapEpoch});
    }

    void CombatActionLedger::ClearEntity(
        std::uint64_t entityUid,
        std::uint32_t generation,
        std::uint32_t mapEpoch) noexcept
    {
        ClearLifecycle({CombatSubjectKind::WorldEntity, entityUid, generation,
                        mapEpoch});
    }

    void CombatActionLedger::Clear() noexcept
    {
        currentActions_.clear();
        hitKeys_.clear();
        fences_.clear();
        targetRevisions_.clear();
        nextSerial_ = 1;
    }

    std::size_t CombatActionLedger::CurrentActionCount() const noexcept
    {
        return currentActions_.size();
    }

    std::size_t CombatActionLedger::HitCount() const noexcept
    {
        return hitKeys_.size();
    }

    std::size_t CombatActionLedger::FenceCount() const noexcept
    {
        return fences_.size();
    }
}
