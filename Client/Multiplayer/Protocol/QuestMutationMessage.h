#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class QuestMutationPhase : std::uint8_t
    {
        Proposal = 1,
        Commit = 2,
        // Host could not commit a proposal. The origin must discard pending
        // optimistic transitions and request an authoritative snapshot.
        RepairNeeded = 3,
    };

    enum class QuestMutationOperation : std::uint8_t
    {
        Activate = 1,
        Deactivate = 2,
        Complete = 3,
        Fail = 4,
    };

    enum class QuestMutationRejectionReason : std::uint8_t
    {
        None = 0,
        WrongAuthority = 1,
        StaleRevision = 2,
        FutureRevision = 3,
        ConflictingMutation = 4,
        PolicyRejected = 5,
        RevisionExhausted = 6,
        QueueOverflow = 7,
    };

    inline constexpr std::size_t MaximumQuestMutationNameBytes = 128;
    inline constexpr std::size_t QuestMutationArgumentCount = 3;

    // A semantic quest transition. Proposals describe the transition observed
    // by a player; only the host turns one into a Commit and assigns the next
    // campaign revision. Full quest state remains on the snapshot lane.
    struct QuestMutationMessage final
    {
        QuestMutationPhase phase = QuestMutationPhase::Proposal;
        QuestMutationOperation operation = QuestMutationOperation::Activate;
        QuestMutationRejectionReason rejectionReason =
            QuestMutationRejectionReason::None;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t sessionRevision = 0;
        std::uint64_t campaignRevision = 0;
        std::uint64_t expectedCampaignRevision = 0;
        std::uint64_t mutationId = 0;
        std::uint64_t initiatorActorId = 0;
        std::uint16_t mapId = 0;
        std::uint32_t mapEpoch = 0;
        std::array<std::uint32_t, QuestMutationArgumentCount> arguments = {};
        std::string questName;
    };

    [[nodiscard]] bool IsSaneQuestMutationMessage(
        const QuestMutationMessage& message) noexcept;
    [[nodiscard]] bool SameQuestMutationProposal(
        const QuestMutationMessage& left,
        const QuestMutationMessage& right) noexcept;
    bool EncodeQuestMutationMessage(
        const QuestMutationMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeQuestMutationMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        QuestMutationMessage& message) noexcept;
}
