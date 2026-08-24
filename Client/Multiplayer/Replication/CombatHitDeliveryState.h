#pragma once

#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Protocol/CombatHitMessage.h"
#include "Multiplayer/Transport/ReliableStream.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fable::multiplayer::replication
{
    enum class CombatHitPublicationAttempt : std::uint8_t
    {
        Submitted,
        Deferred,
        Failed,
    };

    class CombatHitPublicationQueue final
    {
    public:
        static constexpr std::size_t Capacity = 2048;

        struct Reservation final
        {
            std::uint64_t token = 0;
        };

        [[nodiscard]] bool TryReserve(
            const protocol::CombatHitMessage& message,
            Reservation& reservation)
        {
            reservation = {};
            if (entries_.size() >= Capacity)
            {
                return false;
            }
            ++nextToken_;
            if (nextToken_ == 0)
            {
                ++nextToken_;
            }
            entries_.push_back({message, nextToken_, false});
            reservation.token = nextToken_;
            return true;
        }

        [[nodiscard]] bool Commit(
            const Reservation reservation,
            protocol::CombatHitMessage message) noexcept
        {
            Entry* const entry = Find(reservation);
            if (entry == nullptr || entry->committed)
            {
                return false;
            }
            entry->message = std::move(message);
            entry->committed = true;
            return true;
        }

        void Cancel(const Reservation reservation) noexcept
        {
            for (auto current = entries_.begin(); current != entries_.end();
                 ++current)
            {
                if (current->token == reservation.token &&
                    !current->committed)
                {
                    entries_.erase(current);
                    return;
                }
            }
        }

        [[nodiscard]] bool Enqueue(protocol::CombatHitMessage message)
        {
            Reservation reservation;
            return TryReserve(message, reservation) &&
                Commit(reservation, std::move(message));
        }

        template <typename Submit>
        [[nodiscard]] bool DrainRound(Submit&& submit, bool& deferred)
        {
            deferred = false;
            const std::size_t scheduled = entries_.size();
            std::unordered_set<ReliableStreamId> attemptedStreams;
            for (std::size_t attempt = 0;
                 attempt < scheduled && !entries_.empty(); ++attempt)
            {
                Entry& entry = entries_.front();
                if (!entry.committed)
                {
                    return false;
                }
                const ReliableStreamId stream = TargetStream(entry.message);
                if (!attemptedStreams.insert(stream).second)
                {
                    entries_.push_back(std::move(entry));
                    entries_.pop_front();
                    continue;
                }
                const CombatHitPublicationAttempt result = submit(
                    entry.message, stream);
                if (result == CombatHitPublicationAttempt::Failed)
                {
                    return false;
                }
                if (result == CombatHitPublicationAttempt::Deferred)
                {
                    deferred = true;
                    entries_.push_back(std::move(entry));
                    entries_.pop_front();
                    continue;
                }
                entries_.pop_front();
            }
            return true;
        }

        [[nodiscard]] bool Full() const noexcept
        {
            return entries_.size() >= Capacity;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return entries_.size();
        }

        void Clear() noexcept
        {
            entries_.clear();
            nextToken_ = 0;
        }

        [[nodiscard]] static ReliableStreamId TargetStream(
            const protocol::CombatHitMessage& message) noexcept
        {
            return message.targetKind ==
                    protocol::CombatParticipantKind::Player
                ? reliable_stream::Actor(message.targetId)
                : reliable_stream::Entity(message.targetId);
        }

    private:
        struct Entry final
        {
            protocol::CombatHitMessage message;
            std::uint64_t token = 0;
            bool committed = false;
        };

        [[nodiscard]] Entry* Find(const Reservation reservation) noexcept
        {
            if (reservation.token == 0)
            {
                return nullptr;
            }
            for (Entry& entry : entries_)
            {
                if (entry.token == reservation.token)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        std::deque<Entry> entries_;
        std::uint64_t nextToken_ = 0;
    };

    class CombatHitResultRevisionCache final
    {
    public:
        static constexpr std::size_t Capacity = 512;

        [[nodiscard]] bool CanApply(
            const protocol::CombatHitMessage& result,
            std::uint64_t authorityConnectionNonce) const noexcept
        {
            const Key key = ToKey(result, authorityConnectionNonce);
            const auto found = revisions_.find(key);
            return found == revisions_.end() ||
                result.hostTargetRevision > found->second.revision;
        }

        void MarkApplied(
            const protocol::CombatHitMessage& result,
            std::uint64_t authorityConnectionNonce) noexcept
        {
            const Key key = ToKey(result, authorityConnectionNonce);
            auto found = revisions_.find(key);
            if (found == revisions_.end())
            {
                if (revisions_.size() >= Capacity)
                {
                    EvictOldest();
                }
                found = revisions_.emplace(key, Record{}).first;
            }
            found->second.revision = result.hostTargetRevision;
            found->second.serial = ++nextSerial_;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return revisions_.size();
        }

        void Clear() noexcept
        {
            revisions_.clear();
            nextSerial_ = 0;
        }

    private:
        struct Key final
        {
            combat::CombatLifecycle lifecycle;
            std::uint32_t authorityEpoch = 0;
            std::uint64_t authorityConnectionNonce = 0;

            [[nodiscard]] bool operator==(const Key& other) const noexcept
            {
                return lifecycle == other.lifecycle &&
                    authorityEpoch == other.authorityEpoch &&
                    authorityConnectionNonce ==
                        other.authorityConnectionNonce;
            }
        };

        struct Hash final
        {
            [[nodiscard]] std::size_t operator()(const Key& key)
                const noexcept
            {
                std::size_t hash = static_cast<std::size_t>(
                    key.lifecycle.kind);
                const auto mix = [&hash](const std::uint64_t value) noexcept
                {
                    hash ^= static_cast<std::size_t>(value) +
                        static_cast<std::size_t>(0x9E3779B9u) +
                        (hash << 6) + (hash >> 2);
                };
                mix(key.lifecycle.subjectId);
                mix(key.lifecycle.generation);
                mix(key.lifecycle.mapEpoch);
                mix(key.authorityEpoch);
                mix(key.authorityConnectionNonce);
                return hash;
            }
        };

        struct Record final
        {
            std::uint64_t revision = 0;
            std::uint64_t serial = 0;
        };

        [[nodiscard]] static Key ToKey(
            const protocol::CombatHitMessage& result,
            const std::uint64_t authorityConnectionNonce) noexcept
        {
            return {
                {result.targetKind ==
                        protocol::CombatParticipantKind::Player
                     ? combat::CombatSubjectKind::PlayerActor
                     : combat::CombatSubjectKind::WorldEntity,
                 result.targetId,
                 result.targetGeneration,
                 result.targetMapEpoch},
                result.targetKind == protocol::CombatParticipantKind::Player
                    ? result.targetAuthorityEpoch
                    : 0,
                authorityConnectionNonce};
        }

        void EvictOldest() noexcept
        {
            auto oldest = revisions_.end();
            for (auto current = revisions_.begin();
                 current != revisions_.end(); ++current)
            {
                if (oldest == revisions_.end() ||
                    current->second.serial < oldest->second.serial)
                {
                    oldest = current;
                }
            }
            if (oldest != revisions_.end())
            {
                revisions_.erase(oldest);
            }
        }

        std::unordered_map<Key, Record, Hash> revisions_;
        std::uint64_t nextSerial_ = 0;
    };

    // Native OnHit can run locally before the authoritative CombatHit result
    // arrives. Keep only a short, bounded correlation window so the result
    // does not replay the same victim reaction a second time.
    struct CombatHitObservation final
    {
        combat::CombatLifecycle source;
        combat::CombatLifecycle target;
        std::uint8_t impactFlags = 0;
        bool nativeReactionExpected = false;
        game::Vector3 impactPosition = {};
        game::Vector3 impactDirection = {};
        std::uint64_t observedAt = 0;
    };

    class CombatHitObservationCache final
    {
    public:
        static constexpr std::size_t Capacity = 256;
        static constexpr std::uint64_t GraceMilliseconds = 3'000;

        void Observe(const CombatHitObservation& observation) noexcept
        {
            if (!observation.source.IsValid() ||
                !observation.target.IsValid() || observation.observedAt == 0 ||
                !observation.nativeReactionExpected)
            {
                return;
            }
            Prune(observation.observedAt);
            if (observations_.size() >= Capacity)
            {
                observations_.pop_front();
            }
            observations_.push_back(observation);
        }

        [[nodiscard]] bool Consume(
            const protocol::CombatHitMessage& result,
            const std::uint64_t now) noexcept
        {
            Prune(now);
            const combat::CombatLifecycle source{
                result.sourceDomain == protocol::CombatActionDomain::Player
                    ? combat::CombatSubjectKind::PlayerActor
                    : combat::CombatSubjectKind::WorldEntity,
                result.sourceId,
                result.sourceGeneration,
                result.sourceMapEpoch};
            const combat::CombatLifecycle target{
                result.targetKind == protocol::CombatParticipantKind::Player
                    ? combat::CombatSubjectKind::PlayerActor
                    : combat::CombatSubjectKind::WorldEntity,
                result.targetId,
                result.targetGeneration,
                result.targetMapEpoch};
            if (!source.IsValid() || !target.IsValid())
            {
                return false;
            }
            for (auto iterator = observations_.begin();
                 iterator != observations_.end(); ++iterator)
            {
                if (iterator->source != source || iterator->target != target ||
                    iterator->impactFlags != result.impactFlags ||
                    !VectorsMatch(*iterator, result))
                {
                    continue;
                }
                observations_.erase(iterator);
                return true;
            }
            return false;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return observations_.size();
        }

        void Clear() noexcept
        {
            observations_.clear();
        }

    private:
        [[nodiscard]] static bool VectorsMatch(
            const CombatHitObservation& observation,
            const protocol::CombatHitMessage& result) noexcept
        {
            constexpr float tolerance = 0.5f;
            const auto close = [tolerance](float left, float right) noexcept
            {
                return left >= right - tolerance && left <= right + tolerance;
            };
            const bool hasPosition =
                (result.impactFlags &
                    protocol::combat_hit_impact_flag::HasPosition) != 0;
            const bool hasDirection =
                (result.impactFlags &
                    protocol::combat_hit_impact_flag::HasDirection) != 0;
            return (!hasPosition ||
                    (close(observation.impactPosition.x,
                          result.impactPosition.x) &&
                     close(observation.impactPosition.y,
                          result.impactPosition.y) &&
                     close(observation.impactPosition.z,
                          result.impactPosition.z))) &&
                (!hasDirection ||
                    (close(observation.impactDirection.x,
                          result.impactDirection.x) &&
                     close(observation.impactDirection.y,
                          result.impactDirection.y) &&
                     close(observation.impactDirection.z,
                          result.impactDirection.z)));
        }

        void Prune(const std::uint64_t now) noexcept
        {
            for (auto iterator = observations_.begin();
                 iterator != observations_.end();)
            {
                if (now >= iterator->observedAt &&
                    now - iterator->observedAt > GraceMilliseconds)
                {
                    iterator = observations_.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }
        std::deque<CombatHitObservation> observations_;
    };
}
