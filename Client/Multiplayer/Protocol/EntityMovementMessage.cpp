#include "EntityMovementMessage.h"

#include <cmath>
#include <cstring>

namespace
{
#pragma pack(push, 1)
    struct WireEntityMovementMessage final
    {
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t sequence = 0;
        float position[3] = {};
        float velocity[3] = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
        std::uint8_t moving = 0;
        std::uint8_t reserved[3] = {};
        char mapName[96] = {};
    };
#pragma pack(pop)

    bool IsSane(
        const fable::multiplayer::protocol::EntityMovementMessage& message)
        noexcept
    {
        return message.entityUid != 0 && message.entityGeneration != 0 &&
            message.ownerActorId != 0 && message.mapEpoch != 0 &&
            message.sequence != 0 && !message.mapName.empty() &&
            message.mapName.size() < 96 &&
            std::isfinite(message.position.x) &&
            std::isfinite(message.position.y) &&
            std::isfinite(message.position.z) &&
            std::isfinite(message.velocity.x) &&
            std::isfinite(message.velocity.y) &&
            std::isfinite(message.velocity.z) &&
            std::isfinite(message.facing) && message.facing >= 0.0f &&
            message.facing < 1.0f &&
            std::isfinite(message.angularVelocity);
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeEntityMovementMessage(
        const EntityMovementMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireEntityMovementMessage))
        {
            return false;
        }
        WireEntityMovementMessage wire;
        wire.entityUid = message.entityUid;
        wire.entityGeneration = message.entityGeneration;
        wire.ownerActorId = message.ownerActorId;
        wire.mapEpoch = message.mapEpoch;
        wire.sequence = message.sequence;
        wire.position[0] = message.position.x;
        wire.position[1] = message.position.y;
        wire.position[2] = message.position.z;
        wire.velocity[0] = message.velocity.x;
        wire.velocity[1] = message.velocity.y;
        wire.velocity[2] = message.velocity.z;
        wire.facing = message.facing;
        wire.angularVelocity = message.angularVelocity;
        wire.moving = message.moving ? 1 : 0;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeEntityMovementMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityMovementMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount != sizeof(WireEntityMovementMessage))
        {
            return false;
        }
        WireEntityMovementMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (wire.moving > 1 || wire.reserved[0] != 0 ||
            wire.reserved[1] != 0 || wire.reserved[2] != 0 ||
            std::memchr(wire.mapName, '\0', sizeof(wire.mapName)) == nullptr)
        {
            return false;
        }
        message.entityUid = wire.entityUid;
        message.entityGeneration = wire.entityGeneration;
        message.ownerActorId = wire.ownerActorId;
        message.mapEpoch = wire.mapEpoch;
        message.sequence = wire.sequence;
        message.mapName = wire.mapName;
        message.position = {
            wire.position[0], wire.position[1], wire.position[2]};
        message.velocity = {
            wire.velocity[0], wire.velocity[1], wire.velocity[2]};
        message.facing = wire.facing;
        message.angularVelocity = wire.angularVelocity;
        message.moving = wire.moving != 0;
        return IsSane(message);
    }
}
