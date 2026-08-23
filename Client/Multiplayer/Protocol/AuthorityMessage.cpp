#include "AuthorityMessage.h"

#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WireAuthorityMessage final
    {
        std::uint8_t operation = 0;
        std::uint8_t scope = 0;
        std::uint8_t actionKind = 0;
        std::uint8_t reserved = 0;
        std::uint64_t ownerActorId = 0;
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint16_t mapId = 0;
        std::uint16_t reserved2 = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t actionEpoch = 0;
        std::uint64_t mapBaselineRevision = 0;
        char mapName[96] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireAuthorityMessage>);
    static_assert(sizeof(WireAuthorityMessage) == 140);

    bool IsTerminated(const char (&value)[96]) noexcept
    {
        return std::memchr(value, '\0', sizeof(value)) != nullptr;
    }

    bool IsSane(const fable::multiplayer::protocol::AuthorityMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        if ((message.operation != AuthorityOperation::Grant &&
                message.operation != AuthorityOperation::Release &&
                message.operation != AuthorityOperation::Request &&
                message.operation != AuthorityOperation::Prepare &&
                message.operation != AuthorityOperation::Prepared) ||
            (message.scope != AuthorityScope::MapSimulation &&
                message.scope != AuthorityScope::EntityAction) ||
            message.mapName.empty() || message.mapName.size() >= 96 ||
            (message.operation != AuthorityOperation::Request &&
                message.operation != AuthorityOperation::Prepare &&
                message.operation != AuthorityOperation::Prepared &&
                message.mapEpoch == 0))
        {
            return false;
        }
        if (message.operation == AuthorityOperation::Request ||
            message.operation == AuthorityOperation::Prepare)
        {
            return message.scope == AuthorityScope::MapSimulation &&
                message.actionKind == ActionLeaseKind::None &&
                message.ownerActorId != 0 && message.entityUid == 0 &&
                message.entityGeneration == 0 && message.mapId != 0 &&
                message.actionEpoch == 0 &&
                message.mapBaselineRevision == 0;
        }
        if (message.operation == AuthorityOperation::Prepared)
        {
            return message.scope == AuthorityScope::MapSimulation &&
                message.actionKind == ActionLeaseKind::None &&
                message.ownerActorId != 0 && message.entityUid == 0 &&
                message.entityGeneration == 0 && message.mapId != 0 &&
                message.mapEpoch == 0 && message.actionEpoch == 0 &&
                message.mapBaselineRevision != 0;
        }
        const bool grant = message.operation == AuthorityOperation::Grant;
        if (grant != (message.ownerActorId != 0))
        {
            return false;
        }
        if (message.scope == AuthorityScope::MapSimulation)
        {
            return message.actionKind == ActionLeaseKind::None &&
                message.entityUid == 0 && message.entityGeneration == 0 &&
                message.mapId != 0 && message.actionEpoch == 0 &&
                (message.operation == AuthorityOperation::Grant
                    ? message.mapBaselineRevision != 0
                    : message.mapBaselineRevision == 0);
        }
        return message.actionKind >= ActionLeaseKind::Ambient &&
            message.actionKind <= ActionLeaseKind::PrimaryAttacker &&
            message.entityUid != 0 && message.entityGeneration != 0 &&
            message.mapId == 0 && message.actionEpoch != 0 &&
            message.mapBaselineRevision == 0;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodeAuthorityMessage(
        const AuthorityMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WireAuthorityMessage))
        {
            return false;
        }
        WireAuthorityMessage wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.scope = static_cast<std::uint8_t>(message.scope);
        wire.actionKind = static_cast<std::uint8_t>(message.actionKind);
        wire.ownerActorId = message.ownerActorId;
        wire.entityUid = message.entityUid;
        wire.entityGeneration = message.entityGeneration;
        wire.mapId = message.mapId;
        wire.mapEpoch = message.mapEpoch;
        wire.actionEpoch = message.actionEpoch;
        wire.mapBaselineRevision = message.mapBaselineRevision;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodeAuthorityMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        AuthorityMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WireAuthorityMessage))
        {
            return false;
        }
        WireAuthorityMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (!IsTerminated(wire.mapName) || wire.reserved != 0 ||
            wire.reserved2 != 0)
        {
            return false;
        }
        message.operation = static_cast<AuthorityOperation>(wire.operation);
        message.scope = static_cast<AuthorityScope>(wire.scope);
        message.actionKind = static_cast<ActionLeaseKind>(wire.actionKind);
        message.ownerActorId = wire.ownerActorId;
        message.entityUid = wire.entityUid;
        message.entityGeneration = wire.entityGeneration;
        message.mapId = wire.mapId;
        message.mapEpoch = wire.mapEpoch;
        message.actionEpoch = wire.actionEpoch;
        message.mapBaselineRevision = wire.mapBaselineRevision;
        message.mapName = wire.mapName;
        return IsSane(message);
    }
}
