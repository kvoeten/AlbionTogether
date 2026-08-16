#pragma once

#include "Game/Math/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class EntityLifecycleOperation : std::uint8_t
    {
        ObservePresent = 1,
        ObserveDormant = 2,
        AuthoritativeUpsert = 3,
        AuthoritativeDormant = 4,
        AuthoritativeRetire = 5,
        BaselineBegin = 6,
        BaselineEnd = 7,
        ObserveTransfer = 8,
        ObserveMapRosterComplete = 9,
        AuthoritativeMapRosterComplete = 10,
        // The host has no canonical roster for this map yet. Only the named
        // first owner and epoch may seed it from a complete native snapshot.
        AuthoritativeMapRosterSeedAllowed = 11,
        // A fenced map owner changed CTCVillageMember's durable VillageUID.
        // The host converts this mutation into a normal authoritative upsert.
        ObserveVillageMembershipMutation = 12,
    };

    namespace entity_lifecycle_flag
    {
        inline constexpr std::uint8_t GamePersistent = 1u << 0;
        inline constexpr std::uint8_t LevelPersistent = 1u << 1;
        inline constexpr std::uint8_t Creature = 1u << 2;
        inline constexpr std::uint8_t Live = 1u << 3;
        inline constexpr std::uint8_t Available = 1u << 4;
        inline constexpr std::uint8_t HasTransform = 1u << 5;
        // The host accepted an explicit arrival: either a source-fenced
        // cross-map transfer or a runtime birth after the map roster completed.
        // It stays set while live so every observer can reconstruct the entity
        // independently; dormancy or retirement clears it.
        inline constexpr std::uint8_t AwaitingMaterialization = 1u << 6;
        // CTCVillageMember persists a VillageUID. Its transient idle scheduler
        // is rebuilt by the retail brain and is intentionally not replicated.
        inline constexpr std::uint8_t HasVillageMembership = 1u << 7;
        inline constexpr std::uint8_t All = GamePersistent |
            LevelPersistent | Creature | Live | Available | HasTransform |
            AwaitingMaterialization | HasVillageMembership;
    }

    // Guest observations are intents fenced by mapEpoch. A map-roster marker
    // follows the owner's ordered snapshot so replicas can safely remove local
    // extras. Only host-authored messages carry worldRevision and establish
    // canonical generations or roster completeness.
    struct EntityLifecycleMessage final
    {
        EntityLifecycleOperation operation =
            EntityLifecycleOperation::ObservePresent;
        std::uint8_t flags = 0;
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
        game::Vector3 position = {};
        float facing = 0.0f;
        std::string mapName;
        std::string sourceMapName;
        std::string definitionName;
        std::string scriptName;
    };

    bool EncodeEntityLifecycleMessage(
        const EntityLifecycleMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeEntityLifecycleMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityLifecycleMessage& message) noexcept;
}
