#include "PlayerStateCodec.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

namespace
{
    constexpr float kBoneScaleQuantization = 4'095.0f;

#pragma pack(push, 1)
    struct WireHeroBoneScale final
    {
        std::uint16_t boneIndex = 0;
        std::uint16_t x = 4'095;
        std::uint16_t y = 4'095;
        std::uint16_t z = 4'095;
    };

    struct WirePlayerState final
    {
        std::uint32_t sequence = 0;
        std::uint32_t changedProperties = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t actorId = 0;
        std::uint8_t role = 0;
        std::uint8_t moving = 0;
        std::uint16_t mapId = 0;
        float position[3] = {};
        float velocity[3] = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
        char playerId[48] = {};
        char mapName[96] = {};
        char appearanceDefinition[96] = {};
        std::uint8_t appearanceValid = 0;
        std::uint8_t appearanceChild = 0;
        std::uint8_t equipmentValid = 0;
        std::uint8_t activeWeaponFamily = 0;
        std::int32_t meleeWeaponDefinitionIndex = 0;
        std::int32_t rangedWeaponDefinitionIndex = 0;
        std::uint32_t meleeWeaponAttachmentSlot = 0;
        std::uint32_t rangedWeaponAttachmentSlot = 0;
        float heroMorph[8] = {};
        std::int32_t heroClothingDefinitionIndices[
            fable::game::hero_pawn::appearance::
                HeroClothingState::SlotCount] = {};
        std::uint16_t heroAppearanceModifierCount = 0;
        std::int32_t heroAppearanceModifierDefinitionIndices[
            fable::game::hero_pawn::appearance::
                HeroAppearanceModifierState::MaximumEntries] = {};
        std::uint16_t heroBoneScaleCount = 0;
        WireHeroBoneScale heroBoneScales[
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries] = {};
    };
#pragma pack(pop)

    constexpr std::size_t kBaseSize =
        offsetof(WirePlayerState, heroBoneScales);
    static_assert(
        sizeof(WirePlayerState) <=
            fable::multiplayer::protocol::MaximumDatagramBytes -
                fable::multiplayer::protocol::PacketHeaderBytes,
        "The complete player appearance baseline must fit one packet payload.");

