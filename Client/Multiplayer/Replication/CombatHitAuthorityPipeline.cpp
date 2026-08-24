#include "CombatHitAuthorityPipeline.h"

#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <utility>

namespace fable::multiplayer::replication
{
    void CombatHitAuthorityPipeline::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        LocalHeroReplication& localHero,
        RemotePlayerChannels& remotePlayers,
        combat::CombatActionLedger& ledger,
        combat::CombatHitApplicator& applicator,
        CombatHitPublicationQueue& pendingPublications,
        CombatHitResultRevisionCache& appliedRevisions,
        const core::Diagnostics& diagnostics) noexcept
    {
        role_ = role;
        localActorId_ = localActorId;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        localHero_ = &localHero;
        remotePlayers_ = &remotePlayers;
        ledger_ = &ledger;
        applicator_ = &applicator;
        pendingPublications_ = &pendingPublications;
        appliedRevisions_ = &appliedRevisions;
        diagnostics_ = diagnostics;
    }

    void CombatHitAuthorityPipeline::Shutdown() noexcept
    {
        authority_ = nullptr;
        lifecycle_ = nullptr;
        localHero_ = nullptr;
        remotePlayers_ = nullptr;
        ledger_ = nullptr;
        applicator_ = nullptr;
        pendingPublications_ = nullptr;
        appliedRevisions_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
    }

    bool CombatHitAuthorityPipeline::ValidateResolverAuthority(
        const protocol::CombatHitMessage& candidate,
        const std::uint64_t sourceActorId) const noexcept
    {
        // The action owner is the only process allowed to resolve its native
        // collision. The target may be a protected remote presentation, so
        // requiring target ownership here would force a second retail OnHit
        // on the target peer and duplicate or crash native effect dispatch.
        return candidate.resolverActorId == sourceActorId &&
            candidate.sourceOwnerActorId == sourceActorId;
    }

    bool CombatHitAuthorityPipeline::IsTargetCurrent(
        const protocol::CombatHitMessage& message) const noexcept
    {
        if (message.targetKind == protocol::CombatParticipantKind::Player)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == message.targetId)
            {
                return local->authorityEpoch == message.targetAuthorityEpoch &&
                    local->actorGeneration == message.targetGeneration &&
                    local->mapEpoch == message.targetMapEpoch &&
                    local->mapId == message.targetMapId;
            }
            const PlayerState* const remote = remotePlayers_ != nullptr
                ? remotePlayers_->Find(message.targetId)
                : nullptr;
            const RemotePlayerLifecycle* const lifecycle =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(message.targetId)
                : nullptr;
            return remote != nullptr && lifecycle != nullptr &&
                lifecycle->active &&
                remote->authorityEpoch == message.targetAuthorityEpoch &&
                lifecycle->actorGeneration == message.targetGeneration &&
                lifecycle->mapEpoch == message.targetMapEpoch &&
                remote->mapId == message.targetMapId;
        }
        const entities::WorldEntityRecord* const world = lifecycle_ != nullptr
            ? lifecycle_->Directory().Find(message.targetId)
            : nullptr;
        return world != nullptr && world->live && world->available &&
            world->creature && world->generation == message.targetGeneration &&
            world->mapEpoch == message.targetMapEpoch &&
            world->mapId == message.targetMapId;
    }

    bool CombatHitAuthorityPipeline::IsSourceCurrent(
        const protocol::CombatHitMessage& message) const noexcept
    {
        if (message.sourceDomain == protocol::CombatActionDomain::Player)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == message.sourceId)
            {
                return message.sourceOwnerActorId == message.sourceId &&
                    local->authorityEpoch == message.sourceAuthorityEpoch &&
                    local->actorGeneration == message.sourceGeneration &&
                    local->mapEpoch == message.sourceMapEpoch &&
                    local->mapId == message.sourceMapId;
            }
            const PlayerState* const remote = remotePlayers_ != nullptr
                ? remotePlayers_->Find(message.sourceId)
                : nullptr;
            const RemotePlayerLifecycle* const lifecycle =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(message.sourceId)
                : nullptr;
            return remote != nullptr && lifecycle != nullptr &&
                lifecycle->active &&
                message.sourceOwnerActorId == message.sourceId &&
                remote->authorityEpoch == message.sourceAuthorityEpoch &&
                lifecycle->actorGeneration == message.sourceGeneration &&
                lifecycle->mapEpoch == message.sourceMapEpoch &&
                remote->mapId == message.sourceMapId;
        }

        const entities::WorldEntityRecord* const world = lifecycle_ != nullptr
            ? lifecycle_->Directory().Find(message.sourceId)
            : nullptr;
        return world != nullptr && world->live && world->available &&
            world->creature && world->generation == message.sourceGeneration &&
            world->mapEpoch == message.sourceMapEpoch &&
            world->mapId == message.sourceMapId &&
            world->simulationOwnerActorId == message.sourceOwnerActorId;
    }

    CombatHitAdmissionOutcome CombatHitAuthorityPipeline::AcceptCandidate(
        protocol::CombatHitMessage candidate,
        const std::uint64_t sourceActorId,
        const std::uint64_t sourceConnectionNonce)
    {
        CombatHitAdmissionOutcome outcome;
        if (role_ != PeerRole::Host || ledger_ == nullptr ||
            pendingPublications_ == nullptr)
        {
            outcome.disposition = CombatHitAdmissionDisposition::Failed;
            return outcome;
        }
        if (!IsSourceCurrent(candidate))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected",
                "candidate source lifecycle is stale");
            return outcome;
        }
        if (!IsTargetCurrent(candidate))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected",
                "candidate target lifecycle is stale");
            return outcome;
        }
        if (!ValidateResolverAuthority(candidate, sourceActorId))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected",
                "candidate resolver does not own the source action");
            return outcome;
        }
        const combat::CombatSourceAction sourceAction{
            {candidate.sourceDomain == protocol::CombatActionDomain::Player
                 ? combat::CombatSubjectKind::PlayerActor
                 : combat::CombatSubjectKind::WorldEntity,
             candidate.sourceId,
             candidate.sourceGeneration,
             candidate.sourceMapEpoch},
            candidate.sourceActionId,
            candidate.sourceDomain == protocol::CombatActionDomain::Player
                ? candidate.sourceAuthorityEpoch
                : candidate.sourceActionEpoch};
        const combat::CombatLifecycle target{
            candidate.targetKind == protocol::CombatParticipantKind::Player
                ? combat::CombatSubjectKind::PlayerActor
                : combat::CombatSubjectKind::WorldEntity,
            candidate.targetId,
            candidate.targetGeneration,
            candidate.targetMapEpoch};
        CombatHitPublicationQueue::Reservation publication;
        if (!pendingPublications_->TryReserve(candidate, publication))
        {
            outcome.disposition = CombatHitAdmissionDisposition::Deferred;
            return outcome;
        }

        std::uint64_t revision = 0;
        const combat::CombatHitAdmission admission = ledger_->RecordHit(
            sourceAction, target, candidate.hitOrdinal, revision);
        if (admission == combat::CombatHitAdmission::UnknownAction)
        {
            pendingPublications_->Cancel(publication);
            outcome.disposition = CombatHitAdmissionDisposition::Deferred;
            return outcome;
        }
        if (admission == combat::CombatHitAdmission::Duplicate ||
            admission != combat::CombatHitAdmission::Accepted)
        {
            pendingPublications_->Cancel(publication);
            return outcome;
        }
        candidate.phase = protocol::CombatHitPhase::Result;
        candidate.hostTargetRevision = revision;
        if (!pendingPublications_->Commit(publication, candidate))
        {
            outcome.disposition = CombatHitAdmissionDisposition::Failed;
            return outcome;
        }

        bool admissionRecorded = true;
        CombatHitAdmissionOutcome result = AcceptResult(
            candidate, sourceConnectionNonce, true, admissionRecorded);
        result.admissionRecorded = admissionRecorded;
        if (result.Deferred())
        {
            result.hasDeferredResult = true;
            result.deferredResult = {
                std::move(candidate),
                sourceActorId,
                sourceConnectionNonce,
                true,
                true};
        }
        return result;
    }

    CombatHitAdmissionOutcome CombatHitAuthorityPipeline::AcceptResult(
        const protocol::CombatHitMessage& result,
        const std::uint64_t authorityConnectionNonce,
        const bool admissionAlreadyRecorded,
        bool& admissionRecorded)
    {
        CombatHitAdmissionOutcome outcome;
        if (ledger_ == nullptr || applicator_ == nullptr ||
            appliedRevisions_ == nullptr || !IsTargetCurrent(result))
        {
            return outcome;
        }
        const combat::CombatSourceAction sourceAction{
            {result.sourceDomain == protocol::CombatActionDomain::Player
                 ? combat::CombatSubjectKind::PlayerActor
                 : combat::CombatSubjectKind::WorldEntity,
             result.sourceId,
             result.sourceGeneration,
             result.sourceMapEpoch},
            result.sourceActionId,
            result.sourceDomain == protocol::CombatActionDomain::Player
                ? result.sourceAuthorityEpoch
                : result.sourceActionEpoch};
        const combat::CombatLifecycle target{
            result.targetKind == protocol::CombatParticipantKind::Player
                ? combat::CombatSubjectKind::PlayerActor
                : combat::CombatSubjectKind::WorldEntity,
            result.targetId,
            result.targetGeneration,
            result.targetMapEpoch};
        if (!admissionAlreadyRecorded && !admissionRecorded)
        {
            std::uint64_t ignoredRevision = 0;
            const combat::CombatHitAdmission admission = ledger_->RecordHit(
                sourceAction, target, result.hitOrdinal, ignoredRevision);
            if (admission == combat::CombatHitAdmission::UnknownAction)
            {
                outcome.disposition = CombatHitAdmissionDisposition::Deferred;
                return outcome;
            }
            if (admission == combat::CombatHitAdmission::Duplicate ||
                admission == combat::CombatHitAdmission::Invalid ||
                admission == combat::CombatHitAdmission::StaleLifecycle)
            {
                return outcome;
            }
            admissionRecorded = true;
        }
        if (!appliedRevisions_->CanApply(result, authorityConnectionNonce))
        {
            return outcome;
        }
        if (!applicator_->Apply(result))
        {
            outcome.disposition = CombatHitAdmissionDisposition::Deferred;
            return outcome;
        }
        appliedRevisions_->MarkApplied(result, authorityConnectionNonce);
        return outcome;
    }
}
