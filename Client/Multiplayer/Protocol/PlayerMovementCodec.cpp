#include "PlayerMovementCodec.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WirePlayerMovementMessage final
    {
        std::uint64_t actorId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t sequence = 0;
        std::uint16_t mapId = 0;
        std::uint8_t moving = 0;
        std::uint8_t reserved = 0;
        float position[3] = {};
        float velocity[3] = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WirePlayerMovementMessage>);
    static_assert(sizeof(WirePlayerMovementMessage) == 60);
    static_assert(
        sizeof(WirePlayerMovementMessage) <=
            fable::multiplayer::protocol::MaximumDatagramBytes -
                fable::multiplayer::protocol::PacketHeaderBytes,
        "A player movement sample must fit in one datagram.");

    bool IsSane(const WirePlayerMovementMessage& message) noexcept
    {
        return message.actorId != 0 && message.authorityEpoch != 0 &&
            message.actorGeneration != 0 && message.mapEpoch != 0 &&
            message.sequence != 0 && message.mapId != 0 &&
            message.moving <= 1 && message.reserved == 0 &&
            std::isfinite(message.position[0]) &&
            std::isfinite(message.position[1]) &&
            std::isfinite(message.position[2]) &&
            std::isfinite(message.velocity[0]) &&
            std::isfinite(message.velocity[1]) &&
            std::isfinite(message.velocity[2]) &&
            std::isfinite(message.facing) &&
            message.facing >= -0.001f && message.facing <= 1.001f &&
            std::isfinite(message.angularVelocity);
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodePlayerMovementMessage(
        const PlayerMovementMessage& message,
        std::uint8_t* destination,
        const std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (destination == nullptr ||
            destinationCapacity < sizeof(WirePlayerMovementMessage))
        {
            return false;
        }

        WirePlayerMovementMessage wire;
        wire.actorId = message.actorId;
        wire.authorityEpoch = message.authorityEpoch;
        wire.actorGeneration = message.actorGeneration;
        wire.mapEpoch = message.mapEpoch;
        wire.sequence = message.sequence;
        wire.mapId = message.mapId;
        wire.moving = message.moving ? 1 : 0;
        wire.position[0] = message.position.x;
        wire.position[1] = message.position.y;
        wire.position[2] = message.position.z;
        wire.velocity[0] = message.velocity.x;
        wire.velocity[1] = message.velocity.y;
        wire.velocity[2] = message.velocity.z;
        wire.facing = message.facing;
        wire.angularVelocity = message.angularVelocity;
        if (!IsSane(wire))
        {
            return false;
        }

        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodePlayerMovementMessage(
        const std::uint8_t* payload,
        const std::size_t payloadSize,
        PlayerMovementMessage& message) noexcept
    {
        message = {};
        if (payload == nullptr ||
            payloadSize != sizeof(WirePlayerMovementMessage))
        {
            return false;
        }

        WirePlayerMovementMessage wire;
        std::memcpy(&wire, payload, sizeof(wire));
        if (!IsSane(wire))
        {
            return false;
        }

        message.actorId = wire.actorId;
        message.authorityEpoch = wire.authorityEpoch;
        message.actorGeneration = wire.actorGeneration;
        message.mapEpoch = wire.mapEpoch;
        message.sequence = wire.sequence;
        message.mapId = wire.mapId;
        message.moving = wire.moving != 0;
        message.position = {
            wire.position[0], wire.position[1], wire.position[2]};
        message.velocity = {
            wire.velocity[0], wire.velocity[1], wire.velocity[2]};
        message.facing = wire.facing;
        message.angularVelocity = wire.angularVelocity;
        return true;
    }
}
