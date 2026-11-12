#include "Multiplayer/Persistence/QuestMutationAuthority.h"
#include "Multiplayer/Persistence/QuestMutationReplication.h"
#include "Multiplayer/Protocol/QuestMutationMessage.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/ReliableStreamTransport.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* const detail)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "quest mutation: " << detail << '\n';
        }
    }

    fable::multiplayer::protocol::QuestMutationMessage Proposal(
        const std::uint64_t baseRevision = 7,
        const std::uint64_t mutationId = 100)
    {
        using namespace fable::multiplayer::protocol;
        QuestMutationMessage result;
        result.phase = QuestMutationPhase::Proposal;
        result.operation = QuestMutationOperation::Activate;
        result.authorityEpoch = 3;
        result.sessionRevision = 5;
        result.expectedCampaignRevision = baseRevision;
        result.mutationId = mutationId;
        result.initiatorActorId = 41;
        result.mapId = 11;
        result.mapEpoch = 13;
        result.arguments = {1, 1, 0};
        result.questName = "Q_GuildTraining";
        return result;
    }

    struct ApplyProbe final
    {
        std::uint32_t calls = 0;
        bool accept = true;
        std::uint64_t lastRevision = 0;
    };

    struct CommitProbe final
    {
        std::uint32_t calls = 0;
        std::uint64_t lastRevision = 0;
        bool locallyOriginated = true;
    };

    struct RepairProbe final
    {
        std::uint32_t calls = 0;
        std::uint64_t lastRevision = 0;
        fable::multiplayer::protocol::QuestMutationRejectionReason reason =
            fable::multiplayer::protocol::QuestMutationRejectionReason::None;
    };

    bool Validate(
        void*,
        const fable::multiplayer::protocol::QuestMutationMessage& proposal)
        noexcept
    {
        return proposal.questName != "Q_REJECT";
    }

    bool Apply(
        void* const context,
        const fable::multiplayer::protocol::QuestMutationMessage& commit)
        noexcept
    {
        auto* const probe = static_cast<ApplyProbe*>(context);
        if (probe == nullptr)
        {
            return false;
        }
        ++probe->calls;
        probe->lastRevision = commit.campaignRevision;
        return probe->accept;
    }

    void Commit(
        void* const context,
        const fable::multiplayer::protocol::QuestMutationMessage& commit,
        const bool locallyOriginated) noexcept
    {
        auto* const probe = static_cast<CommitProbe*>(context);
        if (probe != nullptr)
        {
            ++probe->calls;
            probe->lastRevision = commit.campaignRevision;
            probe->locallyOriginated = locallyOriginated;
        }
    }

    void Repair(
        void* const context,
        const std::uint64_t hostCampaignRevision,
        const fable::multiplayer::protocol::QuestMutationRejectionReason reason)
        noexcept
    {
        auto* const probe = static_cast<RepairProbe*>(context);
        if (probe != nullptr)
        {
            ++probe->calls;
            probe->lastRevision = hostCampaignRevision;
            probe->reason = reason;
        }
    }

    fable::multiplayer::TransportMessage Transport(
        const fable::multiplayer::protocol::QuestMutationMessage& message,
        const std::uint64_t sourceActorId)
    {
        fable::multiplayer::TransportMessage transport;
        transport.type =
            fable::multiplayer::protocol::PacketType::QuestMutation;
        transport.sourceActorId = sourceActorId;
        transport.streamId =
            fable::multiplayer::reliable_stream::WorldLifecycle;
        fable::multiplayer::protocol::EncodeQuestMutationMessage(
            message, transport.payload.data(), transport.payload.size(),
            transport.payloadSize);
        return transport;
    }
}

