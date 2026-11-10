#include "Multiplayer/Replication/PlayerActions/LocalPlayerActionCapture.h"

#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Presentation/RemotePlayerActionPresentation.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/PlayerActionSemantics.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <cstdio>
#include <utility>

namespace fable::multiplayer::replication
{
    bool LocalPlayerActionCapture::CaptureLocal(
        const game::creature::combat::CreatureAbilityEvent& event,
        const game::creature::actions::CreatureActionLifecycleEvent*
            resolvedAction)
    {
        if (event.abilityId == 0 || event.sourceCreature == nullptr ||
            localHero_ == nullptr ||
            event.sourceCreature != localHero_->NativeHero())
        {
            return true;
        }
        if (event.attackCommand &&
            (resolvedAction == nullptr || !resolvedAction->accepted ||
                resolvedAction->phase != game::creature::actions::
                    CreatureActionLifecyclePhase::Submitted))
        {
            return true;
        }
        const PlayerState* const state = localHero_->CurrentState();
        if (state == nullptr || state->actorId != localActorId_ ||
            state->authorityEpoch == 0 || state->actorGeneration == 0 ||
            state->mapEpoch == 0 || state->mapName.empty())
        {
            return true;
        }

        protocol::PlayerActionMessage message;
        message.phase = role_ == PeerRole::Host
            ? protocol::PlayerActionPhase::Perform
            : protocol::PlayerActionPhase::Intent;
        message.kind = protocol::PlayerActionKind::AbilityRequest;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.abilityId = event.abilityId;
        message.charge = event.charge;
        game::hero_pawn::equipment::HeroEquipmentState liveEquipment;
        if (!game::hero_pawn::equipment::native::HeroWeaponComponent::Capture(
                localHero_->NativeHero(), liveEquipment))
        {
            liveEquipment = state->heroEquipment;
        }
        if (liveEquipment.IsSane())
        {
            message.weaponFamily = liveEquipment.activeFamily;
            message.requiredWeapons = liveEquipment.WeaponDefinitions();
            message.requiredMeleeAttachmentSlot =
                liveEquipment.meleeAttachmentSlot;
            message.requiredRangedAttachmentSlot =
                liveEquipment.rangedAttachmentSlot;
        }
        const bool rangedAttack = resolvedAction != nullptr &&
            player_action_semantics::IsRangedFire(
                resolvedAction->actionType);
        message.weaponFamily =
            player_action_semantics::ResolveCapturedWeaponFamily(
                message.weaponFamily,
                event.attackCommand,
                resolvedAction != nullptr ? resolvedAction->actionType : nullptr,
                message.requiredWeapons.rangedDefinitionIndex > 0);
        message.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(event.targetCreature)
            : 0;
        message.targetThingUid = message.targetPlayerActorId == 0
            ? (identities_ != nullptr
                ? identities_->CanonicalizeLocalObservation(
                    event.targetThingUid)
                : event.targetThingUid)
            : 0;
        message.mapName = state->mapName;
        message.semanticName = event.attackCommand
            ? (rangedAttack ? "RangedAttackAbility" : "AttackAbility")
            : "CreatureAbility";
        if (resolvedAction != nullptr)
        {
            message.resolvedAnimationId = resolvedAction->animationId;
            message.resolvedActionType = resolvedAction->actionType;
        }
        if (!EnsurePresentationTiming(
                message,
                event.observedAt,
                presentation::RemotePlayerActionPresentation::DefaultDurationMs(
                    message.kind)))
        {
            return true;
        }
        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu ability_id=%u charge=%.3f weapon=%u melee=%d ranged=%d target_player=%llu target_uid=%016llX semantic=%s native_action=%s animation_id=%u map=%s phase=%s",
            static_cast<unsigned long long>(message.ownerActorId),
            static_cast<unsigned long long>(message.actionId),
            message.abilityId,
            message.charge,
            static_cast<unsigned int>(message.weaponFamily),
            message.requiredWeapons.meleeDefinitionIndex,
            message.requiredWeapons.rangedDefinitionIndex,
            static_cast<unsigned long long>(message.targetPlayerActorId),
            static_cast<unsigned long long>(message.targetThingUid),
            message.semanticName.c_str(),
            message.resolvedActionType.empty()
                ? "<unresolved>" : message.resolvedActionType.c_str(),
            message.resolvedAnimationId,
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform" : "intent");
        diagnostics_.Event("MultiplayerLocalPlayerActionCaptured", detail);
        return Queue(std::move(message));
    }

