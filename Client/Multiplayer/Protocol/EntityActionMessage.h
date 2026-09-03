#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class EntityActionPhase : std::uint8_t
    {
        Intent = 1,
        Begin = 2,
        Update = 3,
        End = 4,
    };

    enum class EntityActionKind : std::uint8_t
    {
        Native = 1,
        Movement = 2,
        Combat = 3,
        Conversation = 4,
        ConversationAnimation = 5,
        Trade = 6,
        QuestOrCutscene = 7,
    };

    // Retail shopping is private to the local Hero/save. Retain the wire enum
    // for decoding older peers, but never grant or replay a shared trade lease.
    constexpr bool RequiresSharedEntityAuthority(EntityActionKind kind) noexcept
    {
        return kind != EntityActionKind::Trade;
    }

    enum class EntityActionOutcome : std::uint8_t
    {
        None = 0,
        Completed = 1,
        Cancelled = 2,
        Failed = 3,
    };

    namespace entity_action_flag
    {
        inline constexpr std::uint8_t Exclusive = 1u << 0;
        inline constexpr std::uint8_t ObserverNoCamera = 1u << 1;
        inline constexpr std::uint8_t HasEntityTarget = 1u << 2;
        inline constexpr std::uint8_t HasPlayerTarget = 1u << 3;
        inline constexpr std::uint8_t All = Exclusive |
            ObserverNoCamera | HasEntityTarget | HasPlayerTarget;
    }

    struct EntityActionMessage final
    {
        EntityActionPhase phase = EntityActionPhase::Intent;
        EntityActionKind kind = EntityActionKind::Native;
        EntityActionOutcome outcome = EntityActionOutcome::None;
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
        // Stable DefinitionManager ability index and scalar request input.
        // These reconstruct the retail action graph on the observer's native
        // creature; they are not animation or object-memory snapshots.
        std::uint32_t abilityId = 0;
        float abilityCharge = 0.0f;
        std::string mapName;
        // Stable semantic/native action identifier. Concrete action codecs
        // interpret parameters; native object memory is never serialized.
        std::string semanticName;
        std::string parameter;
    };

    bool EncodeEntityActionMessage(
        const EntityActionMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeEntityActionMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        EntityActionMessage& message) noexcept;
}
