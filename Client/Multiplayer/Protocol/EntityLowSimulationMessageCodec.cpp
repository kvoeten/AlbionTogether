#include "EntityLowSimulationMessageCodec.h"

#include <cstring>
#include <type_traits>

namespace
{
    constexpr std::uint8_t RespawnableFlag = 1u << 0;
    constexpr std::uint8_t GuardFlag = 1u << 1;
    constexpr std::uint8_t AllFlags = RespawnableFlag | GuardFlag;

#pragma pack(push, 1)
    struct WireEntityLowSimulationMessage final
    {
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t revision = 0;
        std::int32_t recreationDay = 0;
        std::int32_t recreationFrame = 0;
        std::uint8_t flags = 0;
        std::uint8_t reserved[3] = {};
        char mapName[96] = {};
    };
#pragma pack(pop)

    static_assert(
        std::is_trivially_copyable_v<WireEntityLowSimulationMessage>);
    static_assert(sizeof(WireEntityLowSimulationMessage) == 136);

    bool IsSane(
        const fable::multiplayer::protocol::EntityLowSimulationMessage&
            message) noexcept
    {
        return message.entityUid != 0 && message.entityGeneration != 0 &&
            message.ownerActorId != 0 && message.mapEpoch != 0 &&
            message.revision != 0 && !message.mapName.empty();
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeEntityLowSimulationMessage(
        const EntityLowSimulationMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireEntityLowSimulationMessage))
        {
            return false;
        }
        WireEntityLowSimulationMessage wire;
        wire.entityUid = message.entityUid;
        wire.entityGeneration = message.entityGeneration;
        wire.ownerActorId = message.ownerActorId;
        wire.mapEpoch = message.mapEpoch;
        wire.revision = message.revision;
        wire.recreationDay = message.recreationDay;
        wire.recreationFrame = message.recreationFrame;
        if (message.respawnable)
        {
            wire.flags |= RespawnableFlag;
        }
        if (message.guard)
        {
            wire.flags |= GuardFlag;
        }
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeEntityLowSimulationMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityLowSimulationMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr ||
            byteCount != sizeof(WireEntityLowSimulationMessage))
        {
            return false;
        }
        WireEntityLowSimulationMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if ((wire.flags & ~AllFlags) != 0 ||
            std::memchr(wire.mapName, '\0', sizeof(wire.mapName)) == nullptr)
        {
            return false;
        }
        message.entityUid = wire.entityUid;
        message.entityGeneration = wire.entityGeneration;
        message.ownerActorId = wire.ownerActorId;
        message.mapEpoch = wire.mapEpoch;
        message.revision = wire.revision;
        message.recreationDay = wire.recreationDay;
        message.recreationFrame = wire.recreationFrame;
        message.respawnable = (wire.flags & RespawnableFlag) != 0;
        message.guard = (wire.flags & GuardFlag) != 0;
        message.mapName = wire.mapName;
        return IsSane(message);
    }
}