    bool LocalPlayerActionCapture::CaptureLocalRangedAction(
        const game::creature::actions::CreatureActionLifecycleEvent& action)
    {
        if (!action.accepted || action.creature == nullptr ||
            localHero_ == nullptr || action.creature != localHero_->NativeHero() ||
            action.animationId == 0)
        {
            return true;
        }
        const PlayerState* const state = localHero_->CurrentState();
        if (state == nullptr || state->actorId != localActorId_ ||
            state->authorityEpoch == 0 || state->actorGeneration == 0 ||
            state->mapEpoch == 0 || state->mapName.empty())
        {
            return true;
        }
        game::hero_pawn::equipment::HeroEquipmentState equipment;
        if (!game::hero_pawn::equipment::native::HeroWeaponComponent::Capture(
                localHero_->NativeHero(), equipment))
        {
            equipment = state->heroEquipment;
        }
        if (!equipment.IsSane() || equipment.rangedDefinitionIndex <= 0)
        {
            diagnostics_.Event(
                "MultiplayerLocalRangedShotSuppressed",
                "accepted FireMissileWeapon had no readable ranged loadout");
            return true;
        }

        protocol::PlayerActionMessage message;
        message.phase = role_ == PeerRole::Host
            ? protocol::PlayerActionPhase::Perform
            : protocol::PlayerActionPhase::Intent;
        const bool aimStart = player_action_semantics::IsRangedAimStart(
            action.actionType);
        message.kind = aimStart
            ? protocol::PlayerActionKind::RangedAim
            : protocol::PlayerActionKind::AbilityRequest;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.abilityId = aimStart
            ? 0 : player_action_semantics::HeroAttackAbilityId;
        message.weaponFamily =
            game::creature::equipment::CreatureWeaponFamily::Ranged;
        message.requiredWeapons = equipment.WeaponDefinitions();
        message.requiredMeleeAttachmentSlot = equipment.meleeAttachmentSlot;
        message.requiredRangedAttachmentSlot = equipment.rangedAttachmentSlot;
        message.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(action.targetCreature) : 0;
        message.targetThingUid = message.targetPlayerActorId == 0
            ? (identities_ != nullptr
                ? identities_->CanonicalizeLocalObservation(
                    action.targetThingUid)
                : action.targetThingUid)
            : 0;
        message.mapName = state->mapName;
        message.semanticName = aimStart
            ? "RangedAimStart" : "RangedAttackAbility";
        message.resolvedAnimationId = action.animationId;
        message.resolvedActionType = action.actionType;
        if (!EnsurePresentationTiming(
                message, action.observedAt,
                presentation::RemotePlayerActionPresentation::DefaultDurationMs(
                    message.kind)))
        {
            return true;
        }
        char detail[448] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu ranged=%d target_player=%llu target_uid=%016llX native_action=%s animation_id=%u map=%s phase=%s",
            static_cast<unsigned long long>(message.ownerActorId),
            static_cast<unsigned long long>(message.actionId),
            message.requiredWeapons.rangedDefinitionIndex,
            static_cast<unsigned long long>(message.targetPlayerActorId),
            static_cast<unsigned long long>(message.targetThingUid),
            message.resolvedActionType.c_str(),
            message.resolvedAnimationId,
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform" : "intent");
        diagnostics_.Event(
            aimStart ? "MultiplayerLocalRangedAimCaptured"
                     : "MultiplayerLocalRangedShotCaptured", detail);
        return Queue(std::move(message));
    }

    bool LocalPlayerActionCapture::CaptureLocalRangedAimEnd(
        const game::creature::locomotion::CreatureModeSourceEvent& event)
    {
        if (event.owner == nullptr || localHero_ == nullptr ||
            event.owner != localHero_->NativeHero() || event.source != 25 ||
            event.added || !event.changed)
        {
            return true;
        }
        const PlayerState* const state = localHero_->CurrentState();
        if (state == nullptr || state->actorId != localActorId_ ||
            state->authorityEpoch == 0 || state->actorGeneration == 0 ||
            state->mapEpoch == 0 || state->mapName.empty())
        {
            return true;
        }
        protocol::PlayerActionMessage message;
        message.phase = role_ == PeerRole::Host
            ? protocol::PlayerActionPhase::Perform
            : protocol::PlayerActionPhase::Intent;
        message.kind = protocol::PlayerActionKind::RangedAimEnd;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.mapName = state->mapName;
        message.semanticName = "RangedAimEnd";
        if (!EnsurePresentationTiming(
                message, event.observedAt,
                presentation::RemotePlayerActionPresentation::DefaultDurationMs(
                    message.kind)))
        {
            return true;
        }
        diagnostics_.Event(
            "MultiplayerLocalRangedAimEndCaptured",
            "native Hero removed ranged movement source 25");
        return Queue(std::move(message));
    }

    bool LocalPlayerActionCapture::Queue(
        protocol::PlayerActionMessage message)
    {
        if (pendingMessages_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded local action capture queue is full");
            return false;
        }
        pendingMessages_.push_back(std::move(message));
        return true;
    }

    bool LocalPlayerActionCapture::EnsurePresentationTiming(
        protocol::PlayerActionMessage& message,
        const std::uint64_t observedAt,
        const std::uint32_t durationMs)
    {
        return transport_ != nullptr &&
            presentation::RemotePlayerActionPresentation::EnsureTiming(
                message, *transport_, observedAt, durationMs,
                nextPresentationRevision_);
    }

    std::uint64_t LocalPlayerActionCapture::NextActionId() noexcept
    {
        ++nextActionId_;
        if (nextActionId_ == 0)
        {
            ++nextActionId_;
        }
        return nextActionId_;
    }

}
