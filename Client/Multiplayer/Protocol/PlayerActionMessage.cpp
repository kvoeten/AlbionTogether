#include "PlayerActionMessage.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace
{
#pragma pack(push, 1)
    struct WirePlayerActionMessage final
    {
        std::uint8_t phase = 0;
        std::uint8_t kind = 0;
        std::uint8_t weaponFamily = 0;
        std::uint8_t heroAbilityCommand = 0;
        std::int32_t heroAbilityProgressionState = -1;
        std::uint64_t ownerActorId = 0;
        std::uint64_t actionId = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        fable::multiplayer::protocol::SessionTimeMs startedAtSessionTimeMs =
            fable::multiplayer::protocol::SessionTimeUnset;
        std::uint32_t expectedDurationMs = 0;
        std::uint32_t presentationRevision = 0;
        std::uint32_t abilityId = 0;
        std::int32_t requiredMeleeDefinitionIndex = -1;
        std::int32_t requiredRangedDefinitionIndex = -1;
        std::uint32_t requiredMeleeAttachmentSlot = 0;
        std::uint32_t requiredRangedAttachmentSlot = 0;
        std::uint32_t resolvedAnimationId = 0;
        std::int32_t expressionDurationTicks = 0;
        std::int32_t expressionTriggerTicks = 0;
        float charge = 0.0f;
        std::uint64_t targetPlayerActorId = 0;
        std::uint64_t targetThingUid = 0;
        char mapName[96] = {};
        char semanticName[128] = {};
        char resolvedActionType[128] = {};
    };
#pragma pack(pop)

    static_assert(std::is_trivially_copyable_v<WirePlayerActionMessage>);
    static_assert(sizeof(WirePlayerActionMessage) == 452);
    static_assert(
        sizeof(WirePlayerActionMessage) <=
            fable::multiplayer::protocol::MaximumReliableMessageBytes);

    constexpr std::uint32_t kMaximumPresentationDurationMs = 120'000;
    constexpr std::uint32_t kHeroAttackAbilityId = 1101;

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size]) noexcept
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsSane(
        const fable::multiplayer::protocol::PlayerActionMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        using fable::game::creature::equipment::CreatureWeaponFamily;
        const bool saneFamily =
            message.weaponFamily == CreatureWeaponFamily::None ||
            message.weaponFamily == CreatureWeaponFamily::Melee ||
            message.weaponFamily == CreatureWeaponFamily::Ranged;
        const bool ability =
            message.kind == PlayerActionKind::AbilityRequest;
        const bool rangedAim =
            message.kind == PlayerActionKind::RangedAim;
        const bool weaponTransition =
            message.kind == PlayerActionKind::WeaponTransition;
        const bool heroAbility =
            message.kind == PlayerActionKind::HeroAbility;
        const bool rangedAimEnd =
            message.kind == PlayerActionKind::RangedAimEnd;
        const bool expression =
            message.kind == PlayerActionKind::Expression;
        const bool hasPresentationTiming =
            message.startedAtSessionTimeMs != SessionTimeUnset ||
            message.expectedDurationMs != 0 || message.presentationRevision != 0;
        const bool sanePresentationTiming = !hasPresentationTiming ||
            (message.startedAtSessionTimeMs != SessionTimeUnset &&
                message.presentationRevision != 0 &&
                message.expectedDurationMs <= kMaximumPresentationDurationMs);
        const bool saneExpressionTiming = expression
            ? message.expressionDurationTicks > 0 &&
                message.expressionDurationTicks <= 1'000'000 &&
                message.expressionTriggerTicks >= 0 &&
                message.expressionTriggerTicks <= 1'000'000
            : message.expressionDurationTicks == 0 &&
                message.expressionTriggerTicks == 0;
        const bool saneProgressionState = heroAbility
            ? message.heroAbilityProgressionState >= 0 &&
                message.heroAbilityProgressionState <= 3
            : message.heroAbilityProgressionState == -1;
        // A weaponless Hero attack is only valid when it carries the native
        // auto-turn action observed from the owner. Without that semantic
        // action the receiver would incorrectly fall through to the generic
        // ability path and lose Fable's fist-attack animation and hit flow.
        const bool saneUnarmedAttack =
            !(ability && message.abilityId == kHeroAttackAbilityId &&
                message.weaponFamily == CreatureWeaponFamily::None) ||
            message.resolvedActionType.find(
                "InterruptableMidAttackAutoTurn") != std::string::npos;
        const auto saneAttachment = [](std::int32_t definitionIndex,
                                       std::uint32_t attachmentSlot)
        {
            return definitionIndex == -1
                ? attachmentSlot == 0
                : attachmentSlot < 1'000'000;
        };
        return (message.phase == PlayerActionPhase::Intent ||
                message.phase == PlayerActionPhase::Perform) &&
            (ability || rangedAim || weaponTransition || heroAbility ||
                rangedAimEnd || expression) &&
            saneFamily &&
            saneProgressionState &&
            saneUnarmedAttack &&
            saneExpressionTiming &&
            sanePresentationTiming &&
            message.requiredWeapons.IsSane() &&
            message.requiredWeapons.Supports(message.weaponFamily) &&
            saneAttachment(
                message.requiredWeapons.meleeDefinitionIndex,
                message.requiredMeleeAttachmentSlot) &&
            saneAttachment(
                message.requiredWeapons.rangedDefinitionIndex,
                message.requiredRangedAttachmentSlot) &&
            message.ownerActorId != 0 && message.actionId != 0 &&
            message.authorityEpoch != 0 && message.actorGeneration != 0 &&
            message.mapEpoch != 0 &&
            ((ability && message.abilityId != 0 &&
                    message.abilityId < 1'000'000) ||
                (heroAbility && message.abilityId >= 1 &&
                    message.abilityId <= 19 &&
                    fable::game::hero_pawn::abilities::IsValid(
                        message.heroAbilityCommand)) ||
                (rangedAim && message.abilityId == 0 &&
                    message.weaponFamily == CreatureWeaponFamily::Ranged &&
                    message.targetPlayerActorId == 0 &&
                    message.targetThingUid == 0 &&
                    message.resolvedAnimationId != 0 &&
                    !message.resolvedActionType.empty()) ||
                (weaponTransition && message.abilityId == 0 &&
                    message.targetPlayerActorId == 0 &&
                    message.targetThingUid == 0 &&
                    message.resolvedAnimationId != 0) ||
                (rangedAimEnd && message.abilityId == 0 &&
                    message.weaponFamily == CreatureWeaponFamily::None &&
                    message.targetPlayerActorId == 0 &&
                    message.targetThingUid == 0 &&
                    message.resolvedAnimationId == 0 &&
                    message.resolvedActionType.empty()) ||
                (expression && message.abilityId == 0 &&
                    message.weaponFamily == CreatureWeaponFamily::None &&
                    message.charge == 0.0f &&
                    message.resolvedAnimationId != 0 &&
                    !message.resolvedActionType.empty())) &&
            (message.targetPlayerActorId == 0 ||
                message.targetThingUid == 0) &&
            std::isfinite(message.charge) &&
            message.charge >= -100.0f && message.charge <= 100.0f &&
            !message.mapName.empty() && message.mapName.size() < 96 &&
            !message.semanticName.empty() &&
            message.semanticName.size() < 128 &&
            message.resolvedAnimationId <= 0xFFFF &&
            message.resolvedActionType.size() < 128;
    }
}

