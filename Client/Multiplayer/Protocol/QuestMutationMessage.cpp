#include "QuestMutationMessage.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireQuestMutationMessage final
    {
        std::uint8_t phase = 0;
        std::uint8_t operation = 0;
        std::uint8_t rejectionReason = 0;
        std::uint8_t reserved = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t sessionRevision = 0;
        std::uint64_t campaignRevision = 0;
        std::uint64_t expectedCampaignRevision = 0;
        std::uint64_t mutationId = 0;
        std::uint64_t initiatorActorId = 0;
        std::uint16_t mapId = 0;
        std::uint16_t reserved2 = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t arguments[3] = {};
        char questName[128] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireQuestMutationMessage>);
    static_assert(sizeof(WireQuestMutationMessage) == 196);
    static_assert(sizeof(WireQuestMutationMessage) <=
        fable::multiplayer::protocol::MaximumReliableMessageBytes);

    bool IsTerminated(const char (&value)[128]) noexcept
    {
        return std::memchr(value, '\0', sizeof(value)) != nullptr;
    }

    bool HasValidArguments(
        const fable::multiplayer::protocol::QuestMutationMessage& message)
        noexcept
    {
        using fable::multiplayer::protocol::QuestMutationOperation;
        switch (message.operation)
        {
        case QuestMutationOperation::Activate:
            return message.arguments[2] == 0;
        case QuestMutationOperation::Deactivate:
            return message.arguments[1] == 0 && message.arguments[2] == 0;
        case QuestMutationOperation::Complete:
        case QuestMutationOperation::Fail:
            return true;
        default:
            return false;
        }
    }
}

namespace fable::multiplayer::protocol
{
    bool IsSaneQuestMutationMessage(
        const QuestMutationMessage& message) noexcept
    {
        if ((message.phase != QuestMutationPhase::Proposal &&
                message.phase != QuestMutationPhase::Commit &&
                message.phase != QuestMutationPhase::RepairNeeded) ||
            message.operation < QuestMutationOperation::Activate ||
            message.operation > QuestMutationOperation::Fail ||
            message.authorityEpoch == 0 || message.sessionRevision == 0 ||
            message.initiatorActorId == 0)
        {
            return false;
        }
        const bool validSemanticPayload =
            message.expectedCampaignRevision != 0 &&
            message.mutationId != 0 && message.mapId != 0 &&
            message.mapEpoch != 0 && !message.questName.empty() &&
            message.questName.size() < MaximumQuestMutationNameBytes &&
            message.questName.find('\0') == std::string::npos &&
            HasValidArguments(message);
        if (message.phase == QuestMutationPhase::RepairNeeded)
        {
            return validSemanticPayload && message.campaignRevision != 0 &&
                message.rejectionReason >=
                    QuestMutationRejectionReason::WrongAuthority &&
                message.rejectionReason <=
                    QuestMutationRejectionReason::QueueOverflow;
        }
        if (message.expectedCampaignRevision == 0 ||
            message.mutationId == 0 || message.mapId == 0 ||
            message.mapEpoch == 0 || message.questName.empty() ||
            message.questName.size() >= MaximumQuestMutationNameBytes ||
            message.questName.find('\0') != std::string::npos ||
            !HasValidArguments(message))
        {
            return false;
        }
        if (message.rejectionReason != QuestMutationRejectionReason::None)
        {
            return false;
        }
        if (message.phase == QuestMutationPhase::Proposal)
        {
            return message.campaignRevision == 0;
        }
        return message.campaignRevision != 0 &&
            message.expectedCampaignRevision !=
                (std::numeric_limits<std::uint64_t>::max)() &&
            message.campaignRevision ==
                message.expectedCampaignRevision + 1;
    }

    bool SameQuestMutationProposal(
        const QuestMutationMessage& left,
        const QuestMutationMessage& right) noexcept
    {
        return left.operation == right.operation &&
            left.authorityEpoch == right.authorityEpoch &&
            left.sessionRevision == right.sessionRevision &&
            left.mutationId == right.mutationId &&
            left.initiatorActorId == right.initiatorActorId &&
            left.mapId == right.mapId && left.mapEpoch == right.mapEpoch &&
            left.arguments == right.arguments &&
            left.questName == right.questName;
    }

    bool EncodeQuestMutationMessage(
        const QuestMutationMessage& message,
        std::uint8_t* const destination,
        const std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSaneQuestMutationMessage(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireQuestMutationMessage))
        {
            return false;
        }
        WireQuestMutationMessage wire;
        wire.phase = static_cast<std::uint8_t>(message.phase);
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.rejectionReason = static_cast<std::uint8_t>(
            message.rejectionReason);
        wire.authorityEpoch = message.authorityEpoch;
        wire.sessionRevision = message.sessionRevision;
        wire.campaignRevision = message.campaignRevision;
        wire.expectedCampaignRevision = message.expectedCampaignRevision;
        wire.mutationId = message.mutationId;
        wire.initiatorActorId = message.initiatorActorId;
        wire.mapId = message.mapId;
        wire.mapEpoch = message.mapEpoch;
        for (std::size_t i = 0; i < message.arguments.size(); ++i)
        {
            wire.arguments[i] = message.arguments[i];
        }
        std::memcpy(
            wire.questName,
            message.questName.data(),
            message.questName.size());
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeQuestMutationMessage(
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        QuestMutationMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WireQuestMutationMessage))
        {
            return false;
        }
        WireQuestMutationMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.reserved != 0 || wire.reserved2 != 0 ||
            !IsTerminated(wire.questName))
        {
            return false;
        }
        message.phase = static_cast<QuestMutationPhase>(wire.phase);
        message.operation = static_cast<QuestMutationOperation>(wire.operation);
        message.rejectionReason = static_cast<QuestMutationRejectionReason>(
            wire.rejectionReason);
        message.authorityEpoch = wire.authorityEpoch;
        message.sessionRevision = wire.sessionRevision;
        message.campaignRevision = wire.campaignRevision;
        message.expectedCampaignRevision = wire.expectedCampaignRevision;
        message.mutationId = wire.mutationId;
        message.initiatorActorId = wire.initiatorActorId;
        message.mapId = wire.mapId;
        message.mapEpoch = wire.mapEpoch;
        for (std::size_t i = 0; i < message.arguments.size(); ++i)
        {
            message.arguments[i] = wire.arguments[i];
        }
        message.questName = wire.questName;
        if (!IsSaneQuestMutationMessage(message))
        {
            message = {};
            return false;
        }
        return true;
    }
}
