#include "EntityVitalsMessageCodec.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireEntityVitalsMessage final
    {
        std::uint8_t subject = 0;
        std::uint8_t reserved[3] = {};
        std::uint64_t playerActorId = 0;
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t ownerActorId = 0;
        std::uint32_t playerAuthorityEpoch = 0;
        std::uint32_t playerActorGeneration = 0;
        std::uint32_t playerMapEpoch = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t revision = 0;
        float currentHealth = 0.0f;
        float maximumHealth = 0.0f;
        char mapName[96] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireEntityVitalsMessage>);
    static_assert(sizeof(WireEntityVitalsMessage) == 156);

    bool IsSane(const fable::multiplayer::protocol::EntityVitalsMessage& message)
    {
        using namespace fable::multiplayer::protocol;
        if (message.ownerActorId == 0 || message.revision == 0 ||
            !std::isfinite(message.currentHealth) ||
            !std::isfinite(message.maximumHealth) ||
            message.maximumHealth <= 0.0f || message.currentHealth < 0.0f ||
            message.currentHealth > message.maximumHealth + 0.01f)
        {
            return false;
        }
        if (message.subject == EntityVitalsSubject::Player)
        {
            return message.playerActorId != 0 &&
                message.playerActorId == message.ownerActorId &&
                message.entityUid == 0 && message.entityGeneration == 0 &&
                message.playerAuthorityEpoch != 0 &&
                message.playerActorGeneration != 0 &&
                message.playerMapEpoch != 0 && message.mapEpoch == 0;
        }
        return message.subject == EntityVitalsSubject::WorldEntity &&
            message.playerActorId == 0 && message.entityUid != 0 &&
            message.entityGeneration != 0 && message.mapEpoch != 0 &&
            message.playerAuthorityEpoch == 0 &&
            message.playerActorGeneration == 0 &&
            message.playerMapEpoch == 0 &&
            !message.mapName.empty();
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeEntityVitalsMessage(
        const EntityVitalsMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireEntityVitalsMessage))
        {
            return false;
        }
        WireEntityVitalsMessage wire;
        wire.subject = static_cast<std::uint8_t>(message.subject);
        wire.playerActorId = message.playerActorId;
        wire.entityUid = message.entityUid;
        wire.entityGeneration = message.entityGeneration;
        wire.ownerActorId = message.ownerActorId;
        wire.playerAuthorityEpoch = message.playerAuthorityEpoch;
        wire.playerActorGeneration = message.playerActorGeneration;
        wire.playerMapEpoch = message.playerMapEpoch;
        wire.mapEpoch = message.mapEpoch;
        wire.revision = message.revision;
        wire.currentHealth = message.currentHealth;
        wire.maximumHealth = message.maximumHealth;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeEntityVitalsMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityVitalsMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WireEntityVitalsMessage))
        {
            return false;
        }
        WireEntityVitalsMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (std::memchr(wire.mapName, '\0', sizeof(wire.mapName)) == nullptr)
        {
            return false;
        }
        message.subject = static_cast<EntityVitalsSubject>(wire.subject);
        message.playerActorId = wire.playerActorId;
        message.entityUid = wire.entityUid;
        message.entityGeneration = wire.entityGeneration;
        message.ownerActorId = wire.ownerActorId;
        message.playerAuthorityEpoch = wire.playerAuthorityEpoch;
        message.playerActorGeneration = wire.playerActorGeneration;
        message.playerMapEpoch = wire.playerMapEpoch;
        message.mapEpoch = wire.mapEpoch;
        message.revision = wire.revision;
        message.currentHealth = wire.currentHealth;
        message.maximumHealth = wire.maximumHealth;
        message.mapName = wire.mapName;
        return IsSane(message);
    }
}