int RunQuestMutationTests()
{
    using namespace fable::multiplayer::persistence;
    using namespace fable::multiplayer::protocol;

    std::array<std::uint8_t, 256> bytes = {};
    std::size_t encodedSize = 0;
    QuestMutationMessage proposal = Proposal();
    Check(EncodeQuestMutationMessage(
        proposal, bytes.data(), bytes.size(), encodedSize),
        "proposal encodes");
    Check(encodedSize == 196, "wire message has fixed bounded size");
    QuestMutationMessage decoded;
    Check(DecodeQuestMutationMessage(
        bytes.data(), encodedSize, decoded),
        "proposal decodes");
    Check(SameQuestMutationProposal(proposal, decoded),
        "proposal fields round trip");

    QuestMutationMessage bad = proposal;
    bad.campaignRevision = 8;
    Check(!EncodeQuestMutationMessage(
        bad, bytes.data(), bytes.size(), encodedSize),
        "proposal cannot claim an authoritative revision");
    bad = proposal;
    bad.operation = QuestMutationOperation::Deactivate;
    bad.arguments = {1, 1, 0};
    Check(!EncodeQuestMutationMessage(
        bad, bytes.data(), bytes.size(), encodedSize),
        "unused deactivate arguments must be zero");
    Check(!DecodeQuestMutationMessage(bytes.data(), 195, decoded),
        "truncated payload is rejected");
    QuestMutationMessage repair = proposal;
    repair.phase = QuestMutationPhase::RepairNeeded;
    repair.campaignRevision = 7;
    repair.rejectionReason = QuestMutationRejectionReason::StaleRevision;
    Check(EncodeQuestMutationMessage(
            repair, bytes.data(), bytes.size(), encodedSize) &&
        DecodeQuestMutationMessage(bytes.data(), encodedSize, decoded) &&
        decoded.phase == QuestMutationPhase::RepairNeeded &&
        decoded.rejectionReason ==
            QuestMutationRejectionReason::StaleRevision,
        "repair response carries the authoritative campaign fence");

    QuestMutationAuthority authority;
    ApplyProbe probe;
    authority.Reset(3, 5, 7);
    authority.SetValidator(&Validate, nullptr);
    authority.SetApplySink(&Apply, &probe);
    QuestMutationMessage commit;
    Check(authority.Admit(proposal, commit) ==
        QuestMutationAdmission::Accepted,
        "matching proposal is accepted");
    Check(commit.phase == QuestMutationPhase::Commit &&
        commit.expectedCampaignRevision == 7 &&
        commit.campaignRevision == 8 &&
        authority.CampaignRevision() == 8 && probe.calls == 1,
        "host assigns and applies exactly one campaign revision");

    QuestMutationMessage duplicateCommit;
    Check(authority.Admit(proposal, duplicateCommit) ==
        QuestMutationAdmission::Duplicate &&
        duplicateCommit.campaignRevision == 8 && probe.calls == 1,
        "duplicate returns the original commit without reapplying");
    QuestMutationMessage conflict = proposal;
    conflict.questName = "Q_AnotherQuest";
    Check(authority.Admit(conflict, commit) ==
        QuestMutationAdmission::ConflictingDuplicate,
        "reused mutation id with different semantics is rejected");
    Check(authority.Admit(Proposal(7, 101), commit) ==
        QuestMutationAdmission::Stale,
        "stale campaign base is rejected");
    Check(authority.Admit(Proposal(9, 102), commit) ==
        QuestMutationAdmission::Future,
        "future campaign base is rejected");

    QuestMutationMessage rejected = Proposal(8, 103);
    rejected.questName = "Q_REJECT";
    Check(authority.Admit(rejected, commit) ==
        QuestMutationAdmission::Rejected &&
        authority.CampaignRevision() == 8 && probe.calls == 1,
        "semantic validator may reject without advancing state");

    probe.accept = false;
    Check(authority.Admit(Proposal(8, 104), commit) ==
        QuestMutationAdmission::ApplyFailed &&
        authority.CampaignRevision() == 8 && probe.calls == 2,
        "failed native application does not advance state");
    probe.accept = true;
    Check(authority.Admit(Proposal(8, 104), commit) ==
        QuestMutationAdmission::Accepted &&
        authority.CampaignRevision() == 9 && probe.calls == 3,
        "failed application can be retried with the same mutation id");

    QuestMutationMessage wrongAuthority = Proposal(9, 105);
    wrongAuthority.sessionRevision = 6;
    Check(authority.Admit(wrongAuthority, commit) ==
        QuestMutationAdmission::WrongAuthority,
        "session fence rejects an older or foreign campaign");

    ApplyProbe observedProbe;
    QuestMutationAuthority observedAuthority;
    observedAuthority.Reset(3, 5, 7);
    observedAuthority.SetApplySink(&Apply, &observedProbe);
    Check(observedAuthority.AdmitObserved(Proposal(), commit) ==
        QuestMutationAdmission::Accepted && observedProbe.calls == 0 &&
        commit.campaignRevision == 8,
        "host-observed mutation commits without replaying native apply");

    Check(EncodeQuestMutationMessage(
        proposal, bytes.data(), bytes.size(), encodedSize),
        "proposal re-encodes for stream validation");
    Check(fable::multiplayer::ReliableStreamTransport::IsMessageValid(
        fable::multiplayer::reliable_stream::WorldLifecycle,
        PacketType::QuestMutation, bytes.data(), encodedSize),
        "quest mutations use the ordered world-lifecycle stream");

    // A campaign revision is never guessed from snapshotRevision. Integration
    // binds mutation delivery only after the authoritative snapshot applied.
    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        ApplyProbe earlyApply;
        CommitProbe earlyCommit;
        replication.Initialize(
            fable::multiplayer::PeerRole::Guest, 41, transport, {});
        replication.SetApplySink(&Apply, &earlyApply);
        replication.SetCommitSink(&Commit, &earlyCommit);
        Check(replication.Process(),
            "waiting for baseline is a harmless delivery state");
        QuestMutationMessage early = Proposal(17, 201);
        early.phase = QuestMutationPhase::Commit;
        early.campaignRevision = 18;
        early.initiatorActorId = 52;
        Check(replication.HandleReliableMessage(Transport(early, 77)) &&
            earlyApply.calls == 0,
            "commit arriving before snapshot application is buffered");
        Check(replication.BindGuestAppliedCampaign(
                3, 5, 17, 77) &&
            replication.CampaignRevision() == 17,
            "applied quest snapshot binds its explicit campaign revision");
        Check(replication.Process() && earlyApply.calls == 1 &&
            earlyCommit.calls == 1 &&
            replication.CampaignRevision() == 18,
            "matching buffered commit applies after snapshot readiness");
        replication.Shutdown();
    }

    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Guest, 41, transport, {});
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindGuestAppliedCampaign(3, 5, 7, 77),
            "guest local proposal overflow test binds campaign");
        const std::array<std::uint32_t,
            QuestMutationArgumentCount> arguments = {1, 1, 0};
        bool accepted = true;
        for (std::size_t i = 0; i < 65; ++i)
        {
            accepted = accepted && replication.ObserveLocalMutation(
                QuestMutationOperation::Activate, 11, 13,
                "Q_GuildTraining", arguments);
        }
        Check(accepted && repairProbe.calls == 1 &&
            repairProbe.reason == QuestMutationRejectionReason::QueueOverflow &&
            replication.Process(),
            "guest local saturation fences delivery and queues snapshot repair");
        replication.Shutdown();
    }

    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        ApplyProbe applyProbe;
        CommitProbe commitProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Host, 77, transport, {});
        replication.SetValidator(&Validate, nullptr);
        replication.SetApplySink(&Apply, &applyProbe);
        replication.SetCommitSink(&Commit, &commitProbe);
        Check(replication.BindHostCampaign(3, 5, 7),
            "host binds the explicit quest campaign baseline");
        const fable::multiplayer::TransportMessage incoming =
            Transport(Proposal(), 41);
        Check(incoming.payloadSize != 0 &&
            replication.HandleReliableMessage(incoming) &&
            replication.Process() && applyProbe.calls == 1 &&
            commitProbe.calls == 1 &&
            commitProbe.lastRevision == 8 &&
            !commitProbe.locallyOriginated,
            "guest proposal applies once and commits as remote-originated");
        replication.Shutdown();
    }

    // A packet already consumed from the reliable stream must not be rejected
    // just because the application proposal queue is full. The host records
    // bounded repair state and emits it once queue capacity returns.
    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Host, 77, transport, {});
        replication.SetValidator(&Validate, nullptr);
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindHostCampaign(3, 5, 7),
            "host proposal overflow test binds campaign");
        bool queued = true;
        for (std::uint64_t i = 0; i < 64; ++i)
        {
            queued = queued && replication.HandleReliableMessage(
                Transport(Proposal(7, 1000 + i), 41));
        }
        Check(queued && replication.HandleReliableMessage(
                Transport(Proposal(7, 1064), 41)),
            "overflowing host proposal is consumed and fenced for repair");
        Check(replication.Process() && replication.Process() &&
            repairProbe.calls == 1 && repairProbe.lastRevision == 8 &&
            repairProbe.reason == QuestMutationRejectionReason::QueueOverflow,
            "host emits one authoritative repair after proposal capacity returns");
        replication.Shutdown();
    }

    // Guest commit overflow and a revision gap both fence the lane, request a
    // snapshot, and resume only from a newly applied campaign baseline.
    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        ApplyProbe applyProbe;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Guest, 41, transport, {});
        replication.SetApplySink(&Apply, &applyProbe);
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindGuestAppliedCampaign(3, 5, 7, 77),
            "guest commit overflow test binds campaign");
        bool queued = true;
        for (std::uint64_t i = 0; i < 64; ++i)
        {
            QuestMutationMessage commitMessage = Proposal(7 + i, 2000 + i);
            commitMessage.phase = QuestMutationPhase::Commit;
            commitMessage.campaignRevision = 8 + i;
            commitMessage.initiatorActorId = 52;
            queued = queued && replication.HandleReliableMessage(
                Transport(commitMessage, 77));
        }
        QuestMutationMessage overflowCommit = Proposal(71, 2064);
        overflowCommit.phase = QuestMutationPhase::Commit;
        overflowCommit.campaignRevision = 72;
        overflowCommit.initiatorActorId = 52;
        Check(queued && replication.HandleReliableMessage(
                Transport(overflowCommit, 77)) && repairProbe.calls == 1 &&
            repairProbe.reason == QuestMutationRejectionReason::QueueOverflow,
            "overflowing guest commit is consumed and triggers snapshot repair");
        Check(replication.Process() && applyProbe.calls == 0,
            "guest does not apply commits while repair snapshot is pending");
        Check(replication.BindGuestAppliedCampaign(3, 5, 72, 77),
            "new snapshot baseline clears the guest overflow fence");
        QuestMutationMessage afterRepair = Proposal(72, 3000);
        afterRepair.phase = QuestMutationPhase::Commit;
        afterRepair.campaignRevision = 73;
        afterRepair.initiatorActorId = 52;
        Check(replication.HandleReliableMessage(Transport(afterRepair, 77)) &&
            replication.Process() && applyProbe.calls == 1 &&
            replication.CampaignRevision() == 73,
            "guest resumes ordered commit application after snapshot repair");
        replication.Shutdown();
    }

    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        ApplyProbe applyProbe;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Guest, 41, transport, {});
        replication.SetApplySink(&Apply, &applyProbe);
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindGuestAppliedCampaign(3, 5, 7, 77),
            "guest gap test binds campaign");
        QuestMutationMessage gap = Proposal(8, 4000);
        gap.phase = QuestMutationPhase::Commit;
        gap.campaignRevision = 9;
        gap.initiatorActorId = 52;
        Check(replication.HandleReliableMessage(Transport(gap, 77)) &&
            replication.Process() && applyProbe.calls == 0 &&
            repairProbe.calls == 1 && repairProbe.lastRevision == 9 &&
            repairProbe.reason == QuestMutationRejectionReason::FutureRevision,
            "guest commit gap requests repair instead of waiting forever");
        Check(replication.BindGuestAppliedCampaign(3, 5, 9, 77),
            "guest gap repair binds fresh campaign baseline");
        QuestMutationMessage next = Proposal(9, 4001);
        next.phase = QuestMutationPhase::Commit;
        next.campaignRevision = 10;
        next.initiatorActorId = 52;
        Check(replication.HandleReliableMessage(Transport(next, 77)) &&
            replication.Process() && applyProbe.calls == 1 &&
            replication.CampaignRevision() == 10,
            "guest resumes after repairing a campaign revision gap");
        replication.Shutdown();
    }

    // Host-local retail changes can saturate the commit lane before another
    // native transition arrives. The unrepresentable local transition must
    // cause snapshot repair, not disappear behind a false return.
    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Host, 77, transport, {});
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindHostCampaign(3, 5, 7),
            "host local overflow test binds campaign");
        const std::array<std::uint32_t,
            QuestMutationArgumentCount> arguments = {1, 1, 0};
        bool accepted = true;
        for (std::size_t i = 0; i < 64; ++i)
        {
            accepted = accepted && replication.ObserveLocalMutation(
                QuestMutationOperation::Activate, 11, 13,
                "Q_GuildTraining", arguments);
        }
        Check(accepted && replication.ObserveLocalMutation(
                QuestMutationOperation::Activate, 11, 13,
                "Q_GuildTraining", arguments) &&
            repairProbe.calls == 1 && repairProbe.lastRevision == 71 &&
            repairProbe.reason == QuestMutationRejectionReason::QueueOverflow,
            "host local saturation advances no unpublishable revision and requests snapshot repair");
        replication.Shutdown();
    }

    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Guest, 41, transport, {});
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindGuestAppliedCampaign(3, 5, 7, 77),
            "unmatched repair test binds campaign");
        const std::array<std::uint32_t,
            QuestMutationArgumentCount> arguments = {1, 1, 0};
        Check(replication.ObserveLocalMutation(
                QuestMutationOperation::Activate, 11, 13,
                "Q_GuildTraining", arguments) && replication.Process(),
            "guest has a queued local proposal before late repair response");
        QuestMutationMessage repairMessage = Proposal(7, 5000);
        repairMessage.phase = QuestMutationPhase::RepairNeeded;
        repairMessage.campaignRevision = 9;
        repairMessage.rejectionReason =
            QuestMutationRejectionReason::StaleRevision;
        repairMessage.initiatorActorId = 52;
        Check(replication.HandleReliableMessage(
                Transport(repairMessage, 77)) && replication.Process() &&
            repairProbe.calls == 1 && repairProbe.lastRevision == 9 &&
            repairProbe.reason == QuestMutationRejectionReason::StaleRevision,
            "unmatched host repair response fences queued work and notifies repair sink");
        Check(replication.Process() && repairProbe.calls == 1 &&
            replication.ObserveLocalMutation(
                QuestMutationOperation::Activate, 11, 13,
                "Q_GuildTraining", arguments),
            "late repair does not trigger proposal retries or duplicate repair requests");
        replication.Shutdown();
    }

    {
        fable::multiplayer::UdpPeer transport;
        QuestMutationReplication replication;
        RepairProbe repairProbe;
        replication.Initialize(
            fable::multiplayer::PeerRole::Host, 77, transport, {});
        replication.SetValidator(&Validate, nullptr);
        replication.SetRepairSink(&Repair, &repairProbe);
        Check(replication.BindHostCampaign(3, 5, 7),
            "repair test binds host campaign");
        QuestMutationMessage rejectedProposal = Proposal();
        rejectedProposal.questName = "Q_REJECT";
        const fable::multiplayer::TransportMessage incoming =
            Transport(rejectedProposal, 41);
        Check(replication.HandleReliableMessage(incoming) &&
            replication.Process() && repairProbe.calls == 1 &&
            repairProbe.lastRevision == 7 &&
            repairProbe.reason ==
                QuestMutationRejectionReason::PolicyRejected,
            "host rejection requests an authoritative snapshot repair");
        QuestMutationMessage overflowRequest = Proposal(7, 302);
        overflowRequest.phase = QuestMutationPhase::RepairNeeded;
        overflowRequest.campaignRevision = 7;
        overflowRequest.rejectionReason =
            QuestMutationRejectionReason::QueueOverflow;
        Check(replication.HandleReliableMessage(
                Transport(overflowRequest, 41)) &&
            replication.Process() && repairProbe.calls == 2 &&
            repairProbe.reason == QuestMutationRejectionReason::QueueOverflow,
            "guest observer overflow requests an authoritative host snapshot");
        replication.Shutdown();
    }

    return failures;
}
