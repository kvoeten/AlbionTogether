#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Combat/CombatHitReactionPolicy.h"
#include "Multiplayer/Combat/CombatTerminalTransitionState.h"
#include "Multiplayer/Protocol/CombatHitCodec.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Replication/CombatHitDeliveryState.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/ReliableStreamTransport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
    using fable::multiplayer::ReliableStreamTransport;
    using fable::multiplayer::combat::CombatActionLedger;
    using fable::multiplayer::combat::CombatHitAdmission;
    using fable::multiplayer::combat::CombatHitKey;
    using fable::multiplayer::combat::CombatLifecycle;
    using fable::multiplayer::combat::CombatSourceAction;
    using fable::multiplayer::combat::CombatSubjectKind;
    using fable::multiplayer::combat::CombatTerminalTransitionState;
    using fable::multiplayer::protocol::CombatActionDomain;
    using fable::multiplayer::protocol::CombatHitMessage;
    using fable::multiplayer::protocol::CombatHitPhase;
    using fable::multiplayer::protocol::CombatParticipantKind;
    using fable::multiplayer::protocol::PacketType;
    using fable::multiplayer::replication::CombatHitPublicationAttempt;
    using fable::multiplayer::replication::CombatHitPublicationQueue;
    using fable::multiplayer::replication::CombatHitObservation;
    using fable::multiplayer::replication::CombatHitObservationCache;
    using fable::multiplayer::replication::CombatHitResultRevisionCache;

    constexpr std::size_t kWireCombatHitBytes = 148;
    constexpr std::size_t kReserved1Offset = 50;
    constexpr std::size_t kHealthBeforeOffset = 100;
    constexpr std::uint64_t kSourceActorId = UINT64_C(0x1000000000000011);
    constexpr std::uint64_t kTargetActorId = UINT64_C(0x2000000000000022);
    constexpr std::uint64_t kTargetEntityUid = UINT64_C(0xF123456789ABCDEF);

    int combatHitFailures = 0;

    void Check(const bool condition, const char* expression, const char* test)
    {
        if (!condition)
        {
            std::cerr << test << ": failed: " << expression << '\n';
            ++combatHitFailures;
        }
    }

