#include "EntityActionMessage.h"

#include <cmath>
#include <cstring>

namespace
{
#pragma pack(push, 1)
    struct WireEntityActionMessage final
    {
        std::uint8_t phase = 0;
        std::uint8_t kind = 0;
        std::uint8_t outcome = 0;
        std::uint8_t flags = 0;
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t targetEntityUid = 0;
        std::uint32_t targetEntityGeneration = 0;
        std::uint64_t targetPlayerActorId = 0;
        std::uint64_t actionId = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t actionEpoch = 0;
        std::uint32_t abilityId = 0;
        float abilityCharge = 0.0f;
        char mapName[96] = {};
        char semanticName[96] = {};
        char parameter[160] = {};
    };
#pragma pack(pop)

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size]) noexcept
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsSane(
        const fable::multiplayer::protocol::EntityActionMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        if (message.phase < EntityActionPhase::Intent ||
            message.phase > EntityActionPhase::End ||
            message.kind < EntityActionKind::Native ||
            message.kind > EntityActionKind::QuestOrCutscene ||
            message.outcome > EntityActionOutcome::Failed ||
            (message.flags & ~entity_action_flag::All) != 0 ||
            message.entityUid == 0 || message.actionId == 0 ||
            message.ownerActorId == 0 ||
            message.mapEpoch == 0 || message.mapName.empty() ||
            message.mapName.size() >= 96 || message.semanticName.empty() ||
            message.semanticName.size() >= 96 ||
            message.parameter.size() >= 160 ||
            message.abilityId >= 1'000'000 ||
            !std::isfinite(message.abilityCharge) ||
            message.abilityCharge < -100.0f ||
            message.abilityCharge > 100.0f)
        {
            return false;
        }
        const bool abilityAction = message.semanticName == "CreatureAbility";
        if (abilityAction != (message.abilityId != 0))
        {
            return false;
        }
        const bool hasEntityTarget =
            (message.flags & entity_action_flag::HasEntityTarget) != 0;
        const bool hasPlayerTarget =
            (message.flags & entity_action_flag::HasPlayerTarget) != 0;
        if (hasEntityTarget !=
                (message.targetEntityUid != 0 &&
                    message.targetEntityGeneration != 0) ||
            hasPlayerTarget != (message.targetPlayerActorId != 0))
        {
            return false;
        }
        if (message.phase == EntityActionPhase::Intent)
        {
            return message.actionEpoch == 0 &&
                message.outcome == EntityActionOutcome::None;
        }
        if (message.entityGeneration == 0 || message.actionEpoch == 0)
        {
            return false;
        }
        return message.phase == EntityActionPhase::End
            ? message.outcome != EntityActionOutcome::None
            : message.outcome == EntityActionOutcome::None;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeEntityActionMessage(
        const EntityActionMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireEntityActionMessage))
        {
            return false;
        }
        WireEntityActionMessage wire;
        wire.phase = static_cast<std::uint8_t>(message.phase);
        wire.kind = static_cast<std::uint8_t>(message.kind);
        wire.outcome = static_cast<std::uint8_t>(message.outcome);
        wire.flags = message.flags;
        wire.entityUid = message.entityUid;
        wire.entityGeneration = message.entityGeneration;
        wire.targetEntityUid = message.targetEntityUid;
        wire.targetEntityGeneration = message.targetEntityGeneration;
        wire.targetPlayerActorId = message.targetPlayerActorId;
        wire.actionId = message.actionId;
        wire.ownerActorId = message.ownerActorId;
        wire.mapEpoch = message.mapEpoch;
        wire.actionEpoch = message.actionEpoch;
        wire.abilityId = message.abilityId;
        wire.abilityCharge = message.abilityCharge;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        strncpy_s(wire.semanticName, message.semanticName.c_str(), _TRUNCATE);
        strncpy_s(wire.parameter, message.parameter.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeEntityActionMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityActionMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WireEntityActionMessage))
        {
            return false;
        }
        WireEntityActionMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (!IsTerminated(wire.mapName) ||
            !IsTerminated(wire.semanticName) ||
            !IsTerminated(wire.parameter))
        {
            return false;
        }
        message.phase = static_cast<EntityActionPhase>(wire.phase);
        message.kind = static_cast<EntityActionKind>(wire.kind);
        message.outcome = static_cast<EntityActionOutcome>(wire.outcome);
        message.flags = wire.flags;
        message.entityUid = wire.entityUid;
        message.entityGeneration = wire.entityGeneration;
        message.targetEntityUid = wire.targetEntityUid;
        message.targetEntityGeneration = wire.targetEntityGeneration;
        message.targetPlayerActorId = wire.targetPlayerActorId;
        message.actionId = wire.actionId;
        message.ownerActorId = wire.ownerActorId;
        message.mapEpoch = wire.mapEpoch;
        message.actionEpoch = wire.actionEpoch;
        message.abilityId = wire.abilityId;
        message.abilityCharge = wire.abilityCharge;
        message.mapName = wire.mapName;
        message.semanticName = wire.semanticName;
        message.parameter = wire.parameter;
        return IsSane(message);
    }
}
