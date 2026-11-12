#include "QuestMutationReplication.h"

#include "Multiplayer/Protocol/QuestMutationMessage.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolveQuestMutationSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.world.questMutations;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkQuestMutation,
    0x1012u,
    "quest-mutation",
    815u,
    "multiplayer-quest-mutation-dispatch",
    ResolveQuestMutationSink);

namespace fable::multiplayer::persistence
{
    void QuestMutationReplication::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        UdpPeer& transport,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        diagnostics_ = diagnostics;
    }

    void QuestMutationReplication::Shutdown() noexcept
    {
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        hostActorId_ = 0;
        authorityEpoch_ = 0;
        sessionRevision_ = 0;
        campaignRevision_ = 0;
        nextMutationId_ = 0;
        baselineBound_ = false;
        validator_ = nullptr;
        validatorContext_ = nullptr;
        apply_ = nullptr;
        applyContext_ = nullptr;
        commitSink_ = nullptr;
        commitContext_ = nullptr;
        repairSink_ = nullptr;
        repairContext_ = nullptr;
        authority_.Reset(0, 0, 0);
        inboundProposals_.clear();
        inboundRepairRequests_.clear();
        inboundCommits_.clear();
        localProposals_.clear();
        outbound_.clear();
        awaitingProposal_ = {};
        awaitingCommit_ = false;
        hostRepairPending_ = false;
        guestRepairPending_ = false;
        guestRepairRequestQueued_ = false;
        guestRepairRequest_ = {};
        preBaselineOverflowCommit_ = {};
        preBaselineOverflowSourceActorId_ = 0;
    }

    bool QuestMutationReplication::BindHostCampaign(
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t campaignRevision) noexcept
    {
        if (authorityEpoch == 0 || sessionRevision == 0 ||
            campaignRevision == 0 || role_ != PeerRole::Host ||
            localActorId_ == 0)
        {
            return false;
        }
        authorityEpoch_ = authorityEpoch;
        sessionRevision_ = sessionRevision;
        campaignRevision_ = campaignRevision;
        hostActorId_ = localActorId_;
        baselineBound_ = true;
        inboundProposals_.clear();
        inboundRepairRequests_.clear();
        inboundCommits_.clear();
        localProposals_.clear();
        outbound_.clear();
        awaitingProposal_ = {};
        awaitingCommit_ = false;
        hostRepairPending_ = false;
        guestRepairPending_ = false;
        guestRepairRequestQueued_ = false;
        guestRepairRequest_ = {};
        preBaselineOverflowCommit_ = {};
        preBaselineOverflowSourceActorId_ = 0;
        authority_.Reset(
            authorityEpoch_, sessionRevision_, campaignRevision_);
        authority_.SetValidator(validator_, validatorContext_);
        authority_.SetApplySink(apply_, applyContext_);
        return true;
    }

    bool QuestMutationReplication::BindGuestAppliedCampaign(
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t campaignRevision,
        const std::uint64_t hostActorId) noexcept
    {
        if (role_ != PeerRole::Guest || authorityEpoch == 0 ||
            sessionRevision == 0 || campaignRevision == 0 || hostActorId == 0)
        {
            return false;
        }
        const bool repairPreBaselineOverflow =
            !preBaselineOverflowCommit_.questName.empty() &&
            preBaselineOverflowSourceActorId_ == hostActorId &&
            preBaselineOverflowCommit_.authorityEpoch == authorityEpoch &&
            preBaselineOverflowCommit_.sessionRevision == sessionRevision;
        const protocol::QuestMutationMessage overflowCommit =
            preBaselineOverflowCommit_;
        preBaselineOverflowCommit_ = {};
        preBaselineOverflowSourceActorId_ = 0;

        // Snapshot and mutation delivery use independent reliable streams.
        // Retain only commits that arrived early from this exact host/fence;
        // nothing is applied until this accepted snapshot is bound.
        for (auto iterator = inboundCommits_.begin();
             iterator != inboundCommits_.end();)
        {
            if (iterator->sourceActorId != hostActorId ||
                iterator->message.authorityEpoch != authorityEpoch ||
                iterator->message.sessionRevision != sessionRevision)
            {
                iterator = inboundCommits_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        authorityEpoch_ = authorityEpoch;
        sessionRevision_ = sessionRevision;
        campaignRevision_ = campaignRevision;
        hostActorId_ = hostActorId;
        baselineBound_ = true;
        inboundProposals_.clear();
        inboundRepairRequests_.clear();
        localProposals_.clear();
        outbound_.clear();
        awaitingProposal_ = {};
        awaitingCommit_ = false;
        guestRepairPending_ = false;
        guestRepairRequestQueued_ = false;
        guestRepairRequest_ = {};
        if (repairPreBaselineOverflow)
        {
            BeginGuestRepair(overflowCommit);
        }
        return true;
    }

    void QuestMutationReplication::SetValidator(
        const ValidateSink sink,
        void* const context) noexcept
    {
        validator_ = sink;
        validatorContext_ = context;
        authority_.SetValidator(sink, context);
    }

    void QuestMutationReplication::SetApplySink(
        const ApplySink sink,
        void* const context) noexcept
    {
        apply_ = sink;
        applyContext_ = context;
        authority_.SetApplySink(sink, context);
    }

    void QuestMutationReplication::SetCommitSink(
        const CommitSink sink,
        void* const context) noexcept
    {
        commitSink_ = sink;
        commitContext_ = context;
    }

    void QuestMutationReplication::SetRepairSink(
        const RepairSink sink,
        void* const context) noexcept
    {
        repairSink_ = sink;
        repairContext_ = context;
    }

    bool QuestMutationReplication::ObserveLocalMutation(
        const protocol::QuestMutationOperation operation,
        const std::uint16_t mapId,
        const std::uint32_t mapEpoch,
        const char* const questName,
        const std::array<std::uint32_t,
            protocol::QuestMutationArgumentCount>& arguments)
    {
        if (!baselineBound_ || transport_ == nullptr ||
            localActorId_ == 0 || questName == nullptr)
        {
            return false;
        }
        protocol::QuestMutationMessage proposal = MakeProposal(
            operation, mapId, mapEpoch, questName, arguments);
        if (!protocol::IsSaneQuestMutationMessage(proposal))
        {
            return false;
        }
        if (role_ == PeerRole::Guest)
        {
            if (guestRepairPending_)
            {
                // The native mutation already happened, but a prior event
                // cannot be represented on the campaign lane. Stay fenced
                // until an authoritative snapshot is applied.
                return true;
            }
            if (localProposals_.size() >= QueueCapacity)
            {
                Report("MultiplayerQuestMutationBackpressured",
                    "guest proposal queue is full");
                BeginGuestRepair(proposal);
                return true;
            }
            localProposals_.push_back(std::move(proposal));
            return true;
        }
        if (outbound_.size() >= QueueCapacity)
        {
            Report("MultiplayerQuestMutationBackpressured",
                "host commit queue is full");
            EmitHostRepair(
                protocol::QuestMutationRejectionReason::QueueOverflow);
            return true;
        }
        protocol::QuestMutationMessage commit;
        const QuestMutationAdmission result =
            authority_.AdmitObserved(proposal, commit);
        if (result != QuestMutationAdmission::Accepted &&
            result != QuestMutationAdmission::Duplicate)
        {
            return false;
        }
        campaignRevision_ = authority_.CampaignRevision();
        if (result == QuestMutationAdmission::Accepted)
        {
            NotifyCommit(commit, true);
        }
        if (!QueueOutbound(commit))
        {
            EmitHostRepair(
                protocol::QuestMutationRejectionReason::QueueOverflow);
        }
        return true;
    }

    bool QuestMutationReplication::RequestRepair(
        const protocol::QuestMutationOperation operation,
        const std::uint16_t mapId,
        const std::uint32_t mapEpoch,
        const char* const questName,
        const protocol::QuestMutationRejectionReason reason)
    {
        if (role_ != PeerRole::Guest || !baselineBound_ ||
            questName == nullptr ||
            reason == protocol::QuestMutationRejectionReason::None ||
            reason > protocol::QuestMutationRejectionReason::QueueOverflow)
        {
            return false;
        }
        const std::array<std::uint32_t,
            protocol::QuestMutationArgumentCount> arguments = {};
        protocol::QuestMutationMessage request = MakeProposal(
            operation, mapId, mapEpoch, questName, arguments);
        if (!protocol::IsSaneQuestMutationMessage(request))
        {
            return false;
        }
        BeginGuestRepair(request, reason);
        return true;
    }

    bool QuestMutationReplication::Process()
    {
        if (transport_ == nullptr)
        {
            return false;
        }
        if (!baselineBound_)
        {
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            if (!ProcessHostProposals())
            {
                return false;
            }
        }
        else
        {
            if (!ProcessGuestCommits() || !StartNextGuestProposal())
            {
                return false;
            }
        }
        return PublishOne();
    }

    bool QuestMutationReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (transportMessage.type != protocol::PacketType::QuestMutation ||
            transportMessage.streamId != reliable_stream::WorldLifecycle)
        {
            return false;
        }
        protocol::QuestMutationMessage message;
        if (!protocol::DecodeQuestMutationMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            return false;
        }
        if (!baselineBound_)
        {
            if (role_ != PeerRole::Guest ||
                message.phase == protocol::QuestMutationPhase::Proposal ||
                transportMessage.sourceActorId == 0)
            {
                return false;
            }
            if (inboundCommits_.size() >= QueueCapacity)
            {
                if (preBaselineOverflowCommit_.questName.empty())
                {
                    preBaselineOverflowCommit_ = message;
                    preBaselineOverflowSourceActorId_ =
                        transportMessage.sourceActorId;
                }
                Report("MultiplayerQuestMutationBackpressured",
                    "pre-baseline commit queue overflow; snapshot repair is pending");
                return true;
            }
            inboundCommits_.push_back(
                {std::move(message), transportMessage.sourceActorId});
            return true;
        }
        if (message.authorityEpoch != authorityEpoch_ ||
            message.sessionRevision != sessionRevision_)
        {
            return false;
        }
        if (message.phase == protocol::QuestMutationPhase::Proposal)
        {
            if (role_ != PeerRole::Host ||
                transportMessage.sourceActorId != message.initiatorActorId)
            {
                return false;
            }
            if (inboundProposals_.size() >= QueueCapacity)
            {
                hostRepairPending_ = true;
                Report("MultiplayerQuestMutationBackpressured",
                    "host proposal queue overflow; authoritative repair is pending");
                return true;
            }
            inboundProposals_.push_back(std::move(message));
            return true;
        }
        if (message.phase == protocol::QuestMutationPhase::RepairNeeded &&
            role_ == PeerRole::Host)
        {
            if (transportMessage.sourceActorId != message.initiatorActorId)
            {
                return false;
            }
            if (inboundRepairRequests_.size() >= QueueCapacity)
            {
                hostRepairPending_ = true;
                return true;
            }
            inboundRepairRequests_.push_back(std::move(message));
            return true;
        }
        if (role_ != PeerRole::Guest ||
            transportMessage.sourceActorId != hostActorId_)
        {
            return false;
        }
        if (guestRepairPending_)
        {
            // Later commits are superseded by the requested snapshot.
            return true;
        }
        if (inboundCommits_.size() >= QueueCapacity)
        {
            BeginGuestRepair(message);
            Report("MultiplayerQuestMutationBackpressured",
                "guest commit queue overflow; authoritative repair is pending");
            return true;
        }
        inboundCommits_.push_back(
            {std::move(message), transportMessage.sourceActorId});
        return true;
    }

    protocol::QuestMutationMessage QuestMutationReplication::MakeProposal(
        const protocol::QuestMutationOperation operation,
        const std::uint16_t mapId,
        const std::uint32_t mapEpoch,
        const char* const questName,
        const std::array<std::uint32_t,
            protocol::QuestMutationArgumentCount>& arguments)
    {
        protocol::QuestMutationMessage proposal;
        proposal.phase = protocol::QuestMutationPhase::Proposal;
        proposal.operation = operation;
        proposal.authorityEpoch = authorityEpoch_;
        proposal.sessionRevision = sessionRevision_;
        proposal.expectedCampaignRevision = campaignRevision_;
        ++nextMutationId_;
        if (nextMutationId_ == 0)
        {
            ++nextMutationId_;
        }
        proposal.mutationId = nextMutationId_ ^
            (localActorId_ * 0x9E3779B97F4A7C15ull);
        if (proposal.mutationId == 0)
        {
            proposal.mutationId = nextMutationId_;
        }
        proposal.initiatorActorId = localActorId_;
        proposal.mapId = mapId;
        proposal.mapEpoch = mapEpoch;
        proposal.arguments = arguments;
        proposal.questName = questName;
        return proposal;
    }

    bool QuestMutationReplication::ProcessHostProposals()
    {
        if (hostRepairPending_ && repairSink_ != nullptr &&
            inboundProposals_.size() < QueueCapacity &&
            inboundRepairRequests_.size() < QueueCapacity)
        {
            hostRepairPending_ = false;
            EmitHostRepair(
                protocol::QuestMutationRejectionReason::QueueOverflow);
            return true;
        }
        if (!inboundRepairRequests_.empty())
        {
            const protocol::QuestMutationRejectionReason reason =
                inboundRepairRequests_.front().rejectionReason;
            inboundRepairRequests_.pop_front();
            EmitHostRepair(reason);
            return true;
        }
        if (inboundProposals_.empty() || outbound_.size() >= QueueCapacity)
        {
            return true;
        }
        protocol::QuestMutationMessage& pending = inboundProposals_.front();
        protocol::QuestMutationMessage commit;
        const QuestMutationAdmission result = authority_.Admit(
            pending, commit);
        if (result == QuestMutationAdmission::ApplyFailed ||
            result == QuestMutationAdmission::StorageFailed)
        {
            return true;
        }
        if (result != QuestMutationAdmission::Accepted &&
            result != QuestMutationAdmission::Duplicate)
        {
            const protocol::QuestMutationMessage repair =
                MakeRepairNeeded(pending, result);
            inboundProposals_.pop_front();
            if (!QueueOutbound(repair))
            {
                EmitHostRepair(
                    protocol::QuestMutationRejectionReason::QueueOverflow);
                return true;
            }
            if (repairSink_ != nullptr)
            {
                repairSink_(repairContext_, repair.campaignRevision,
                    repair.rejectionReason);
            }
            Report("MultiplayerQuestMutationRejected",
                "host rejected a fenced quest proposal and requested repair");
            return true;
        }
        inboundProposals_.pop_front();
        campaignRevision_ = authority_.CampaignRevision();
        if (result == QuestMutationAdmission::Accepted)
        {
            NotifyCommit(commit, false);
        }
        if (!QueueOutbound(commit))
        {
            EmitHostRepair(
                protocol::QuestMutationRejectionReason::QueueOverflow);
        }
        return true;
    }

    bool QuestMutationReplication::ProcessGuestCommits()
    {
        if (guestRepairPending_)
        {
            if (!guestRepairRequestQueued_ &&
                QueueOutbound(guestRepairRequest_))
            {
                guestRepairRequestQueued_ = true;
                Report("MultiplayerQuestMutationRepairRequested",
                    "guest queued an authoritative snapshot repair request");
            }
            return true;
        }
        if (inboundCommits_.empty())
        {
            return true;
        }
        const protocol::QuestMutationMessage& commit =
            inboundCommits_.front().message;
        if (commit.phase == protocol::QuestMutationPhase::RepairNeeded)
        {
            const std::uint64_t hostRevision = commit.campaignRevision;
            const protocol::QuestMutationRejectionReason reason =
                commit.rejectionReason;
            inboundCommits_.pop_front();
            // Any host repair invalidates queued work, even if its response
            // arrived after the proposal was superseded locally. The host is
            // already producing the snapshot, so do not echo another request.
            guestRepairPending_ = true;
            guestRepairRequestQueued_ = true;
            guestRepairRequest_ = {};
            inboundCommits_.clear();
            localProposals_.clear();
            outbound_.clear();
            awaitingProposal_ = {};
            awaitingCommit_ = false;
            if (repairSink_ != nullptr)
            {
                repairSink_(repairContext_, hostRevision, reason);
            }
            Report("MultiplayerQuestMutationRepairRequested",
                "host requested an authoritative quest snapshot repair");
            return true;
        }
        if (commit.campaignRevision <= campaignRevision_)
        {
            inboundCommits_.pop_front();
            return true;
        }
        if (commit.expectedCampaignRevision != campaignRevision_)
        {
            Report("MultiplayerQuestMutationGap",
                "commit is waiting for a missing campaign revision");
            BeginGuestRepair(commit,
                protocol::QuestMutationRejectionReason::FutureRevision);
            return true;
        }
        const bool localOrigin = commit.initiatorActorId == localActorId_;
        if (!localOrigin && apply_ != nullptr &&
            !apply_(applyContext_, commit))
        {
            return true;
        }
        campaignRevision_ = commit.campaignRevision;
        NotifyCommit(commit, localOrigin);
        if (localOrigin && awaitingCommit_ &&
            awaitingProposal_.mutationId == commit.mutationId)
        {
            awaitingProposal_ = {};
            awaitingCommit_ = false;
        }
        inboundCommits_.pop_front();
        return true;
    }

    bool QuestMutationReplication::StartNextGuestProposal()
    {
        if (guestRepairPending_ || awaitingCommit_ || localProposals_.empty())
        {
            return true;
        }
        if (outbound_.size() >= QueueCapacity)
        {
            return true;
        }
        awaitingProposal_ = std::move(localProposals_.front());
        localProposals_.pop_front();
        awaitingProposal_.expectedCampaignRevision = campaignRevision_;
        awaitingProposal_.campaignRevision = 0;
        awaitingCommit_ = true;
        if (!QueueOutbound(awaitingProposal_))
        {
            BeginGuestRepair(awaitingProposal_);
        }
        return true;
    }

    bool QuestMutationReplication::PublishOne()
    {
        if (outbound_.empty())
        {
            return true;
        }
        std::array<std::uint8_t,
            protocol::MaximumReliableMessageBytes> payload = {};
        std::size_t payloadSize = 0;
        if (!protocol::EncodeQuestMutationMessage(
                outbound_.front(), payload.data(), payload.size(),
                payloadSize))
        {
            return false;
        }
        if (!transport_->SubmitReliable(
                reliable_stream::WorldLifecycle,
                protocol::PacketType::QuestMutation,
                payload.data(), payloadSize))
        {
            return true;
        }
        outbound_.pop_front();
        return true;
    }

    protocol::QuestMutationMessage
        QuestMutationReplication::MakeRepairNeeded(
            const protocol::QuestMutationMessage& proposal,
            const QuestMutationAdmission admission) const
    {
        protocol::QuestMutationMessage repair = proposal;
        repair.phase = protocol::QuestMutationPhase::RepairNeeded;
        repair.campaignRevision = campaignRevision_;
        using Reason = protocol::QuestMutationRejectionReason;
        switch (admission)
        {
        case QuestMutationAdmission::WrongAuthority:
            repair.rejectionReason = Reason::WrongAuthority;
            break;
        case QuestMutationAdmission::Stale:
            repair.rejectionReason = Reason::StaleRevision;
            break;
        case QuestMutationAdmission::Future:
            repair.rejectionReason = Reason::FutureRevision;
            break;
        case QuestMutationAdmission::ConflictingDuplicate:
            repair.rejectionReason = Reason::ConflictingMutation;
            break;
        case QuestMutationAdmission::RevisionExhausted:
            repair.rejectionReason = Reason::RevisionExhausted;
            break;
        default:
            repair.rejectionReason = Reason::PolicyRejected;
            break;
        }
        return repair;
    }

    void QuestMutationReplication::BeginGuestRepair(
        const protocol::QuestMutationMessage& context,
        const protocol::QuestMutationRejectionReason reason)
    {
        if (role_ != PeerRole::Guest || !baselineBound_ ||
            guestRepairPending_)
        {
            return;
        }
        const std::array<std::uint32_t,
            protocol::QuestMutationArgumentCount> arguments = {};
        guestRepairRequest_ = MakeProposal(
            context.operation, context.mapId, context.mapEpoch,
            context.questName.c_str(), arguments);
        guestRepairRequest_.phase =
            protocol::QuestMutationPhase::RepairNeeded;
        guestRepairRequest_.campaignRevision = campaignRevision_;
        guestRepairRequest_.rejectionReason = reason;
        guestRepairPending_ = true;
        guestRepairRequestQueued_ = false;
        inboundCommits_.clear();
        localProposals_.clear();
        outbound_.clear();
        awaitingProposal_ = {};
        awaitingCommit_ = false;
        if (repairSink_ != nullptr)
        {
            repairSink_(repairContext_,
                (std::max)(campaignRevision_, context.campaignRevision),
                reason);
        }
        Report("MultiplayerQuestMutationRepairRequested",
            "guest fenced quest delivery pending an authoritative snapshot");
    }

    void QuestMutationReplication::EmitHostRepair(
        const protocol::QuestMutationRejectionReason reason) noexcept
    {
        if (repairSink_ == nullptr)
        {
            hostRepairPending_ = true;
            return;
        }
        repairSink_(repairContext_, campaignRevision_, reason);
        Report("MultiplayerQuestRepairRequested",
            "host queued an authoritative quest snapshot repair");
    }

    bool QuestMutationReplication::QueueOutbound(
        const protocol::QuestMutationMessage& message)
    {
        if (outbound_.size() >= QueueCapacity)
        {
            return false;
        }
        try
        {
            outbound_.push_back(message);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void QuestMutationReplication::NotifyCommit(
        const protocol::QuestMutationMessage& commit,
        const bool locallyOriginated) const noexcept
    {
        if (commitSink_ != nullptr)
        {
            commitSink_(commitContext_, commit, locallyOriginated);
        }
    }

    void QuestMutationReplication::Report(
        const char* const event,
        const char* const detail) const noexcept
    {
        if (diagnostics_.event != nullptr)
        {
            diagnostics_.Event(event, detail);
        }
    }
}
