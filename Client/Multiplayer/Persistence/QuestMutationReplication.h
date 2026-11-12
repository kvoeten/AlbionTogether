#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Persistence/QuestMutationAuthority.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::persistence
{
    // Ordered semantic quest transitions. This service deliberately owns no
    // quest scanner and no opaque save bytes: QuestStateAuthorityService is
    // the separate baseline/repair lane.
    class QuestMutationReplication final : public ReliableMessageSink
    {
    public:
        using ValidateSink = QuestMutationAuthority::ValidateSink;
        using ApplySink = QuestMutationAuthority::ApplySink;
        using CommitSink = void (*)(
            void* context,
            const protocol::QuestMutationMessage& commit,
            bool locallyOriginated) noexcept;
        using RepairSink = void (*)(
            void* context,
            std::uint64_t hostCampaignRevision,
            protocol::QuestMutationRejectionReason reason) noexcept;

        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::QuestMutation};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        // Must be called from an accepted host quest snapshot/baseline. It is
        // the only way this service learns the campaign fence.
        bool BindHostCampaign(
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t campaignRevision) noexcept;
        // Guest integration calls this only after QuestStateAuthorityService
        // applied the accepted opaque snapshot. campaignRevision comes from
        // that snapshot's metadata, never from snapshotRevision.
        bool BindGuestAppliedCampaign(
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t campaignRevision,
            std::uint64_t hostActorId) noexcept;

        void SetValidator(ValidateSink sink, void* context) noexcept;
        void SetApplySink(ApplySink sink, void* context) noexcept;
        void SetCommitSink(CommitSink sink, void* context) noexcept;
        void SetRepairSink(RepairSink sink, void* context) noexcept;

        // Called after retail accepted a local transition. Host-local
        // transitions are committed without native replay. Guest transitions
        // become proposals and their eventual commit is confirmation only.
        bool ObserveLocalMutation(
            protocol::QuestMutationOperation operation,
            std::uint16_t mapId,
            std::uint32_t mapEpoch,
            const char* questName,
            const std::array<std::uint32_t,
                protocol::QuestMutationArgumentCount>& arguments);
        // A bounded local observer overflow cannot be treated as a skipped
        // event. Guests explicitly ask the host for a fresh campaign snapshot.
        bool RequestRepair(
            protocol::QuestMutationOperation operation,
            std::uint16_t mapId,
            std::uint32_t mapEpoch,
            const char* questName,
            protocol::QuestMutationRejectionReason reason);

        // Delivery/application pump only; it never discovers game state.
        bool Process();
        bool HandleReliableMessage(
            const TransportMessage& message) override;

        [[nodiscard]] std::uint64_t CampaignRevision() const noexcept
        {
            return campaignRevision_;
        }

    private:
        static constexpr std::size_t QueueCapacity = 64;

        struct InboundCommit final
        {
            protocol::QuestMutationMessage message;
            std::uint64_t sourceActorId = 0;
        };

        [[nodiscard]] protocol::QuestMutationMessage MakeProposal(
            protocol::QuestMutationOperation operation,
            std::uint16_t mapId,
            std::uint32_t mapEpoch,
            const char* questName,
            const std::array<std::uint32_t,
                protocol::QuestMutationArgumentCount>& arguments);
        bool ProcessHostProposals();
        bool ProcessGuestCommits();
        bool StartNextGuestProposal();
        bool PublishOne();
        bool QueueOutbound(const protocol::QuestMutationMessage& message);
        [[nodiscard]] protocol::QuestMutationMessage MakeRepairNeeded(
            const protocol::QuestMutationMessage& proposal,
            QuestMutationAdmission admission) const;
        void BeginGuestRepair(
            const protocol::QuestMutationMessage& context,
            protocol::QuestMutationRejectionReason reason =
                protocol::QuestMutationRejectionReason::QueueOverflow);
        void EmitHostRepair(
            protocol::QuestMutationRejectionReason reason) noexcept;
        void NotifyCommit(
            const protocol::QuestMutationMessage& commit,
            bool locallyOriginated) const noexcept;
        void Report(const char* event, const char* detail) const noexcept;

        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t hostActorId_ = 0;
        std::uint32_t authorityEpoch_ = 0;
        std::uint64_t sessionRevision_ = 0;
        std::uint64_t campaignRevision_ = 0;
        std::uint64_t nextMutationId_ = 0;
        bool baselineBound_ = false;
        ValidateSink validator_ = nullptr;
        void* validatorContext_ = nullptr;
        ApplySink apply_ = nullptr;
        void* applyContext_ = nullptr;
        CommitSink commitSink_ = nullptr;
        void* commitContext_ = nullptr;
        RepairSink repairSink_ = nullptr;
        void* repairContext_ = nullptr;
        QuestMutationAuthority authority_;
        std::deque<protocol::QuestMutationMessage> inboundProposals_;
        std::deque<protocol::QuestMutationMessage> inboundRepairRequests_;
        std::deque<InboundCommit> inboundCommits_;
        std::deque<protocol::QuestMutationMessage> localProposals_;
        std::deque<protocol::QuestMutationMessage> outbound_;
        protocol::QuestMutationMessage awaitingProposal_;
        bool awaitingCommit_ = false;
        bool hostRepairPending_ = false;
        bool guestRepairPending_ = false;
        bool guestRepairRequestQueued_ = false;
        protocol::QuestMutationMessage guestRepairRequest_;
        protocol::QuestMutationMessage preBaselineOverflowCommit_;
        std::uint64_t preBaselineOverflowSourceActorId_ = 0;
    };
}