#define COMBAT_CHECK(test, expression) \
    Check((expression), #expression, (test))

    CombatHitMessage PlayerToEntityCandidate()
    {
        using namespace fable::multiplayer::protocol;
        CombatHitMessage message;
        message.phase = CombatHitPhase::Candidate;
        message.sourceDomain = CombatActionDomain::Player;
        message.targetKind = CombatParticipantKind::WorldEntity;
        message.impactFlags = combat_hit_impact_flag::HasPosition |
            combat_hit_impact_flag::HasDirection;
        message.reactionFlags = combat_hit_reaction_flag::Blockable |
            combat_hit_reaction_flag::PlayHitResponse |
            combat_hit_reaction_flag::CauseRecoil;
        message.sourceActionId = UINT64_C(0x3000000000000033);
        message.sourceId = kSourceActorId;
        message.sourceOwnerActorId = kSourceActorId;
        message.sourceAuthorityEpoch = 3;
        message.sourceGeneration = 4;
        message.sourceMapEpoch = 5;
        message.sourceMapId = 100;
        message.targetId = kTargetEntityUid;
        message.targetGeneration = 6;
        message.targetMapEpoch = 7;
        message.targetMapId = 100;
        message.candidateSequence = UINT64_C(0x4000000000000044);
        message.hitOrdinal = 1;
        message.healthBefore = 100.0f;
        message.healthAfter = 75.0f;
        message.maximumHealth = 100.0f;
        message.impactPosition = {10.0f, 20.0f, 30.0f};
        message.impactDirection = {1.0f, 0.0f, 0.0f};
        message.resolverActorId = kSourceActorId;
        return message;
    }

    CombatHitMessage PlayerToPlayerCandidate()
    {
        CombatHitMessage message = PlayerToEntityCandidate();
        message.targetKind = CombatParticipantKind::Player;
        message.targetId = kTargetActorId;
        message.targetAuthorityEpoch = 8;
        message.targetGeneration = 9;
        message.targetMapEpoch = 10;
        message.resolverActorId = kSourceActorId;
        return message;
    }

    bool Encode(
        const CombatHitMessage& message,
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& bytes,
        std::size_t& size)
    {
        return fable::multiplayer::protocol::EncodeCombatHitMessage(
            message, bytes.data(), bytes.size(), size);
    }

    bool SameMessage(
        const CombatHitMessage& left,
        const CombatHitMessage& right)
    {
        return left.phase == right.phase &&
            left.sourceDomain == right.sourceDomain &&
            left.targetKind == right.targetKind &&
            left.impactFlags == right.impactFlags &&
            left.reactionFlags == right.reactionFlags &&
            left.sourceActionId == right.sourceActionId &&
            left.sourceId == right.sourceId &&
            left.sourceOwnerActorId == right.sourceOwnerActorId &&
            left.sourceAuthorityEpoch == right.sourceAuthorityEpoch &&
            left.sourceGeneration == right.sourceGeneration &&
            left.sourceMapEpoch == right.sourceMapEpoch &&
            left.sourceActionEpoch == right.sourceActionEpoch &&
            left.sourceMapId == right.sourceMapId &&
            left.targetId == right.targetId &&
            left.targetAuthorityEpoch == right.targetAuthorityEpoch &&
            left.targetGeneration == right.targetGeneration &&
            left.targetMapEpoch == right.targetMapEpoch &&
            left.targetMapId == right.targetMapId &&
            left.candidateSequence == right.candidateSequence &&
            left.hitOrdinal == right.hitOrdinal &&
            left.hostTargetRevision == right.hostTargetRevision &&
            left.healthBefore == right.healthBefore &&
            left.healthAfter == right.healthAfter &&
            left.maximumHealth == right.maximumHealth &&
            left.reactionId == right.reactionId &&
            left.impactPosition.x == right.impactPosition.x &&
            left.impactPosition.y == right.impactPosition.y &&
            left.impactPosition.z == right.impactPosition.z &&
            left.impactDirection.x == right.impactDirection.x &&
            left.impactDirection.y == right.impactDirection.y &&
            left.impactDirection.z == right.impactDirection.z &&
            left.resolverActorId == right.resolverActorId;
    }

    void TestCombatHitCodecRoundTripAndRejection()
    {
        constexpr const char* test = "combat hit codec roundtrip/rejection";
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> bytes = {};
        std::size_t size = 0;
        const CombatHitMessage candidate = PlayerToEntityCandidate();
        COMBAT_CHECK(test, Encode(candidate, bytes, size));
        COMBAT_CHECK(test, size == kWireCombatHitBytes);

        CombatHitMessage decoded;
        COMBAT_CHECK(test,
            fable::multiplayer::protocol::DecodeCombatHitMessage(
                bytes.data(), size, decoded));
        COMBAT_CHECK(test, SameMessage(candidate, decoded));

        CombatHitMessage result = candidate;
        result.phase = CombatHitPhase::Result;
        result.hostTargetRevision = UINT64_C(0x5000000000000055);
        COMBAT_CHECK(test, Encode(result, bytes, size));
        COMBAT_CHECK(test,
            fable::multiplayer::protocol::DecodeCombatHitMessage(
                bytes.data(), size, decoded));
        COMBAT_CHECK(test, SameMessage(result, decoded));

        CombatHitMessage invalid = candidate;
        invalid.hostTargetRevision = 1;
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid = candidate;
        invalid.hitOrdinal = 0;
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid = candidate;
        invalid.targetMapId = 101;
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid = candidate;
        invalid.healthBefore =
            (std::numeric_limits<float>::quiet_NaN)();
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid = candidate;
        invalid.impactFlags = 0;
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid = candidate;
        invalid.sourceDomain = CombatActionDomain::WorldEntity;
        invalid.sourceAuthorityEpoch = 0;
        invalid.sourceActionEpoch = 0;
        COMBAT_CHECK(test, !Encode(invalid, bytes, size));
        invalid.sourceActionEpoch = 2;
        COMBAT_CHECK(test, Encode(invalid, bytes, size));

        COMBAT_CHECK(test, Encode(candidate, bytes, size));
        bytes[kReserved1Offset] = 1;
        decoded = result;
        COMBAT_CHECK(test,
            !fable::multiplayer::protocol::DecodeCombatHitMessage(
                bytes.data(), size, decoded));
        COMBAT_CHECK(test, decoded.sourceActionId == 0);

        COMBAT_CHECK(test, Encode(candidate, bytes, size));
        const float notFinite =
            (std::numeric_limits<float>::infinity)();
        std::memcpy(bytes.data() + kHealthBeforeOffset,
            &notFinite, sizeof(notFinite));
        COMBAT_CHECK(test,
            !fable::multiplayer::protocol::DecodeCombatHitMessage(
                bytes.data(), size, decoded));
        COMBAT_CHECK(test,
            !fable::multiplayer::protocol::DecodeCombatHitMessage(
                bytes.data(), size - 1, decoded));
    }

    void TestCombatHitTargetStreamEnforcement()
    {
        constexpr const char* test = "combat hit target stream enforcement";
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> bytes = {};
        std::size_t size = 0;

        const CombatHitMessage playerTarget = PlayerToPlayerCandidate();
        COMBAT_CHECK(test, Encode(playerTarget, bytes, size));
        COMBAT_CHECK(test, ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Actor(kTargetActorId),
            PacketType::CombatHit, bytes.data(), size));
        COMBAT_CHECK(test, !ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Actor(kTargetActorId + 1),
            PacketType::CombatHit, bytes.data(), size));
        COMBAT_CHECK(test, !ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Entity(kTargetActorId),
            PacketType::CombatHit, bytes.data(), size));

        const CombatHitMessage entityTarget = PlayerToEntityCandidate();
        COMBAT_CHECK(test, Encode(entityTarget, bytes, size));
        COMBAT_CHECK(test, ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Entity(kTargetEntityUid),
            PacketType::CombatHit, bytes.data(), size));
        COMBAT_CHECK(test, !ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Entity(kTargetEntityUid - 1),
            PacketType::CombatHit, bytes.data(), size));
        COMBAT_CHECK(test, !ReliableStreamTransport::IsMessageValid(
            fable::multiplayer::reliable_stream::Actor(kTargetEntityUid),
            PacketType::CombatHit, bytes.data(), size));
    }

    CombatLifecycle PlayerLifecycle(
        const std::uint64_t actorId,
        const std::uint32_t generation,
        const std::uint32_t mapEpoch)
    {
        return {CombatSubjectKind::PlayerActor, actorId, generation, mapEpoch};
    }

    CombatLifecycle EntityLifecycle(
        const std::uint64_t entityUid,
        const std::uint32_t generation,
        const std::uint32_t mapEpoch)
    {
        return {
            CombatSubjectKind::WorldEntity, entityUid, generation, mapEpoch};
    }

    void TestCombatLedgerDuplicateAfterFinish()
    {
        constexpr const char* test = "combat ledger duplicate after finish";
        CombatActionLedger ledger;
        const CombatSourceAction action{
            PlayerLifecycle(kSourceActorId, 1, 1), 101, 9};
        const CombatLifecycle target = EntityLifecycle(kTargetEntityUid, 2, 3);
        COMBAT_CHECK(test, ledger.Begin(action));
        std::uint64_t firstRevision = 0;
        COMBAT_CHECK(test, ledger.RecordHit(action, target, 1,
            firstRevision) == CombatHitAdmission::Accepted);
        COMBAT_CHECK(test, firstRevision == 1);
        COMBAT_CHECK(test, ledger.Finish(action));
        COMBAT_CHECK(test, !ledger.IsCurrent(action));
        std::uint64_t duplicateRevision = 0;
        COMBAT_CHECK(test, ledger.RecordHit(action, target, 1,
            duplicateRevision) == CombatHitAdmission::Duplicate);
        COMBAT_CHECK(test, duplicateRevision == firstRevision);
        COMBAT_CHECK(test, ledger.HitCount() == 1);
        COMBAT_CHECK(test, !ledger.Begin(action));
    }

    void TestCombatLedgerLifecycleFencing()
    {
        constexpr const char* test = "combat ledger lifecycle fencing";
        CombatActionLedger ledger;
        const CombatLifecycle oldSource = PlayerLifecycle(kSourceActorId, 1, 1);
        const CombatSourceAction oldAction{oldSource, 201, 4};
        const CombatLifecycle target = EntityLifecycle(kTargetEntityUid, 1, 1);
        COMBAT_CHECK(test, ledger.Begin(oldAction));
        COMBAT_CHECK(test, ledger.FenceLifecycle(
            PlayerLifecycle(kSourceActorId, 2, 1)));
        COMBAT_CHECK(test, !ledger.Begin(oldAction));
        std::uint64_t revision = 0;
        COMBAT_CHECK(test, ledger.RecordHit(oldAction, target, 1,
            revision) == CombatHitAdmission::UnknownAction);

        const CombatSourceAction currentAction{
            PlayerLifecycle(kSourceActorId, 2, 1), 202, 4};
        COMBAT_CHECK(test, ledger.Begin(currentAction));
        COMBAT_CHECK(test, ledger.FenceLifecycle(
            EntityLifecycle(kTargetEntityUid, 1, 2)));
        COMBAT_CHECK(test, ledger.RecordHit(currentAction, target, 1,
            revision) == CombatHitAdmission::StaleLifecycle);
        COMBAT_CHECK(test, !ledger.FenceLifecycle(
            EntityLifecycle(kTargetEntityUid, 2, 1)));
    }

    void TestCombatLedgerMultiHitOrdinals()
    {
        constexpr const char* test = "combat ledger multi-hit ordinals";
        CombatActionLedger ledger;
        const CombatSourceAction action{
            PlayerLifecycle(kSourceActorId, 1, 1), 301, 2};
        const CombatLifecycle target = EntityLifecycle(kTargetEntityUid, 1, 1);
        COMBAT_CHECK(test, ledger.Begin(action));
        std::uint64_t firstRevision = 0;
        std::uint64_t secondRevision = 0;
        std::uint64_t duplicateRevision = 0;
        COMBAT_CHECK(test, ledger.RecordHit(action, target, 1,
            firstRevision) == CombatHitAdmission::Accepted);
        COMBAT_CHECK(test, ledger.RecordHit(action, target, 2,
            secondRevision) == CombatHitAdmission::Accepted);
        COMBAT_CHECK(test, firstRevision == 1);
        COMBAT_CHECK(test, secondRevision == 2);
        COMBAT_CHECK(test, ledger.RecordHit(action, target, 1,
            duplicateRevision) == CombatHitAdmission::Duplicate);
        COMBAT_CHECK(test, duplicateRevision == firstRevision);
        COMBAT_CHECK(test, ledger.TargetRevision(target) == 2);
        COMBAT_CHECK(test, ledger.HasHit(
            CombatHitKey{action, target, 1}));
        COMBAT_CHECK(test, ledger.HasHit(
            CombatHitKey{action, target, 2}));
    }

    void TestCombatLedgerBounds()
    {
        constexpr const char* test = "combat ledger bounded behavior";
        CombatActionLedger actions;
        const CombatLifecycle source = PlayerLifecycle(kSourceActorId, 1, 1);
        for (std::size_t index = 0;
             index < CombatActionLedger::MaxCurrentActions + 8; ++index)
        {
            COMBAT_CHECK(test, actions.Begin(
                {source, static_cast<std::uint64_t>(index + 1), 1}));
            COMBAT_CHECK(test,
                actions.CurrentActionCount() <=
                    CombatActionLedger::MaxCurrentActions);
        }
        COMBAT_CHECK(test,
            actions.CurrentActionCount() ==
                CombatActionLedger::MaxCurrentActions);
        COMBAT_CHECK(test, !actions.IsCurrent({source, 1, 1}));
        COMBAT_CHECK(test, actions.IsCurrent({source,
            CombatActionLedger::MaxCurrentActions + 8, 1}));

        CombatActionLedger hits;
        const CombatSourceAction hitAction{source, 501, 1};
        const CombatLifecycle target = EntityLifecycle(kTargetEntityUid, 1, 1);
        COMBAT_CHECK(test, hits.Begin(hitAction));
        std::uint64_t revision = 0;
        for (std::size_t ordinal = 1;
             ordinal <= CombatActionLedger::MaxHitKeys + 8; ++ordinal)
        {
            COMBAT_CHECK(test, hits.RecordHit(
                hitAction,
                target,
                static_cast<std::uint32_t>(ordinal),
                revision) == CombatHitAdmission::Accepted);
            COMBAT_CHECK(test,
                hits.HitCount() <= CombatActionLedger::MaxHitKeys);
        }
        COMBAT_CHECK(test, hits.HitCount() == CombatActionLedger::MaxHitKeys);
        COMBAT_CHECK(test, !hits.HasHit({hitAction, target, 1}));
        COMBAT_CHECK(test, hits.HasHit({hitAction, target,
            static_cast<std::uint32_t>(
                CombatActionLedger::MaxHitKeys + 8)}));
        COMBAT_CHECK(test, hits.TargetRevision(target) ==
            CombatActionLedger::MaxHitKeys + 8);

        CombatActionLedger fences;
        for (std::size_t index = 0;
             index < CombatActionLedger::MaxLifecycleFences + 8; ++index)
        {
            COMBAT_CHECK(test, fences.FenceActor(
                static_cast<std::uint64_t>(index + 1), 1, 1));
            COMBAT_CHECK(test,
                fences.FenceCount() <=
                    CombatActionLedger::MaxLifecycleFences);
        }
        COMBAT_CHECK(test,
            fences.FenceCount() == CombatActionLedger::MaxLifecycleFences);
    }

    void TestCombatResultRevisionSessionScope()
    {
        constexpr const char* test =
            "combat result revision authority/session scope";
        CombatHitResultRevisionCache revisions;
        CombatHitMessage result = PlayerToPlayerCandidate();
        result.phase = CombatHitPhase::Result;
        result.hostTargetRevision = 7;
        constexpr std::uint64_t firstSession = UINT64_C(0xA001);
        constexpr std::uint64_t secondSession = UINT64_C(0xA002);

        COMBAT_CHECK(test, revisions.CanApply(result, firstSession));
        revisions.MarkApplied(result, firstSession);
        COMBAT_CHECK(test, !revisions.CanApply(result, firstSession));
        result.hostTargetRevision = 6;
        COMBAT_CHECK(test, !revisions.CanApply(result, firstSession));
        result.hostTargetRevision = 8;
        COMBAT_CHECK(test, revisions.CanApply(result, firstSession));

        result.hostTargetRevision = 1;
        COMBAT_CHECK(test, revisions.CanApply(result, secondSession));
        ++result.targetAuthorityEpoch;
        COMBAT_CHECK(test, revisions.CanApply(result, firstSession));
        --result.targetAuthorityEpoch;
        ++result.targetGeneration;
        COMBAT_CHECK(test, revisions.CanApply(result, firstSession));
        --result.targetGeneration;
        ++result.targetMapEpoch;
        COMBAT_CHECK(test, revisions.CanApply(result, firstSession));

        revisions.Clear();
        for (std::size_t index = 0;
             index < CombatHitResultRevisionCache::Capacity + 8; ++index)
        {
            result.targetId = kTargetActorId + index;
            result.hostTargetRevision = 1;
            revisions.MarkApplied(result, firstSession);
            COMBAT_CHECK(test,
                revisions.Size() <= CombatHitResultRevisionCache::Capacity);
        }
        COMBAT_CHECK(test,
            revisions.Size() == CombatHitResultRevisionCache::Capacity);
    }

    void TestCombatPublicationReservationAndFairness()
    {
        constexpr const char* test =
            "combat publication durable reservation/fairness";
        CombatHitPublicationQueue publications;
        CombatHitMessage actorA = PlayerToPlayerCandidate();
        actorA.targetId = kTargetActorId;
        CombatHitMessage actorASecond = actorA;
        actorASecond.candidateSequence += 1;
        actorASecond.hitOrdinal += 1;
        CombatHitMessage actorB = actorA;
        actorB.targetId = kTargetActorId + 1;
        actorB.candidateSequence += 2;

        CombatHitPublicationQueue::Reservation reservation;
        COMBAT_CHECK(test, publications.TryReserve(actorA, reservation));
        COMBAT_CHECK(test, reservation.token != 0);
        COMBAT_CHECK(test, publications.Size() == 1);
        actorA.phase = CombatHitPhase::Result;
        actorA.hostTargetRevision = 1;
        COMBAT_CHECK(test,
            publications.Commit(reservation, std::move(actorA)));
        COMBAT_CHECK(test, publications.Enqueue(actorASecond));
        COMBAT_CHECK(test, publications.Enqueue(actorB));

        std::vector<std::uint64_t> submitted;
        bool deferred = false;
        COMBAT_CHECK(test, publications.DrainRound(
            [&](const CombatHitMessage& message,
                const fable::multiplayer::ReliableStreamId stream)
            {
                if (stream == fable::multiplayer::reliable_stream::Actor(
                        kTargetActorId))
                {
                    return CombatHitPublicationAttempt::Deferred;
                }
                submitted.push_back(message.targetId);
                return CombatHitPublicationAttempt::Submitted;
            },
            deferred));
        COMBAT_CHECK(test, deferred);
        COMBAT_CHECK(test, submitted.size() == 1);
        COMBAT_CHECK(test, submitted.front() == kTargetActorId + 1);
        COMBAT_CHECK(test, publications.Size() == 2);

        submitted.clear();
        COMBAT_CHECK(test, publications.DrainRound(
            [&](const CombatHitMessage& message,
                const fable::multiplayer::ReliableStreamId)
            {
                submitted.push_back(message.targetId);
                return CombatHitPublicationAttempt::Submitted;
            },
            deferred));
        COMBAT_CHECK(test, !deferred);
        COMBAT_CHECK(test, submitted.size() == 1);
        COMBAT_CHECK(test, publications.Size() == 1);
        COMBAT_CHECK(test, publications.DrainRound(
            [](const CombatHitMessage&,
               const fable::multiplayer::ReliableStreamId)
            {
                return CombatHitPublicationAttempt::Submitted;
            },
            deferred));
        COMBAT_CHECK(test, publications.Size() == 0);

        CombatHitPublicationQueue bounded;
        for (std::size_t index = 0;
             index < CombatHitPublicationQueue::Capacity; ++index)
        {
            CombatHitMessage message = PlayerToEntityCandidate();
            message.targetId += index;
            COMBAT_CHECK(test, bounded.Enqueue(std::move(message)));
            COMBAT_CHECK(test,
                bounded.Size() <= CombatHitPublicationQueue::Capacity);
        }
        COMBAT_CHECK(test, bounded.Full());
        CombatHitPublicationQueue::Reservation rejected;
        COMBAT_CHECK(test,
            !bounded.TryReserve(PlayerToEntityCandidate(), rejected));
        COMBAT_CHECK(test, rejected.token == 0);
    }

    void TestNativeObservationSuppressesDuplicateReaction()
    {
        constexpr const char* test =
            "native hit observation duplicate suppression";
        CombatHitObservationCache cache;
        const CombatHitMessage result = PlayerToPlayerCandidate();
        CombatHitObservation observation;
        observation.source = {
            CombatSubjectKind::PlayerActor,
            result.sourceId,
            result.sourceGeneration,
            result.sourceMapEpoch};
        observation.target = {
            CombatSubjectKind::PlayerActor,
            result.targetId,
            result.targetGeneration,
            result.targetMapEpoch};
        observation.impactFlags = result.impactFlags;
        observation.nativeReactionExpected = true;
        observation.impactPosition = result.impactPosition;
        observation.impactDirection = result.impactDirection;
        observation.observedAt = 100;
        cache.Observe(observation);
        COMBAT_CHECK(test, cache.Size() == 1);
        COMBAT_CHECK(test, cache.Consume(result, 250));
        COMBAT_CHECK(test, cache.Size() == 0);
        COMBAT_CHECK(test, !cache.Consume(result, 251));

        observation.observedAt = 100;
        cache.Observe(observation);
        COMBAT_CHECK(
            test,
            !cache.Consume(result, 100 +
                CombatHitObservationCache::GraceMilliseconds + 1));
        COMBAT_CHECK(test, cache.Size() == 0);
    }

    void TestOwnerReactionReplayNeedsNativeObservation()
    {
        constexpr const char* test =
            "owner reaction replay needs native observation";
        CombatHitObservationCache cache;
        CombatHitMessage ownerResult = PlayerToPlayerCandidate();
        ownerResult.phase = CombatHitPhase::Result;
        ownerResult.hostTargetRevision = 1;
        ownerResult.resolverActorId = kSourceActorId;

        CombatHitObservation observation;
        observation.source = {
            CombatSubjectKind::PlayerActor,
            ownerResult.sourceId,
            ownerResult.sourceGeneration,
            ownerResult.sourceMapEpoch};
        observation.target = {
            CombatSubjectKind::PlayerActor,
            ownerResult.targetId,
            ownerResult.targetGeneration,
            ownerResult.targetMapEpoch};
        observation.impactFlags = ownerResult.impactFlags;
        observation.nativeReactionExpected = true;
        observation.impactPosition = ownerResult.impactPosition;
        observation.impactDirection = ownerResult.impactDirection;
        observation.observedAt = 500;
        cache.Observe(observation);

        // A source-resolver result still consumes a confirmed local native
        // response rather than submitting a second one.
        COMBAT_CHECK(test, cache.Consume(ownerResult, 600));
        // With no observation, the applicator's owner-replay branch remains
        // eligible and must not be mistaken for owner-native completion.
        COMBAT_CHECK(test, !cache.Consume(ownerResult, 601));
    }

    void TestTerminalHitRetainsVictimReaction()
    {
        constexpr const char* test =
            "terminal hit retains native victim reaction";
        CombatHitMessage result = PlayerToEntityCandidate();
        result.phase = CombatHitPhase::Result;
        result.hostTargetRevision = 1;
        result.healthBefore = 14.0f;
        result.healthAfter = 0.0f;
        result.reactionFlags |=
            fable::multiplayer::protocol::combat_hit_reaction_flag::Killed;
        COMBAT_CHECK(test,
            fable::multiplayer::combat::ShouldSubmitVictimReaction(result));

        result.reactionFlags |=
            fable::multiplayer::protocol::combat_hit_reaction_flag::Blocked;
        COMBAT_CHECK(test,
            !fable::multiplayer::combat::ShouldSubmitVictimReaction(result));
        result.reactionFlags &=
            ~fable::multiplayer::protocol::combat_hit_reaction_flag::Blocked;
        result.healthAfter = result.healthBefore;
        COMBAT_CHECK(test,
            !fable::multiplayer::combat::ShouldSubmitVictimReaction(result));
    }

    void TestTerminalTransitionProgressIsLifecycleScoped()
    {
        constexpr const char* test =
            "terminal transition progress is lifecycle scoped";
        CombatTerminalTransitionState transitions;
        const CombatLifecycle first{
            CombatSubjectKind::WorldEntity,
            kTargetEntityUid,
            6,
            7};
        const CombatLifecycle replacement{
            CombatSubjectKind::WorldEntity,
            kTargetEntityUid,
            7,
            8};

        COMBAT_CHECK(test, !transitions.Get(first).healthHandled);
        COMBAT_CHECK(test, !transitions.Get(first).reactionHandled);
        COMBAT_CHECK(test, !transitions.Get(first).deathSubmitted);
        transitions.MarkHealthHandled(first);
        transitions.MarkReactionHandled(first);
        transitions.MarkDeathSubmitted(first);
        COMBAT_CHECK(test, transitions.Get(first).healthHandled);
        COMBAT_CHECK(test, transitions.Get(first).reactionHandled);
        COMBAT_CHECK(test, transitions.Get(first).deathSubmitted);
        COMBAT_CHECK(test, !transitions.Get(replacement).healthHandled);
        COMBAT_CHECK(test, !transitions.Get(replacement).reactionHandled);
        COMBAT_CHECK(test, !transitions.Get(replacement).deathSubmitted);
        transitions.Clear();
        COMBAT_CHECK(test, !transitions.Get(first).deathSubmitted);
    }

    void TestDeferredNativeObservationIsRecordedOnce()
    {
        constexpr const char* test =
            "deferred native observation is recorded once";
        CombatHitObservationCache cache;
        const CombatHitMessage result = PlayerToPlayerCandidate();
        CombatHitObservation observation;
        observation.source = {
            CombatSubjectKind::PlayerActor,
            result.sourceId,
            result.sourceGeneration,
            result.sourceMapEpoch};
        observation.target = {
            CombatSubjectKind::PlayerActor,
            result.targetId,
            result.targetGeneration,
            result.targetMapEpoch};
        observation.impactFlags = result.impactFlags;
        observation.nativeReactionExpected = true;
        observation.impactPosition = result.impactPosition;
        observation.impactDirection = result.impactDirection;
        observation.observedAt = 700;

        // ProcessNative carries this processed bit with a deferred event. A
        // retry must not call Observe again and create a second cache entry.
        bool observationProcessed = false;
        const auto processObservation = [&]()
        {
            if (!observationProcessed)
            {
                cache.Observe(observation);
                observationProcessed = true;
            }
        };
        processObservation();
        processObservation();
        COMBAT_CHECK(test, cache.Size() == 1);
        COMBAT_CHECK(test, cache.Consume(result, 800));
        COMBAT_CHECK(test, cache.Size() == 0);
        COMBAT_CHECK(test, !cache.Consume(result, 801));
    }
}

int RunCombatHitReplicationTests()
{
    combatHitFailures = 0;
    TestCombatHitCodecRoundTripAndRejection();
    TestCombatHitTargetStreamEnforcement();
    TestCombatLedgerDuplicateAfterFinish();
    TestCombatLedgerLifecycleFencing();
    TestCombatLedgerMultiHitOrdinals();
    TestCombatLedgerBounds();
    TestCombatResultRevisionSessionScope();
    TestCombatPublicationReservationAndFairness();
    TestNativeObservationSuppressesDuplicateReaction();
    TestOwnerReactionReplayNeedsNativeObservation();
    TestTerminalHitRetainsVictimReaction();
    TestTerminalTransitionProgressIsLifecycleScoped();
    TestDeferredNativeObservationIsRecordedOnce();
    return combatHitFailures;
}
