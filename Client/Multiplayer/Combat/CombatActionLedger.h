#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace fable::multiplayer::combat
{
    // A stable identity for either a player presentation or a world entity.
    // Native pointers and map-local Thing UIDs are deliberately not part of
    // this contract.  Generation and mapEpoch fence recycled identities.
    enum class CombatSubjectKind : std::uint8_t
    {
        PlayerActor = 1,
        WorldEntity = 2,
    };

    struct CombatLifecycle final
    {
        CombatSubjectKind kind = CombatSubjectKind::PlayerActor;
        std::uint64_t subjectId = 0;
        std::uint32_t generation = 0;
        std::uint32_t mapEpoch = 0;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const CombatLifecycle& other)
            const noexcept;
        [[nodiscard]] bool operator!=(const CombatLifecycle& other)
            const noexcept
        {
            return !(*this == other);
        }
    };

    struct CombatSourceAction final
    {
        CombatLifecycle source;
        std::uint64_t actionId = 0;
        std::uint32_t actionEpoch = 0;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const CombatSourceAction& other)
            const noexcept;
        [[nodiscard]] bool operator!=(const CombatSourceAction& other)
            const noexcept
        {
            return !(*this == other);
        }
    };

    // This is the semantic identity of one hit opportunity.  hitOrdinal is
    // important for multi-hit actions: a repeated packet for ordinal 1 is a
    // duplicate, while ordinal 2 is a distinct hit of the same action.
    struct CombatHitKey final
    {
        CombatSourceAction sourceAction;
        CombatLifecycle target;
        std::uint32_t hitOrdinal = 0;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const CombatHitKey& other)
            const noexcept;
        [[nodiscard]] bool operator!=(const CombatHitKey& other)
            const noexcept
        {
            return !(*this == other);
        }
    };

    enum class CombatHitAdmission : std::uint8_t
    {
        Accepted = 1,
        Duplicate = 2,
        UnknownAction = 3,
        StaleLifecycle = 4,
        Invalid = 5,
    };

    // Bounded, process-local combat bookkeeping.  This class owns no native
    // objects and performs no transport work.  It is intended to sit behind
    // the action/vitals seams and make hit application idempotent.
    class CombatActionLedger final
    {
    public:
        static constexpr std::size_t MaxCurrentActions = 256;
        static constexpr std::size_t MaxHitKeys = 2048;
        static constexpr std::size_t MaxLifecycleFences = 512;

        CombatActionLedger();

        // Opens a source action.  A source action identity is accepted once;
        // callers should use Touch for subsequent observations.  A finished
        // or stale identity cannot be reopened.
        [[nodiscard]] bool Begin(
            const CombatSourceAction& action,
            std::uint64_t observedAt = 0);
        [[nodiscard]] bool Touch(const CombatSourceAction& action) noexcept;
        [[nodiscard]] bool Finish(
            const CombatSourceAction& action,
            std::uint64_t observedAt = 0) noexcept;
        [[nodiscard]] bool IsCurrent(
            const CombatSourceAction& action) const noexcept;
        [[nodiscard]] bool FindLatest(
            const CombatLifecycle& source,
            CombatSourceAction& action) const noexcept;
        [[nodiscard]] bool FindAt(
            const CombatLifecycle& source,
            std::uint64_t observedAt,
            CombatSourceAction& action) const noexcept;

        // Records one semantic hit and returns the target-local monotonic
        // result revision.  Duplicate hits return the original revision and
        // never advance it.  Revisions are retained while the bounded target
        // revision entry is retained; an explicitly retired/fenced lifecycle
        // starts fresh only after a newer lifecycle is admitted.
        [[nodiscard]] CombatHitAdmission RecordHit(
            const CombatSourceAction& action,
            const CombatLifecycle& target,
            std::uint32_t hitOrdinal,
            std::uint64_t& resultRevision);
        [[nodiscard]] bool HasHit(const CombatHitKey& key) const noexcept;
        [[nodiscard]] std::uint64_t TargetRevision(
            const CombatLifecycle& target) const noexcept;

        // Remove one exact source action and its dedup entries.
        void ClearSource(const CombatSourceAction& action) noexcept;

        // Remove all records for the exact lifecycle.  The lifecycle remains
        // fenced so delayed packets for the retired generation are rejected.
        void ClearLifecycle(const CombatLifecycle& lifecycle) noexcept;

        // Establish or advance the current lifecycle for a subject, removing
        // older records.  A lower generation/mapEpoch is rejected.  This is
        // useful at actor/entity construction and map handoff boundaries.
        [[nodiscard]] bool FenceLifecycle(
            const CombatLifecycle& lifecycle) noexcept;

        [[nodiscard]] bool FenceActor(
            std::uint64_t actorId,
            std::uint32_t generation,
            std::uint32_t mapEpoch) noexcept;
        [[nodiscard]] bool FenceEntity(
            std::uint64_t entityUid,
            std::uint32_t generation,
            std::uint32_t mapEpoch) noexcept;
        void ClearActor(
            std::uint64_t actorId,
            std::uint32_t generation,
            std::uint32_t mapEpoch) noexcept;
        void ClearEntity(
            std::uint64_t entityUid,
            std::uint32_t generation,
            std::uint32_t mapEpoch) noexcept;

        void Clear() noexcept;
        [[nodiscard]] std::size_t CurrentActionCount() const noexcept;
        [[nodiscard]] std::size_t HitCount() const noexcept;
        [[nodiscard]] std::size_t FenceCount() const noexcept;

    private:
        struct SubjectKey final
        {
            CombatSubjectKind kind = CombatSubjectKind::PlayerActor;
            std::uint64_t subjectId = 0;

            [[nodiscard]] bool operator==(const SubjectKey& other)
                const noexcept
            {
                return kind == other.kind && subjectId == other.subjectId;
            }
        };

        struct ActionRecord final
        {
            bool finished = false;
            std::uint64_t serial = 0;
            std::uint64_t beganAt = 0;
            std::uint64_t finishedAt = 0;
        };

        struct HitRecord final
        {
            std::uint64_t resultRevision = 0;
            std::uint64_t serial = 0;
        };

        struct FenceRecord final
        {
            CombatLifecycle lifecycle;
            std::uint64_t serial = 0;
            bool retired = false;
        };

        struct TargetRevisionRecord final
        {
            std::uint64_t revision = 0;
            std::uint64_t serial = 0;
        };

        struct SourceActionHash final
        {
            std::size_t operator()(const CombatSourceAction& value)
                const noexcept;
        };
        struct HitKeyHash final
        {
            std::size_t operator()(const CombatHitKey& value)
                const noexcept;
        };
        struct SubjectHash final
        {
            std::size_t operator()(const SubjectKey& value)
                const noexcept;
        };

        [[nodiscard]] static SubjectKey ToSubjectKey(
            const CombatLifecycle& lifecycle) noexcept;
        [[nodiscard]] static bool IsNewer(
            const CombatLifecycle& candidate,
            const CombatLifecycle& current) noexcept;
        [[nodiscard]] bool AcceptLifecycle(
            const CombatLifecycle& lifecycle) noexcept;
        [[nodiscard]] bool IsLifecycleAccepted(
            const CombatLifecycle& lifecycle) const noexcept;
        [[nodiscard]] bool IsKnown(
            const CombatSourceAction& action) const noexcept;
        void RemoveLifecycleRecords(
            const CombatLifecycle& lifecycle,
            bool exact) noexcept;
        void RemoveSourceRecords(const CombatSourceAction& action) noexcept;
        void EvictOldestAction() noexcept;
        void EvictOldestHit() noexcept;
        void EvictOldestFence() noexcept;
        void RemoveHitsForSource(const CombatSourceAction& action) noexcept;
        void RemoveHitsForLifecycle(const CombatLifecycle& lifecycle) noexcept;

        std::unordered_map<CombatSourceAction, ActionRecord, SourceActionHash>
            currentActions_;
        std::unordered_map<CombatHitKey, HitRecord, HitKeyHash> hitKeys_;
        std::unordered_map<SubjectKey, FenceRecord, SubjectHash> fences_;
        std::unordered_map<SubjectKey, TargetRevisionRecord, SubjectHash>
            targetRevisions_;
        std::uint64_t nextSerial_ = 1;
    };
}
