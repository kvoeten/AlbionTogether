#include "EntityLifecycleMessage.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireEntityLifecycleMessage final
    {
        std::uint8_t operation = 0;
        std::uint8_t flags = 0;
        std::uint16_t reserved = 0;
        std::uint64_t entityUid = 0;
        std::uint64_t villageUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t worldRevision = 0;
        std::uint64_t simulationOwnerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t sourceMapEpoch = 0;
        std::uint32_t baselineId = 0;
        std::uint16_t mapId = 0;
        std::uint16_t definitionIndex = 0;
        float position[3] = {};
        float facing = 0.0f;
        char mapName[96] = {};
        char sourceMapName[96] = {};
        char definitionName[128] = {};
        char scriptName[96] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireEntityLifecycleMessage>);
    static_assert(sizeof(WireEntityLifecycleMessage) == 488);

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size]) noexcept
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsBaseline(
        fable::multiplayer::protocol::EntityLifecycleOperation operation)
        noexcept
    {
        using Operation =
            fable::multiplayer::protocol::EntityLifecycleOperation;
        return operation == Operation::BaselineBegin ||
            operation == Operation::BaselineEnd;
    }

    bool IsIntent(
        fable::multiplayer::protocol::EntityLifecycleOperation operation)
        noexcept
    {
        using Operation =
            fable::multiplayer::protocol::EntityLifecycleOperation;
        return operation == Operation::ObservePresent ||
            operation == Operation::ObserveDormant ||
            operation == Operation::ObserveTransfer ||
            operation == Operation::ObserveMapRosterComplete ||
            operation == Operation::ObserveVillageMembershipMutation;
    }

    bool IsMapRosterMarker(
        fable::multiplayer::protocol::EntityLifecycleOperation operation)
        noexcept
    {
        using Operation =
            fable::multiplayer::protocol::EntityLifecycleOperation;
        return operation == Operation::ObserveMapRosterComplete ||
            operation == Operation::AuthoritativeMapRosterComplete ||
            operation == Operation::AuthoritativeMapRosterSeedAllowed;
    }

    bool IsSane(
        const fable::multiplayer::protocol::EntityLifecycleMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        const auto operationValue = static_cast<std::uint8_t>(
            message.operation);
        if (operationValue < static_cast<std::uint8_t>(
                EntityLifecycleOperation::ObservePresent) ||
            operationValue > static_cast<std::uint8_t>(
                EntityLifecycleOperation::
                    ObserveVillageMembershipMutation) ||
            (message.flags & ~entity_lifecycle_flag::All) != 0 ||
            message.mapName.size() >= 96 ||
            message.sourceMapName.size() >= 96 ||
            message.definitionName.size() >= 128 ||
            message.scriptName.size() >= 96)
        {
            return false;
        }

        if (IsBaseline(message.operation))
        {
            return message.flags == 0 && message.entityUid == 0 &&
                message.villageUid == 0 &&
                message.entityGeneration == 0 && message.mapEpoch == 0 &&
                message.sourceMapEpoch == 0 &&
                message.simulationOwnerActorId == 0 &&
                message.baselineId != 0 && message.mapId == 0 &&
                message.definitionIndex == 0 &&
                (message.flags & entity_lifecycle_flag::HasTransform) == 0 &&
                message.mapName.empty() && message.sourceMapName.empty() &&
                message.definitionName.empty() &&
                message.scriptName.empty();
        }

        if (IsMapRosterMarker(message.operation))
        {
            const bool common = message.flags == 0 &&
                message.entityUid == 0 && message.villageUid == 0 &&
                message.entityGeneration == 0 &&
                message.baselineId == 0 && message.mapId == 0 &&
                message.definitionIndex == 0 &&
                message.position.x == 0.0f &&
                message.position.y == 0.0f &&
                message.position.z == 0.0f && message.facing == 0.0f &&
                !message.mapName.empty() &&
                message.definitionName.empty() &&
                message.scriptName.empty();
            if (!common)
            {
                return false;
            }
            if (message.operation ==
                EntityLifecycleOperation::ObserveMapRosterComplete)
            {
                return message.worldRevision == 0 &&
                    message.simulationOwnerActorId == 0 &&
                    message.mapEpoch != 0 &&
                    message.sourceMapEpoch == message.mapEpoch &&
                    message.sourceMapName == message.mapName;
            }
            if (message.operation == EntityLifecycleOperation::
                    AuthoritativeMapRosterSeedAllowed)
            {
                return message.worldRevision == 0 &&
                    message.simulationOwnerActorId != 0 &&
                    message.mapEpoch != 0 && message.sourceMapEpoch == 0 &&
                    message.sourceMapName.empty();
            }
            return message.worldRevision != 0 &&
                message.simulationOwnerActorId != 0 &&
                message.mapEpoch != 0 && message.sourceMapEpoch == 0 &&
                message.sourceMapName.empty();
        }

        if (message.entityUid == 0 || message.baselineId != 0)
        {
            return false;
        }
        const bool hasTransform =
            (message.flags & entity_lifecycle_flag::HasTransform) != 0;
        const bool hasVillageMembership =
            (message.flags &
                entity_lifecycle_flag::HasVillageMembership) != 0;
        if (hasVillageMembership != (message.villageUid != 0))
        {
            return false;
        }
        const bool awaitingMaterialization =
            (message.flags &
                entity_lifecycle_flag::AwaitingMaterialization) != 0;
        if (hasTransform &&
            (!std::isfinite(message.position.x) ||
                !std::isfinite(message.position.y) ||
                !std::isfinite(message.position.z) ||
                !std::isfinite(message.facing) || message.facing < 0.0f ||
                message.facing >= 1.0f))
        {
            return false;
        }
        if (!hasTransform &&
            (message.position.x != 0.0f || message.position.y != 0.0f ||
                message.position.z != 0.0f || message.facing != 0.0f))
        {
            return false;
        }
        if (IsIntent(message.operation))
        {
            if (message.worldRevision != 0 ||
                message.simulationOwnerActorId != 0 ||
                awaitingMaterialization ||
                message.sourceMapEpoch == 0 ||
                message.sourceMapName.empty() ||
                (message.flags & entity_lifecycle_flag::Available) == 0)
            {
                return false;
            }
            const bool live =
                (message.flags & entity_lifecycle_flag::Live) != 0;
            if (message.operation == EntityLifecycleOperation::
                    ObserveVillageMembershipMutation)
            {
                constexpr std::uint8_t allowedFlags =
                    entity_lifecycle_flag::Available |
                    entity_lifecycle_flag::Live |
                    entity_lifecycle_flag::HasVillageMembership;
                return message.flags != 0 &&
                    (message.flags & ~allowedFlags) == 0 && live &&
                    message.entityGeneration != 0 &&
                    message.mapId != 0 && message.mapEpoch != 0 &&
                    message.mapEpoch == message.sourceMapEpoch &&
                    message.mapName == message.sourceMapName &&
                    message.definitionIndex == 0 &&
                    message.definitionName.empty() &&
                    message.scriptName.empty();
            }
            if (message.operation ==
                EntityLifecycleOperation::ObserveTransfer)
            {
                return !live && message.entityGeneration != 0 &&
                    message.mapId != 0 && message.mapEpoch == 0;
            }
            return message.mapId != 0 &&
                message.mapEpoch == message.sourceMapEpoch &&
                message.mapName == message.sourceMapName &&
                ((message.operation ==
                        EntityLifecycleOperation::ObservePresent) == live);
        }
        if (!message.sourceMapName.empty() || message.sourceMapEpoch != 0 ||
            message.entityGeneration == 0 || message.worldRevision == 0)
        {
            return false;
        }

        const bool live =
            (message.flags & entity_lifecycle_flag::Live) != 0;
        const bool available =
            (message.flags & entity_lifecycle_flag::Available) != 0;
        switch (message.operation)
        {
        case EntityLifecycleOperation::AuthoritativeUpsert:
            return live && available &&
                message.mapId != 0 &&
                !message.mapName.empty() &&
                message.simulationOwnerActorId != 0 &&
                message.mapEpoch != 0;
        case EntityLifecycleOperation::AuthoritativeDormant:
            return !live && available && message.mapId != 0 &&
                ((message.simulationOwnerActorId == 0 &&
                        message.mapEpoch == 0) ||
                    (message.simulationOwnerActorId != 0 &&
                        message.mapEpoch != 0 && !message.mapName.empty()));
        case EntityLifecycleOperation::AuthoritativeRetire:
            return !live && !available && !awaitingMaterialization &&
                !message.mapName.empty() &&
                ((message.simulationOwnerActorId == 0 &&
                        message.mapEpoch == 0) ||
                    (message.simulationOwnerActorId != 0 &&
                        message.mapEpoch != 0));
        default:
            return false;
        }
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeEntityLifecycleMessage(
        const EntityLifecycleMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireEntityLifecycleMessage))
        {
            return false;
        }

        WireEntityLifecycleMessage wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.flags = message.flags;
        wire.entityUid = message.entityUid;
        wire.villageUid = message.villageUid;
        wire.entityGeneration = message.entityGeneration;
        wire.worldRevision = message.worldRevision;
        wire.simulationOwnerActorId = message.simulationOwnerActorId;
        wire.mapEpoch = message.mapEpoch;
        wire.sourceMapEpoch = message.sourceMapEpoch;
        wire.baselineId = message.baselineId;
        wire.mapId = message.mapId;
        wire.definitionIndex = message.definitionIndex;
        wire.position[0] = message.position.x;
        wire.position[1] = message.position.y;
        wire.position[2] = message.position.z;
        wire.facing = message.facing;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        strncpy_s(
            wire.sourceMapName,
            message.sourceMapName.c_str(),
            _TRUNCATE);
        strncpy_s(
            wire.definitionName,
            message.definitionName.c_str(),
            _TRUNCATE);
        strncpy_s(wire.scriptName, message.scriptName.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeEntityLifecycleMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityLifecycleMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount != sizeof(WireEntityLifecycleMessage))
        {
            return false;
        }

        WireEntityLifecycleMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.reserved != 0 ||
            !IsTerminated(wire.mapName) ||
            !IsTerminated(wire.sourceMapName) ||
            !IsTerminated(wire.definitionName) ||
            !IsTerminated(wire.scriptName))
        {
            return false;
        }

        message.operation = static_cast<EntityLifecycleOperation>(
            wire.operation);
        message.flags = wire.flags;
        message.entityUid = wire.entityUid;
        message.villageUid = wire.villageUid;
        message.entityGeneration = wire.entityGeneration;
        message.worldRevision = wire.worldRevision;
        message.simulationOwnerActorId = wire.simulationOwnerActorId;
        message.mapEpoch = wire.mapEpoch;
        message.sourceMapEpoch = wire.sourceMapEpoch;
        message.baselineId = wire.baselineId;
        message.mapId = wire.mapId;
        message.definitionIndex = wire.definitionIndex;
        message.position = {
            wire.position[0], wire.position[1], wire.position[2]};
        message.facing = wire.facing;
        message.mapName = wire.mapName;
        message.sourceMapName = wire.sourceMapName;
        message.definitionName = wire.definitionName;
        message.scriptName = wire.scriptName;
        return IsSane(message);
    }
}
