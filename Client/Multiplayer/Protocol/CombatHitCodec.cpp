#include "CombatHitCodec.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireCombatHitMessage final
    {
        std::uint8_t phase = 0;
        std::uint8_t sourceDomain = 0;
        std::uint8_t targetKind = 0;
        std::uint8_t impactFlags = 0;
        std::uint32_t reactionFlags = 0;

        std::uint64_t sourceActionId = 0;
        std::uint64_t sourceId = 0;
        std::uint64_t sourceOwnerActorId = 0;
        std::uint32_t sourceAuthorityEpoch = 0;
        std::uint32_t sourceGeneration = 0;
        std::uint32_t sourceMapEpoch = 0;
        std::uint32_t sourceActionEpoch = 0;
        std::uint16_t sourceMapId = 0;
        std::uint16_t reserved1 = 0;

        std::uint64_t targetId = 0;
        std::uint32_t targetAuthorityEpoch = 0;
        std::uint32_t targetGeneration = 0;
        std::uint32_t targetMapEpoch = 0;
        std::uint16_t targetMapId = 0;
        std::uint16_t reserved2 = 0;

        std::uint64_t candidateSequence = 0;
        std::uint32_t hitOrdinal = 0;
        std::uint32_t reserved3 = 0;
        std::uint64_t hostTargetRevision = 0;
        float healthBefore = 0.0f;
        float healthAfter = 0.0f;
        float maximumHealth = 0.0f;
        std::uint32_t reactionId = 0;
        float impactPosition[3] = {};
        float impactDirection[3] = {};
        std::uint64_t resolverActorId = 0;
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireCombatHitMessage>);
    static_assert(sizeof(WireCombatHitMessage) == 148);
    static_assert(
        sizeof(WireCombatHitMessage) <=
            fable::multiplayer::protocol::MaximumDatagramBytes -
                fable::multiplayer::protocol::PacketHeaderBytes,
        "A combat hit must fit in one datagram.");

    using fable::multiplayer::protocol::CombatActionDomain;
    using fable::multiplayer::protocol::CombatHitPhase;
    using fable::multiplayer::protocol::CombatParticipantKind;

    constexpr float kHealthTolerance = 0.01f;
    constexpr std::uint32_t kMaximumHitOrdinal = 4'095;

    bool IsZero(const float (&value)[3]) noexcept
    {
        return value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
    }

    bool IsFinite(const float (&value)[3]) noexcept
    {
        return std::isfinite(value[0]) && std::isfinite(value[1]) &&
            std::isfinite(value[2]);
    }

    bool IsDirectionSane(const float (&value)[3]) noexcept
    {
        if (!IsFinite(value))
        {
            return false;
        }
        const float lengthSquared = value[0] * value[0] +
            value[1] * value[1] + value[2] * value[2];
        return std::isfinite(lengthSquared) && lengthSquared >= 0.0001f;
    }

    bool IsSane(const WireCombatHitMessage& wire) noexcept
    {
        using namespace fable::multiplayer::protocol;
        const auto phase = static_cast<CombatHitPhase>(wire.phase);
        const auto sourceDomain = static_cast<CombatActionDomain>(
            wire.sourceDomain);
        const auto targetKind = static_cast<CombatParticipantKind>(
            wire.targetKind);
        if ((phase != CombatHitPhase::Candidate &&
                phase != CombatHitPhase::Result) ||
            (sourceDomain != CombatActionDomain::Player &&
                sourceDomain != CombatActionDomain::WorldEntity) ||
            (targetKind != CombatParticipantKind::Player &&
                targetKind != CombatParticipantKind::WorldEntity) ||
            (wire.impactFlags & ~combat_hit_impact_flag::All) != 0 ||
            (wire.reactionFlags & ~combat_hit_reaction_flag::All) != 0 ||
            wire.reserved1 != 0 || wire.reserved2 != 0 ||
            wire.reserved3 != 0 ||
            wire.sourceActionId == 0 || wire.sourceId == 0 ||
            wire.sourceOwnerActorId == 0 || wire.sourceGeneration == 0 ||
            wire.sourceMapEpoch == 0 || wire.sourceMapId == 0 ||
            wire.targetId == 0 || wire.targetGeneration == 0 ||
            wire.targetMapEpoch == 0 || wire.targetMapId == 0 ||
            wire.sourceMapId != wire.targetMapId ||
            wire.candidateSequence == 0 ||
            wire.hitOrdinal == 0 || wire.hitOrdinal > kMaximumHitOrdinal ||
            wire.resolverActorId == 0)
        {
            return false;
        }

        const bool playerSource = sourceDomain == CombatActionDomain::Player;
        if (playerSource != (wire.sourceAuthorityEpoch != 0) ||
            playerSource != (wire.sourceActionEpoch == 0) ||
            (!playerSource && wire.sourceActionEpoch == 0) ||
            (playerSource && wire.sourceOwnerActorId != wire.sourceId))
        {
            return false;
        }
        const bool playerTarget = targetKind ==
            CombatParticipantKind::Player;
        if (playerTarget != (wire.targetAuthorityEpoch != 0))
        {
            return false;
        }

        const bool result = phase == CombatHitPhase::Result;
        if (result != (wire.hostTargetRevision != 0))
        {
            return false;
        }

        if (!std::isfinite(wire.healthBefore) ||
            !std::isfinite(wire.healthAfter) ||
            !std::isfinite(wire.maximumHealth) ||
            wire.maximumHealth <= 0.0f || wire.healthBefore < 0.0f ||
            wire.healthAfter < 0.0f ||
            wire.healthBefore > wire.maximumHealth + kHealthTolerance ||
            wire.healthAfter > wire.maximumHealth + kHealthTolerance ||
            !IsFinite(wire.impactPosition) ||
            !IsFinite(wire.impactDirection))
        {
            return false;
        }

        const bool healing = (wire.reactionFlags &
            combat_hit_reaction_flag::Healing) != 0;
        const bool killed = (wire.reactionFlags &
            combat_hit_reaction_flag::Killed) != 0;
        const bool hasReactionId = (wire.reactionFlags &
            combat_hit_reaction_flag::HasReactionId) != 0;
        if ((healing && wire.healthAfter + kHealthTolerance <
                wire.healthBefore) ||
            (!healing && wire.healthAfter >
                wire.healthBefore + kHealthTolerance) ||
            (killed != (wire.healthAfter <= kHealthTolerance)) ||
            (hasReactionId != (wire.reactionId != 0)) ||
            wire.reactionId >= 1'000'000)
        {
            return false;
        }

        const bool hasPosition = (wire.impactFlags &
            combat_hit_impact_flag::HasPosition) != 0;
        const bool hasDirection = (wire.impactFlags &
            combat_hit_impact_flag::HasDirection) != 0;
        return (hasPosition || IsZero(wire.impactPosition)) &&
            (hasDirection
                ? IsDirectionSane(wire.impactDirection)
                : IsZero(wire.impactDirection));
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeCombatHitMessage(
        const CombatHitMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (destination == nullptr ||
            destinationCapacity < sizeof(WireCombatHitMessage))
        {
            return false;
        }

        WireCombatHitMessage wire;
        wire.phase = static_cast<std::uint8_t>(message.phase);
        wire.sourceDomain = static_cast<std::uint8_t>(message.sourceDomain);
        wire.targetKind = static_cast<std::uint8_t>(message.targetKind);
        wire.impactFlags = message.impactFlags;
        wire.reactionFlags = message.reactionFlags;
        wire.sourceActionId = message.sourceActionId;
        wire.sourceId = message.sourceId;
        wire.sourceOwnerActorId = message.sourceOwnerActorId;
        wire.sourceAuthorityEpoch = message.sourceAuthorityEpoch;
        wire.sourceGeneration = message.sourceGeneration;
        wire.sourceMapEpoch = message.sourceMapEpoch;
        wire.sourceActionEpoch = message.sourceActionEpoch;
        wire.sourceMapId = message.sourceMapId;
        wire.targetId = message.targetId;
        wire.targetAuthorityEpoch = message.targetAuthorityEpoch;
        wire.targetGeneration = message.targetGeneration;
        wire.targetMapEpoch = message.targetMapEpoch;
        wire.targetMapId = message.targetMapId;
        wire.candidateSequence = message.candidateSequence;
        wire.hitOrdinal = message.hitOrdinal;
        wire.hostTargetRevision = message.hostTargetRevision;
        wire.healthBefore = message.healthBefore;
        wire.healthAfter = message.healthAfter;
        wire.maximumHealth = message.maximumHealth;
        wire.reactionId = message.reactionId;
        wire.impactPosition[0] = message.impactPosition.x;
        wire.impactPosition[1] = message.impactPosition.y;
        wire.impactPosition[2] = message.impactPosition.z;
        wire.impactDirection[0] = message.impactDirection.x;
        wire.impactDirection[1] = message.impactDirection.y;
        wire.impactDirection[2] = message.impactDirection.z;
        wire.resolverActorId = message.resolverActorId;
        if (!IsSane(wire))
        {
            return false;
        }

        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeCombatHitMessage(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        CombatHitMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WireCombatHitMessage))
        {
            return false;
        }

        WireCombatHitMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (!IsSane(wire))
        {
            return false;
        }

        message.phase = static_cast<CombatHitPhase>(wire.phase);
        message.sourceDomain = static_cast<CombatActionDomain>(
            wire.sourceDomain);
        message.targetKind = static_cast<CombatParticipantKind>(
            wire.targetKind);
        message.impactFlags = wire.impactFlags;
        message.reactionFlags = wire.reactionFlags;
        message.sourceActionId = wire.sourceActionId;
        message.sourceId = wire.sourceId;
        message.sourceOwnerActorId = wire.sourceOwnerActorId;
        message.sourceAuthorityEpoch = wire.sourceAuthorityEpoch;
        message.sourceGeneration = wire.sourceGeneration;
        message.sourceMapEpoch = wire.sourceMapEpoch;
        message.sourceActionEpoch = wire.sourceActionEpoch;
        message.sourceMapId = wire.sourceMapId;
        message.targetId = wire.targetId;
        message.targetAuthorityEpoch = wire.targetAuthorityEpoch;
        message.targetGeneration = wire.targetGeneration;
        message.targetMapEpoch = wire.targetMapEpoch;
        message.targetMapId = wire.targetMapId;
        message.candidateSequence = wire.candidateSequence;
        message.hitOrdinal = wire.hitOrdinal;
        message.hostTargetRevision = wire.hostTargetRevision;
        message.healthBefore = wire.healthBefore;
        message.healthAfter = wire.healthAfter;
        message.maximumHealth = wire.maximumHealth;
        message.reactionId = wire.reactionId;
        message.impactPosition = {
            wire.impactPosition[0],
            wire.impactPosition[1],
            wire.impactPosition[2]};
        message.impactDirection = {
            wire.impactDirection[0],
            wire.impactDirection[1],
            wire.impactDirection[2]};
        message.resolverActorId = wire.resolverActorId;
        return true;
    }
}
