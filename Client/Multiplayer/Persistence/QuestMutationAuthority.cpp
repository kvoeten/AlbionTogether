#include "QuestMutationAuthority.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace fable::multiplayer::persistence
{
    void QuestMutationAuthority::Reset(
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t campaignRevision) noexcept
    {
        authorityEpoch_ = authorityEpoch;
        sessionRevision_ = sessionRevision;
        campaignRevision_ = campaignRevision;
        nextHistory_ = 0;
        for (AcceptedMutation& mutation : history_)
        {
            mutation = {};
        }
    }

    void QuestMutationAuthority::SetValidator(
        const ValidateSink sink,
        void* const context) noexcept
    {
        validator_ = sink;
        validatorContext_ = context;
    }

    void QuestMutationAuthority::SetApplySink(
        const ApplySink sink,
        void* const context) noexcept
    {
        apply_ = sink;
        applyContext_ = context;
    }

    QuestMutationAdmission QuestMutationAuthority::Admit(
        const protocol::QuestMutationMessage& proposal,
        protocol::QuestMutationMessage& commit) noexcept
    {
        return AdmitInternal(proposal, false, commit);
    }

    QuestMutationAdmission QuestMutationAuthority::AdmitObserved(
        const protocol::QuestMutationMessage& proposal,
        protocol::QuestMutationMessage& commit) noexcept
    {
        return AdmitInternal(proposal, true, commit);
    }

    QuestMutationAdmission QuestMutationAuthority::AdmitInternal(
        const protocol::QuestMutationMessage& proposal,
        const bool alreadyApplied,
        protocol::QuestMutationMessage& commit) noexcept
    {
        commit = {};
        if (!protocol::IsSaneQuestMutationMessage(proposal) ||
            proposal.phase != protocol::QuestMutationPhase::Proposal)
        {
            return QuestMutationAdmission::Invalid;
        }
        if (authorityEpoch_ == 0 || sessionRevision_ == 0 ||
            campaignRevision_ == 0 ||
            proposal.authorityEpoch != authorityEpoch_ ||
            proposal.sessionRevision != sessionRevision_)
        {
            return QuestMutationAdmission::WrongAuthority;
        }
        if (AcceptedMutation* const accepted = Find(
                proposal.initiatorActorId, proposal.mutationId))
        {
            if (!protocol::SameQuestMutationProposal(
                    accepted->proposal, proposal))
            {
                return QuestMutationAdmission::ConflictingDuplicate;
            }
            try
            {
                commit = accepted->commit;
            }
            catch (...)
            {
                commit = {};
                return QuestMutationAdmission::StorageFailed;
            }
            return QuestMutationAdmission::Duplicate;
        }
        if (proposal.expectedCampaignRevision < campaignRevision_)
        {
            return QuestMutationAdmission::Stale;
        }
        if (proposal.expectedCampaignRevision > campaignRevision_)
        {
            return QuestMutationAdmission::Future;
        }
        if (campaignRevision_ ==
            (std::numeric_limits<std::uint64_t>::max)())
        {
            return QuestMutationAdmission::RevisionExhausted;
        }
        if (validator_ != nullptr &&
            !validator_(validatorContext_, proposal))
        {
            return QuestMutationAdmission::Rejected;
        }

        AcceptedMutation pending;
        try
        {
            commit = proposal;
            commit.phase = protocol::QuestMutationPhase::Commit;
            commit.campaignRevision = campaignRevision_ + 1;
            pending.proposal = proposal;
            pending.commit = commit;
            pending.present = true;
        }
        catch (...)
        {
            commit = {};
            return QuestMutationAdmission::StorageFailed;
        }
        if (!alreadyApplied && apply_ != nullptr &&
            !apply_(applyContext_, commit))
        {
            commit = {};
            return QuestMutationAdmission::ApplyFailed;
        }
        campaignRevision_ = commit.campaignRevision;
        Remember(std::move(pending));
        return QuestMutationAdmission::Accepted;
    }

    QuestMutationAuthority::AcceptedMutation* QuestMutationAuthority::Find(
        const std::uint64_t initiatorActorId,
        const std::uint64_t mutationId) noexcept
    {
        for (AcceptedMutation& accepted : history_)
        {
            if (accepted.present &&
                accepted.proposal.initiatorActorId == initiatorActorId &&
                accepted.proposal.mutationId == mutationId)
            {
                return &accepted;
            }
        }
        return nullptr;
    }

    void QuestMutationAuthority::Remember(AcceptedMutation&& mutation) noexcept
    {
        static_assert(std::is_nothrow_move_assignable_v<AcceptedMutation>);
        history_[nextHistory_] = std::move(mutation);
        nextHistory_ = (nextHistory_ + 1) % history_.size();
    }
}