    template <std::size_t Size>
    void CopyText(char (&destination)[Size], const std::string& source)
    {
        static_assert(Size > 0);
        destination[0] = '\0';
        strncpy_s(destination, source.c_str(), _TRUNCATE);
    }

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size])
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsFinite(const WirePlayerState& state)
    {
        const bool morphFinite = std::all_of(
            std::begin(state.heroMorph),
            std::end(state.heroMorph),
            [](float value) { return std::isfinite(value); });
        return morphFinite && std::isfinite(state.position[0]) &&
            std::isfinite(state.position[1]) &&
            std::isfinite(state.position[2]) &&
            std::isfinite(state.velocity[0]) &&
            std::isfinite(state.velocity[1]) &&
            std::isfinite(state.velocity[2]) &&
            std::isfinite(state.facing) &&
            std::isfinite(state.angularVelocity);
    }

    bool IsSaneAppearance(const WirePlayerState& state)
    {
        const bool carriesAppearance =
            (state.changedProperties &
                fable::multiplayer::player_property::Appearance) != 0;
        if (!carriesAppearance)
        {
            return state.appearanceValid == 0 &&
                state.heroAppearanceModifierCount == 0 &&
                state.heroBoneScaleCount == 0;
        }
        if (state.appearanceValid != 1 || state.appearanceChild > 1 ||
            state.heroBoneScaleCount >
                fable::game::hero_pawn::appearance::HeroBoneScaleState::
                    MaximumEntries ||
            state.heroAppearanceModifierCount >
                fable::game::hero_pawn::appearance::
                    HeroAppearanceModifierState::MaximumEntries)
        {
            return false;
        }
        const auto unitValue = [](float value)
        {
            return value >= -0.001f && value <= 1.001f;
        };
        for (std::size_t index = 0; index < 7; ++index)
        {
            if (!unitValue(state.heroMorph[index]))
            {
                return false;
            }
        }
        for (const std::int32_t definitionIndex :
             state.heroClothingDefinitionIndices)
        {
            if (definitionIndex != -1 &&
                (definitionIndex <= 0 || definitionIndex >= 1'000'000))
            {
                return false;
            }
        }
        for (std::size_t index = 0;
             index < state.heroAppearanceModifierCount;
             ++index)
        {
            const std::int32_t definitionIndex =
                state.heroAppearanceModifierDefinitionIndices[index];
            if (definitionIndex <= 0 || definitionIndex >= 1'000'000)
            {
                return false;
            }
            for (std::size_t earlier = 0; earlier < index; ++earlier)
            {
                if (state.heroAppearanceModifierDefinitionIndices[earlier] ==
                    definitionIndex)
                {
                    return false;
                }
            }
        }
        for (std::size_t index = 0;
             index < state.heroBoneScaleCount;
             ++index)
        {
            const WireHeroBoneScale& scale = state.heroBoneScales[index];
            if (scale.boneIndex >= 1'024 ||
                scale.x > 16 * kBoneScaleQuantization ||
                scale.y > 16 * kBoneScaleQuantization ||
                scale.z > 16 * kBoneScaleQuantization)
            {
                return false;
            }
        }
        return state.heroMorph[7] >= -16.0f &&
            state.heroMorph[7] <= 16.0f;
    }

    bool IsSaneEquipment(const WirePlayerState& state)
    {
        const bool carriesEquipment =
            (state.changedProperties &
                fable::multiplayer::player_property::Equipment) != 0;
        if (!carriesEquipment)
        {
            return state.equipmentValid == 0;
        }
        const auto saneDefinition = [](std::int32_t definitionIndex)
        {
            return definitionIndex == -1 ||
                (definitionIndex > 0 && definitionIndex < 1'000'000);
        };
        using fable::game::creature::equipment::CreatureWeaponFamily;
        const auto family = static_cast<CreatureWeaponFamily>(
            state.activeWeaponFamily);
        const bool saneFamily = family == CreatureWeaponFamily::None ||
            family == CreatureWeaponFamily::Melee ||
            family == CreatureWeaponFamily::Ranged;
        const bool availableFamily = family == CreatureWeaponFamily::None ||
            (family == CreatureWeaponFamily::Melee &&
                state.meleeWeaponDefinitionIndex > 0) ||
            (family == CreatureWeaponFamily::Ranged &&
                state.rangedWeaponDefinitionIndex > 0);
        const auto saneAttachment = [](std::int32_t definitionIndex,
                                       std::uint32_t attachmentSlot)
        {
            return definitionIndex == -1
                ? attachmentSlot == 0
                : attachmentSlot < 1'000'000;
        };
        return state.equipmentValid == 1 && saneFamily && availableFamily &&
            saneDefinition(state.meleeWeaponDefinitionIndex) &&
            saneDefinition(state.rangedWeaponDefinitionIndex) &&
            saneAttachment(
                state.meleeWeaponDefinitionIndex,
                state.meleeWeaponAttachmentSlot) &&
            saneAttachment(
                state.rangedWeaponDefinitionIndex,
                state.rangedWeaponAttachmentSlot);
    }

    std::size_t PayloadSize(const WirePlayerState& state)
    {
        return kBaseSize +
            static_cast<std::size_t>(state.heroBoneScaleCount) *
                sizeof(WireHeroBoneScale);
    }

    std::uint16_t QuantizeBoneScale(float value)
    {
        return static_cast<std::uint16_t>(
            std::lround(value * kBoneScaleQuantization));
    }

    float DequantizeBoneScale(std::uint16_t value)
    {
        return static_cast<float>(value) / kBoneScaleQuantization;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodePlayerState(
        const PlayerState& state,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        const bool carriesAppearance =
            (state.changedProperties & player_property::Appearance) != 0;
        const bool carriesEquipment =
            (state.changedProperties & player_property::Equipment) != 0;
        if (state.actorId == 0 || state.authorityEpoch == 0 ||
            (state.role != PeerRole::Host && state.role != PeerRole::Guest) ||
            (state.changedProperties & ~player_property::All) != 0 ||
            !std::isfinite(state.position.x) ||
            !std::isfinite(state.position.y) ||
            !std::isfinite(state.position.z) ||
            !std::isfinite(state.velocity.x) ||
            !std::isfinite(state.velocity.y) ||
            !std::isfinite(state.velocity.z) ||
            !std::isfinite(state.facing) ||
            !std::isfinite(state.angularVelocity) ||
            (carriesAppearance &&
                (!state.heroMorph.IsSane() ||
                    !state.heroClothing.IsSane() ||
                    !state.heroBoneScales.IsSane() ||
                    !state.heroAppearanceModifiers.IsSane())) ||
            (carriesEquipment && !state.heroEquipment.IsSane()))
        {
            return false;
        }
        WirePlayerState packet;
        packet.sequence = state.sequence;
        packet.changedProperties = state.changedProperties;
        packet.authorityEpoch = state.authorityEpoch;
        packet.actorId = state.actorId;
        packet.role = static_cast<std::uint8_t>(state.role);
        packet.moving = state.moving ? 1 : 0;
        packet.mapId = state.mapId;
        packet.position[0] = state.position.x;
        packet.position[1] = state.position.y;
        packet.position[2] = state.position.z;
        packet.velocity[0] = state.velocity.x;
        packet.velocity[1] = state.velocity.y;
        packet.velocity[2] = state.velocity.z;
        packet.facing = state.facing;
        packet.angularVelocity = state.angularVelocity;
        CopyText(packet.playerId, state.playerId);
        CopyText(packet.mapName, state.mapName);
        CopyText(packet.appearanceDefinition, state.appearanceDefinition);
        if ((state.changedProperties & player_property::Appearance) != 0)
        {
            packet.appearanceValid = state.heroMorph.valid ? 1 : 0;
            packet.appearanceChild = state.heroMorph.child ? 1 : 0;
            packet.heroMorph[0] = state.heroMorph.strength;
            packet.heroMorph[1] = state.heroMorph.berserk;
            packet.heroMorph[2] = state.heroMorph.will;
            packet.heroMorph[3] = state.heroMorph.skill;
            packet.heroMorph[4] = state.heroMorph.age;
            packet.heroMorph[5] = state.heroMorph.alignment;
            packet.heroMorph[6] = state.heroMorph.fatness;
            packet.heroMorph[7] = state.heroMorph.auxiliary;
            for (std::size_t index = 0;
                 index < state.heroClothing.definitionIndices.size();
                 ++index)
            {
                packet.heroClothingDefinitionIndices[index] =
                    state.heroClothing.definitionIndices[index];
            }
            packet.heroAppearanceModifierCount =
                static_cast<std::uint16_t>(
                    state.heroAppearanceModifiers.count);
            for (std::size_t index = 0;
                 index < state.heroAppearanceModifiers.count;
                 ++index)
            {
                packet.heroAppearanceModifierDefinitionIndices[index] =
                    state.heroAppearanceModifiers.definitionIndices[index];
            }
            packet.heroBoneScaleCount = static_cast<std::uint16_t>(
                state.heroBoneScales.count);
            for (std::size_t index = 0;
                 index < state.heroBoneScales.count;
                 ++index)
            {
                const auto& source = state.heroBoneScales.entries[index];
                auto& target = packet.heroBoneScales[index];
                target.boneIndex = source.boneIndex;
                target.x = QuantizeBoneScale(source.x);
                target.y = QuantizeBoneScale(source.y);
                target.z = QuantizeBoneScale(source.z);
            }
        }
        if (carriesEquipment)
        {
            packet.equipmentValid = 1;
            packet.meleeWeaponDefinitionIndex =
                state.heroEquipment.meleeDefinitionIndex;
            packet.rangedWeaponDefinitionIndex =
                state.heroEquipment.rangedDefinitionIndex;
            packet.meleeWeaponAttachmentSlot =
                state.heroEquipment.meleeAttachmentSlot;
            packet.rangedWeaponAttachmentSlot =
                state.heroEquipment.rangedAttachmentSlot;
            packet.activeWeaponFamily = static_cast<std::uint8_t>(
                state.heroEquipment.activeFamily);
        }
        const std::size_t payloadSize = PayloadSize(packet);
        if (destination == nullptr || payloadSize > destinationCapacity)
        {
            return false;
        }
        std::memcpy(destination, &packet, payloadSize);
        encodedSize = payloadSize;
        return true;
    }

    bool DecodePlayerState(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerState& state) noexcept
    {
        state = {};
        if (bytes == nullptr || byteCount < kBaseSize ||
            byteCount > sizeof(WirePlayerState))
        {
            return false;
        }
        WirePlayerState packet;
        std::memcpy(&packet, bytes, byteCount);
        if ((packet.role != static_cast<std::uint8_t>(PeerRole::Host) &&
                packet.role != static_cast<std::uint8_t>(PeerRole::Guest)) ||
            packet.moving > 1 || packet.actorId == 0 ||
            packet.authorityEpoch == 0 ||
            packet.mapId == 0 ||
            packet.heroBoneScaleCount >
                game::hero_pawn::appearance::HeroBoneScaleState::
                    MaximumEntries ||
            PayloadSize(packet) != byteCount ||
            (packet.changedProperties & ~player_property::All) != 0 ||
            !IsFinite(packet) || !IsSaneAppearance(packet) ||
            !IsSaneEquipment(packet) ||
            !IsTerminated(packet.playerId) ||
            !IsTerminated(packet.mapName) ||
            !IsTerminated(packet.appearanceDefinition))
        {
            return false;
        }

        state.sequence = packet.sequence;
        state.changedProperties = packet.changedProperties;
        state.authorityEpoch = packet.authorityEpoch;
        state.actorId = packet.actorId;
        state.role = static_cast<PeerRole>(packet.role);
        state.moving = packet.moving != 0;
        state.mapId = packet.mapId;
        state.position = {
            packet.position[0], packet.position[1], packet.position[2]};
        state.velocity = {
            packet.velocity[0], packet.velocity[1], packet.velocity[2]};
        state.facing = packet.facing;
        state.angularVelocity = packet.angularVelocity;
        state.playerId = packet.playerId;
        state.mapName = packet.mapName;
        state.appearanceDefinition = packet.appearanceDefinition;
        state.heroMorph.valid = packet.appearanceValid != 0;
        state.heroMorph.child = packet.appearanceChild != 0;
        state.heroMorph.strength = packet.heroMorph[0];
        state.heroMorph.berserk = packet.heroMorph[1];
        state.heroMorph.will = packet.heroMorph[2];
        state.heroMorph.skill = packet.heroMorph[3];
        state.heroMorph.age = packet.heroMorph[4];
        state.heroMorph.alignment = packet.heroMorph[5];
        state.heroMorph.fatness = packet.heroMorph[6];
        state.heroMorph.auxiliary = packet.heroMorph[7];
        state.heroClothing.valid = packet.appearanceValid != 0;
        for (std::size_t index = 0;
             index < state.heroClothing.definitionIndices.size();
             ++index)
        {
            state.heroClothing.definitionIndices[index] =
                packet.heroClothingDefinitionIndices[index];
        }
        state.heroAppearanceModifiers.valid = packet.appearanceValid != 0;
        state.heroAppearanceModifiers.count =
            packet.heroAppearanceModifierCount;
        for (std::size_t index = 0;
             index < packet.heroAppearanceModifierCount;
             ++index)
        {
            state.heroAppearanceModifiers.definitionIndices[index] =
                packet.heroAppearanceModifierDefinitionIndices[index];
        }
        state.heroBoneScales.valid = packet.appearanceValid != 0;
        state.heroBoneScales.count = packet.heroBoneScaleCount;
        for (std::size_t index = 0;
             index < packet.heroBoneScaleCount;
             ++index)
        {
            const auto& source = packet.heroBoneScales[index];
            auto& target = state.heroBoneScales.entries[index];
            target.boneIndex = source.boneIndex;
            target.x = DequantizeBoneScale(source.x);
            target.y = DequantizeBoneScale(source.y);
            target.z = DequantizeBoneScale(source.z);
        }
        if ((packet.changedProperties & player_property::Equipment) != 0)
        {
            state.heroEquipment.valid = packet.equipmentValid != 0;
            state.heroEquipment.meleeDefinitionIndex =
                packet.meleeWeaponDefinitionIndex;
            state.heroEquipment.rangedDefinitionIndex =
                packet.rangedWeaponDefinitionIndex;
            state.heroEquipment.meleeAttachmentSlot =
                packet.meleeWeaponAttachmentSlot;
            state.heroEquipment.rangedAttachmentSlot =
                packet.rangedWeaponAttachmentSlot;
            state.heroEquipment.activeFamily = static_cast<
                game::creature::equipment::CreatureWeaponFamily>(
                    packet.activeWeaponFamily);
        }
        return true;
    }
}