namespace fable::multiplayer::protocol
{
    bool EncodePlayerActionMessage(
        const PlayerActionMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept
    {
        encodedSize = 0;
        if (!IsSane(message) || destination == nullptr ||
            destinationCapacity < sizeof(WirePlayerActionMessage))
        {
            return false;
        }
        WirePlayerActionMessage wire;
        wire.phase = static_cast<std::uint8_t>(message.phase);
        wire.kind = static_cast<std::uint8_t>(message.kind);
        wire.weaponFamily = static_cast<std::uint8_t>(message.weaponFamily);
        wire.heroAbilityCommand = static_cast<std::uint8_t>(
            message.heroAbilityCommand);
        wire.heroAbilityProgressionState = message.heroAbilityProgressionState;
        wire.ownerActorId = message.ownerActorId;
        wire.actionId = message.actionId;
        wire.authorityEpoch = message.authorityEpoch;
        wire.actorGeneration = message.actorGeneration;
        wire.mapEpoch = message.mapEpoch;
        wire.startedAtSessionTimeMs = message.startedAtSessionTimeMs;
        wire.expectedDurationMs = message.expectedDurationMs;
        wire.presentationRevision = message.presentationRevision;
        wire.abilityId = message.abilityId;
        wire.requiredMeleeDefinitionIndex =
            message.requiredWeapons.meleeDefinitionIndex;
        wire.requiredRangedDefinitionIndex =
            message.requiredWeapons.rangedDefinitionIndex;
        wire.requiredMeleeAttachmentSlot =
            message.requiredMeleeAttachmentSlot;
        wire.requiredRangedAttachmentSlot =
            message.requiredRangedAttachmentSlot;
        wire.resolvedAnimationId = message.resolvedAnimationId;
        wire.expressionDurationTicks = message.expressionDurationTicks;
        wire.expressionTriggerTicks = message.expressionTriggerTicks;
        wire.charge = message.charge;
        wire.targetPlayerActorId = message.targetPlayerActorId;
        wire.targetThingUid = message.targetThingUid;
        strncpy_s(wire.mapName, message.mapName.c_str(), _TRUNCATE);
        strncpy_s(wire.semanticName, message.semanticName.c_str(), _TRUNCATE);
        strncpy_s(
            wire.resolvedActionType,
            message.resolvedActionType.c_str(),
            _TRUNCATE);
        std::memcpy(destination, &wire, sizeof(wire));
        encodedSize = sizeof(wire);
        return true;
    }

    bool DecodePlayerActionMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PlayerActionMessage& message) noexcept
    {
        message = {};
        if (bytes == nullptr || byteCount != sizeof(WirePlayerActionMessage))
        {
            return false;
        }
        WirePlayerActionMessage wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        if (!IsTerminated(wire.mapName) ||
            !IsTerminated(wire.semanticName) ||
            !IsTerminated(wire.resolvedActionType))
        {
            return false;
        }
        message.phase = static_cast<PlayerActionPhase>(wire.phase);
        message.kind = static_cast<PlayerActionKind>(wire.kind);
        message.weaponFamily = static_cast<
            game::creature::equipment::CreatureWeaponFamily>(
                wire.weaponFamily);
        message.heroAbilityCommand = static_cast<
            game::hero_pawn::abilities::HeroAbilityCommand>(
                wire.heroAbilityCommand);
        message.heroAbilityProgressionState = wire.heroAbilityProgressionState;
        message.ownerActorId = wire.ownerActorId;
        message.actionId = wire.actionId;
        message.authorityEpoch = wire.authorityEpoch;
        message.actorGeneration = wire.actorGeneration;
        message.mapEpoch = wire.mapEpoch;
        message.startedAtSessionTimeMs = wire.startedAtSessionTimeMs;
        message.expectedDurationMs = wire.expectedDurationMs;
        message.presentationRevision = wire.presentationRevision;
        message.abilityId = wire.abilityId;
        message.requiredWeapons.meleeDefinitionIndex =
            wire.requiredMeleeDefinitionIndex;
        message.requiredWeapons.rangedDefinitionIndex =
            wire.requiredRangedDefinitionIndex;
        message.requiredMeleeAttachmentSlot =
            wire.requiredMeleeAttachmentSlot;
        message.requiredRangedAttachmentSlot =
            wire.requiredRangedAttachmentSlot;
        message.resolvedAnimationId = wire.resolvedAnimationId;
        message.expressionDurationTicks = wire.expressionDurationTicks;
        message.expressionTriggerTicks = wire.expressionTriggerTicks;
        message.charge = wire.charge;
        message.targetPlayerActorId = wire.targetPlayerActorId;
        message.targetThingUid = wire.targetThingUid;
        message.mapName = wire.mapName;
        message.semanticName = wire.semanticName;
        message.resolvedActionType = wire.resolvedActionType;
        return IsSane(message);
    }
}
