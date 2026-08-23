#include "PlayerActorStateCodec.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>

namespace
{
    constexpr float kBoneScaleQuantization = 4'095.0f;
    constexpr std::size_t kPlayerIdBytes = 48;
    constexpr std::size_t kMapNameBytes = 96;
    constexpr std::size_t kAppearanceDefinitionBytes = 96;

#pragma pack(push, 1)
    struct WireHeroBoneScale final
    {
        std::uint16_t boneIndex = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint16_t z = 0;
    };

    struct WirePlayerActorStateMessage final
    {
        std::uint8_t operation = 0;
        std::uint8_t componentFlags = 0;
        std::uint8_t role = 0;
        std::uint8_t reserved = 0;
        std::uint64_t actorId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t structuralRevision = 0;
        std::uint16_t mapId = 0;
        float initialPosition[3] = {};
        float initialFacing = 0.0f;
        char playerId[kPlayerIdBytes] = {};
        char mapName[kMapNameBytes] = {};
        char appearanceDefinition[kAppearanceDefinitionBytes] = {};
        float heroMorph[8] = {};
        std::int32_t heroClothingDefinitionIndices[
            fable::game::hero_pawn::appearance::HeroClothingState::SlotCount] = {};
        std::uint16_t heroAppearanceModifierCount = 0;
        std::int32_t heroAppearanceModifierDefinitionIndices[
            fable::game::hero_pawn::appearance::HeroAppearanceModifierState::
                MaximumEntries] = {};
        std::uint16_t heroBoneScaleCount = 0;
        WireHeroBoneScale heroBoneScales[
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries] = {};
        std::int32_t meleeWeaponDefinitionIndex = 0;
        std::int32_t rangedWeaponDefinitionIndex = 0;
        std::uint32_t meleeWeaponAttachmentSlot = 0;
        std::uint32_t rangedWeaponAttachmentSlot = 0;
        std::uint8_t activeWeaponFamily = 0;
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WireHeroBoneScale>);
    static_assert(sizeof(WireHeroBoneScale) == 8);
    static_assert(
        std::is_trivially_copyable_v<WirePlayerActorStateMessage>);
    static_assert(sizeof(WirePlayerActorStateMessage) == 1'387);
    static_assert(
        sizeof(WirePlayerActorStateMessage) <=
            fable::multiplayer::protocol::MaximumDatagramBytes -
                fable::multiplayer::protocol::PacketHeaderBytes,
        "The complete reliable actor construction message must fit one datagram.");

    using fable::game::hero_pawn::appearance::HeroAppearanceModifierState;
    using fable::game::hero_pawn::appearance::HeroBoneScaleState;
    using fable::game::hero_pawn::appearance::HeroClothingState;
    using fable::game::hero_pawn::appearance::HeroMorphState;
    using fable::game::hero_pawn::equipment::HeroEquipmentState;
    using fable::multiplayer::PeerRole;
    using fable::multiplayer::protocol::PlayerActorStateMessage;
    using fable::multiplayer::protocol::PlayerActorStateOperation;

    bool IsOperationValid(PlayerActorStateOperation operation) noexcept
    {
        return operation == PlayerActorStateOperation::Construct ||
            operation == PlayerActorStateOperation::ComponentDelta ||
            operation == PlayerActorStateOperation::MapTransition ||
            operation == PlayerActorStateOperation::Retire;
    }

    bool IsRoleValid(PeerRole role) noexcept
    {
        return role == PeerRole::Host || role == PeerRole::Guest;
    }

    bool IsFacingSane(float value) noexcept
    {
        return std::isfinite(value) && value >= -0.001f && value <= 1.001f;
    }

    bool IsTextSane(const std::string& value, std::size_t capacity) noexcept
    {
        return !value.empty() && value.size() < capacity;
    }

    template <std::size_t Size>
    void CopyText(char (&destination)[Size], const std::string& source) noexcept
    {
        (void)strncpy_s(destination, source.c_str(), _TRUNCATE);
    }

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size]) noexcept
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsFinite(const WirePlayerActorStateMessage& wire) noexcept
    {
        return std::isfinite(wire.initialPosition[0]) &&
            std::isfinite(wire.initialPosition[1]) &&
            std::isfinite(wire.initialPosition[2]) &&
            IsFacingSane(wire.initialFacing) &&
            std::all_of(
                std::begin(wire.heroMorph),
                std::end(wire.heroMorph),
                [](float value) { return std::isfinite(value); });
    }

    bool IsSaneAppearance(const WirePlayerActorStateMessage& wire) noexcept
    {
        const bool changed = (wire.componentFlags & fable::multiplayer::protocol::
            player_actor_state_flag::AppearanceChanged) != 0;
        const bool present = (wire.componentFlags & fable::multiplayer::protocol::
            player_actor_state_flag::AppearancePresent) != 0;
        if (!changed || !present)
        {
            return true;
        }
        if (wire.heroBoneScaleCount > HeroBoneScaleState::MaximumEntries ||
            wire.heroAppearanceModifierCount >
                HeroAppearanceModifierState::MaximumEntries)
        {
            return false;
        }
        const auto unitValue = [](float value) noexcept
        {
            return value >= -0.001f && value <= 1.001f;
        };
        for (std::size_t index = 0; index < 7; ++index)
        {
            if (!unitValue(wire.heroMorph[index]))
            {
                return false;
            }
        }
        if (wire.heroMorph[7] < -16.0f || wire.heroMorph[7] > 16.0f)
        {
            return false;
        }
        for (const std::int32_t definitionIndex : wire.heroClothingDefinitionIndices)
        {
            if (definitionIndex != -1 &&
                (definitionIndex <= 0 || definitionIndex >= 1'000'000))
            {
                return false;
            }
        }
        for (std::size_t index = 0;
             index < wire.heroAppearanceModifierCount;
             ++index)
        {
            const std::int32_t definitionIndex =
                wire.heroAppearanceModifierDefinitionIndices[index];
            if (definitionIndex <= 0 || definitionIndex >= 1'000'000)
            {
                return false;
            }
            for (std::size_t earlier = 0; earlier < index; ++earlier)
            {
                if (wire.heroAppearanceModifierDefinitionIndices[earlier] ==
                    definitionIndex)
                {
                    return false;
                }
            }
        }
        for (std::size_t index = 0; index < wire.heroBoneScaleCount; ++index)
        {
            const WireHeroBoneScale& scale = wire.heroBoneScales[index];
            if (scale.boneIndex >= 1'024 ||
                scale.x > 16 * kBoneScaleQuantization ||
                scale.y > 16 * kBoneScaleQuantization ||
                scale.z > 16 * kBoneScaleQuantization)
            {
                return false;
            }
        }
        return true;
    }

    bool IsSaneEquipment(const WirePlayerActorStateMessage& wire) noexcept
    {
        const bool changed = (wire.componentFlags & fable::multiplayer::protocol::
            player_actor_state_flag::EquipmentChanged) != 0;
        const bool present = (wire.componentFlags & fable::multiplayer::protocol::
            player_actor_state_flag::EquipmentPresent) != 0;
        if (!changed || !present)
        {
            return true;
        }
        const auto saneDefinition = [](std::int32_t value) noexcept
        {
            return value == -1 || (value > 0 && value < 1'000'000);
        };
        using fable::game::creature::equipment::CreatureWeaponFamily;
        const auto family = static_cast<CreatureWeaponFamily>(
            wire.activeWeaponFamily);
        const bool saneFamily = family == CreatureWeaponFamily::None ||
            family == CreatureWeaponFamily::Melee ||
            family == CreatureWeaponFamily::Ranged;
        const bool availableFamily = family == CreatureWeaponFamily::None ||
            (family == CreatureWeaponFamily::Melee &&
                wire.meleeWeaponDefinitionIndex > 0) ||
            (family == CreatureWeaponFamily::Ranged &&
                wire.rangedWeaponDefinitionIndex > 0);
        const auto saneAttachment = [](std::int32_t definition,
                                       std::uint32_t slot) noexcept
        {
            return definition == -1 ? slot == 0 : slot < 1'000'000;
        };
        return saneFamily && availableFamily &&
            saneDefinition(wire.meleeWeaponDefinitionIndex) &&
            saneDefinition(wire.rangedWeaponDefinitionIndex) &&
            saneAttachment(
                wire.meleeWeaponDefinitionIndex,
                wire.meleeWeaponAttachmentSlot) &&
            saneAttachment(
                wire.rangedWeaponDefinitionIndex,
                wire.rangedWeaponAttachmentSlot);
    }

    bool IsSaneConstruction(
        const WirePlayerActorStateMessage& wire) noexcept
    {
        const auto operation = static_cast<PlayerActorStateOperation>(
            wire.operation);
        if (!IsOperationValid(operation) || wire.reserved != 0 ||
            (wire.componentFlags & ~fable::multiplayer::protocol::
                player_actor_state_flag::All) != 0 ||
            wire.actorId == 0 || wire.authorityEpoch == 0 ||
            wire.actorGeneration == 0 || wire.mapEpoch == 0 ||
            wire.structuralRevision == 0 || wire.mapId == 0 ||
            !IsRoleValid(static_cast<PeerRole>(wire.role)) ||
            !IsFinite(wire) ||
            !IsTerminated(wire.playerId) || !IsTerminated(wire.mapName) ||
            !IsTerminated(wire.appearanceDefinition) ||
            wire.playerId[0] == '\0' || wire.mapName[0] == '\0' ||
            wire.appearanceDefinition[0] == '\0' ||
            !IsSaneAppearance(wire) || !IsSaneEquipment(wire))
        {
            return false;
        }

        const bool appearanceChanged =
            (wire.componentFlags & fable::multiplayer::protocol::
                player_actor_state_flag::AppearanceChanged) != 0;
        const bool equipmentChanged =
            (wire.componentFlags & fable::multiplayer::protocol::
                player_actor_state_flag::EquipmentChanged) != 0;
        const bool appearancePresent =
            (wire.componentFlags & fable::multiplayer::protocol::
                player_actor_state_flag::AppearancePresent) != 0;
        const bool equipmentPresent =
            (wire.componentFlags & fable::multiplayer::protocol::
                player_actor_state_flag::EquipmentPresent) != 0;

        if (operation == PlayerActorStateOperation::Construct &&
            (!appearanceChanged || !equipmentChanged ||
                !appearancePresent || !equipmentPresent))
        {
            return false;
        }
        if (operation == PlayerActorStateOperation::ComponentDelta &&
            !appearanceChanged && !equipmentChanged)
        {
            return false;
        }
        if (operation != PlayerActorStateOperation::Construct &&
            operation != PlayerActorStateOperation::ComponentDelta &&
            wire.componentFlags != 0)
        {
            return false;
        }
        if ((!appearanceChanged && appearancePresent) ||
            (!equipmentChanged && equipmentPresent))
        {
            return false;
        }
        return true;
    }

    std::uint16_t QuantizeBoneScale(float value) noexcept
    {
        return static_cast<std::uint16_t>(
            std::lround(value * kBoneScaleQuantization));
    }

    float DequantizeBoneScale(std::uint16_t value) noexcept
    {
        return static_cast<float>(value) / kBoneScaleQuantization;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodePlayerActorStateMessage(
        const PlayerActorStateMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (destination == nullptr ||
            destinationCapacity < sizeof(WirePlayerActorStateMessage) ||
            !IsOperationValid(message.operation) || message.actorId == 0 ||
            message.authorityEpoch == 0 || message.actorGeneration == 0 ||
            message.mapEpoch == 0 || message.structuralRevision == 0 ||
            message.mapId == 0 || !IsRoleValid(message.role) ||
            !IsTextSane(message.playerId, kPlayerIdBytes) ||
            !IsTextSane(message.mapName, kMapNameBytes) ||
            !IsTextSane(message.appearanceDefinition,
                kAppearanceDefinitionBytes) ||
            !std::isfinite(message.initialPosition.x) ||
            !std::isfinite(message.initialPosition.y) ||
            !std::isfinite(message.initialPosition.z) ||
            !IsFacingSane(message.initialFacing) ||
            (message.componentFlags & ~player_actor_state_flag::All) != 0)
        {
            return false;
        }
        const bool appearanceChanged = message.AppearanceChanged();
        const bool equipmentChanged = message.EquipmentChanged();
        const bool appearancePresent = message.AppearancePresent();
        const bool equipmentPresent = message.EquipmentPresent();
        if (message.operation == PlayerActorStateOperation::Construct &&
            (!appearanceChanged || !equipmentChanged ||
                !appearancePresent || !equipmentPresent))
        {
            return false;
        }
        if (message.operation == PlayerActorStateOperation::ComponentDelta &&
            !appearanceChanged && !equipmentChanged)
        {
            return false;
        }
        if (message.operation != PlayerActorStateOperation::Construct &&
            message.operation != PlayerActorStateOperation::ComponentDelta &&
            message.componentFlags != 0)
        {
            return false;
        }
        if ((!appearanceChanged && appearancePresent) ||
            (!equipmentChanged && equipmentPresent) ||
            (appearancePresent &&
                (!message.heroMorph.IsSane() ||
                    !message.heroClothing.IsSane() ||
                    !message.heroBoneScales.IsSane() ||
                    !message.heroAppearanceModifiers.IsSane())) ||
            (equipmentPresent && !message.heroEquipment.IsSane()))
        {
            return false;
        }

        WirePlayerActorStateMessage wire;
        wire.operation = static_cast<std::uint8_t>(message.operation);
        wire.componentFlags = message.componentFlags;
        wire.role = static_cast<std::uint8_t>(message.role);
        wire.actorId = message.actorId;
        wire.authorityEpoch = message.authorityEpoch;
        wire.actorGeneration = message.actorGeneration;
        wire.mapEpoch = message.mapEpoch;
        wire.structuralRevision = message.structuralRevision;
        wire.mapId = message.mapId;
        wire.initialPosition[0] = message.initialPosition.x;
        wire.initialPosition[1] = message.initialPosition.y;
        wire.initialPosition[2] = message.initialPosition.z;
        wire.initialFacing = message.initialFacing;
        CopyText(wire.playerId, message.playerId);
        CopyText(wire.mapName, message.mapName);
        CopyText(wire.appearanceDefinition, message.appearanceDefinition);
        if (appearancePresent)
        {
            wire.heroMorph[0] = message.heroMorph.strength;
            wire.heroMorph[1] = message.heroMorph.berserk;
            wire.heroMorph[2] = message.heroMorph.will;
            wire.heroMorph[3] = message.heroMorph.skill;
            wire.heroMorph[4] = message.heroMorph.age;
            wire.heroMorph[5] = message.heroMorph.alignment;
            wire.heroMorph[6] = message.heroMorph.fatness;
            wire.heroMorph[7] = message.heroMorph.auxiliary;
            for (std::size_t index = 0;
                 index < HeroClothingState::SlotCount;
                 ++index)
            {
                wire.heroClothingDefinitionIndices[index] =
                    message.heroClothing.definitionIndices[index];
            }
            wire.heroAppearanceModifierCount = static_cast<std::uint16_t>(
                message.heroAppearanceModifiers.count);
            for (std::size_t index = 0;
                 index < message.heroAppearanceModifiers.count;
                 ++index)
            {
                wire.heroAppearanceModifierDefinitionIndices[index] =
                    message.heroAppearanceModifiers.definitionIndices[index];
            }
            wire.heroBoneScaleCount = static_cast<std::uint16_t>(
                message.heroBoneScales.count);
            for (std::size_t index = 0;
                 index < message.heroBoneScales.count;
                 ++index)
            {
                const auto& source = message.heroBoneScales.entries[index];
                auto& target = wire.heroBoneScales[index];
                target.boneIndex = source.boneIndex;
                target.x = QuantizeBoneScale(source.x);
                target.y = QuantizeBoneScale(source.y);
                target.z = QuantizeBoneScale(source.z);
            }
        }
        if (equipmentPresent)
        {
            wire.meleeWeaponDefinitionIndex =
                message.heroEquipment.meleeDefinitionIndex;
            wire.rangedWeaponDefinitionIndex =
                message.heroEquipment.rangedDefinitionIndex;
            wire.meleeWeaponAttachmentSlot =
                message.heroEquipment.meleeAttachmentSlot;
            wire.rangedWeaponAttachmentSlot =
                message.heroEquipment.rangedAttachmentSlot;
            wire.activeWeaponFamily = static_cast<std::uint8_t>(
                message.heroEquipment.activeFamily);
        }
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodePlayerActorStateMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerActorStateMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WirePlayerActorStateMessage))
        {
            return false;
        }
        WirePlayerActorStateMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (!IsSaneConstruction(wire))
        {
            return false;
        }
        message.operation = static_cast<PlayerActorStateOperation>(
            wire.operation);
        message.componentFlags = wire.componentFlags;
        message.actorId = wire.actorId;
        message.authorityEpoch = wire.authorityEpoch;
        message.actorGeneration = wire.actorGeneration;
        message.mapEpoch = wire.mapEpoch;
        message.structuralRevision = wire.structuralRevision;
        message.role = static_cast<PeerRole>(wire.role);
        message.mapId = wire.mapId;
        message.initialPosition = {
            wire.initialPosition[0], wire.initialPosition[1],
            wire.initialPosition[2]};
        message.initialFacing = wire.initialFacing;
        message.playerId = wire.playerId;
        message.mapName = wire.mapName;
        message.appearanceDefinition = wire.appearanceDefinition;
        if (message.AppearancePresent())
        {
            message.heroMorph.valid = true;
            message.heroMorph.strength = wire.heroMorph[0];
            message.heroMorph.berserk = wire.heroMorph[1];
            message.heroMorph.will = wire.heroMorph[2];
            message.heroMorph.skill = wire.heroMorph[3];
            message.heroMorph.age = wire.heroMorph[4];
            message.heroMorph.alignment = wire.heroMorph[5];
            message.heroMorph.fatness = wire.heroMorph[6];
            message.heroMorph.auxiliary = wire.heroMorph[7];
            for (std::size_t index = 0;
                 index < HeroClothingState::SlotCount;
                 ++index)
            {
                message.heroClothing.definitionIndices[index] =
                    wire.heroClothingDefinitionIndices[index];
            }
            message.heroClothing.valid = true;
            message.heroAppearanceModifiers.valid = true;
            message.heroAppearanceModifiers.count =
                wire.heroAppearanceModifierCount;
            for (std::size_t index = 0;
                 index < wire.heroAppearanceModifierCount;
                 ++index)
            {
                message.heroAppearanceModifiers.definitionIndices[index] =
                    wire.heroAppearanceModifierDefinitionIndices[index];
            }
            message.heroBoneScales.valid = true;
            message.heroBoneScales.count = wire.heroBoneScaleCount;
            for (std::size_t index = 0;
                 index < wire.heroBoneScaleCount;
                 ++index)
            {
                const auto& source = wire.heroBoneScales[index];
                auto& target = message.heroBoneScales.entries[index];
                target.boneIndex = source.boneIndex;
                target.x = DequantizeBoneScale(source.x);
                target.y = DequantizeBoneScale(source.y);
                target.z = DequantizeBoneScale(source.z);
            }
        }
        if (message.EquipmentPresent())
        {
            message.heroEquipment.valid = true;
            message.heroEquipment.meleeDefinitionIndex =
                wire.meleeWeaponDefinitionIndex;
            message.heroEquipment.rangedDefinitionIndex =
                wire.rangedWeaponDefinitionIndex;
            message.heroEquipment.meleeAttachmentSlot =
                wire.meleeWeaponAttachmentSlot;
            message.heroEquipment.rangedAttachmentSlot =
                wire.rangedWeaponAttachmentSlot;
            message.heroEquipment.activeFamily = static_cast<
                game::creature::equipment::CreatureWeaponFamily>(
                    wire.activeWeaponFamily);
        }
        return true;
    }
}
