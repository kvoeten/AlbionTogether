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
    bool LocalPlayerActionCapture::CaptureLocalExpression(
        const game::creature::actions::CreatureActionLifecycleEvent& action)
    {
        void* const localHero = localHero_ != nullptr
            ? localHero_->NativeHero() : nullptr;
        const PlayerState* const state = localHero_ != nullptr
            ? localHero_->CurrentState() : nullptr;
        if (!action.accepted || action.creature == nullptr ||
            action.creature != localHero || action.expressionName[0] == '\0' ||
            action.actionType[0] == '\0' || state == nullptr ||
            state->actorId != localActorId_ || state->authorityEpoch == 0 ||
            state->actorGeneration == 0 || state->mapEpoch == 0 ||
            state->mapName.empty())
        {
            return true;
        }

        protocol::PlayerActionMessage message;
        message.phase = role_ == PeerRole::Host
            ? protocol::PlayerActionPhase::Perform
            : protocol::PlayerActionPhase::Intent;
        message.kind = protocol::PlayerActionKind::Expression;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(action.targetCreature) : 0;
        message.targetThingUid = message.targetPlayerActorId == 0
            ? (identities_ != nullptr
                ? identities_->CanonicalizeLocalObservation(
                    action.targetThingUid)
                : action.targetThingUid)
            : 0;
        message.mapName = state->mapName;
        message.semanticName = action.expressionName;
        message.resolvedActionType = action.actionType;
        message.resolvedAnimationId = action.animationId;
        message.expressionDurationTicks = action.expressionDurationTicks;
        message.expressionTriggerTicks = action.expressionTriggerTicks;
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
            "actor_id=%llu action_id=%llu expression=%s target_player=%llu target_uid=%016llX native_action=%s animation_id=%u duration_ticks=%d trigger_ticks=%d map=%s phase=%s",
            static_cast<unsigned long long>(message.ownerActorId),
            static_cast<unsigned long long>(message.actionId),
            message.semanticName.c_str(),
            static_cast<unsigned long long>(message.targetPlayerActorId),
            static_cast<unsigned long long>(message.targetThingUid),
            message.resolvedActionType.c_str(),
            message.resolvedAnimationId,
            message.expressionDurationTicks,
            message.expressionTriggerTicks,
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform" : "intent");
        diagnostics_.Event("MultiplayerLocalExpressionCaptured", detail);
        return Queue(std::move(message));
    }

    bool LocalPlayerActionCapture::CaptureLocalHeroAbility(
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (localHero_ == nullptr || event.sourceCreature == nullptr ||
            event.sourceCreature != localHero_->NativeHero() ||
            !game::hero_pawn::abilities::IsValid(event.ability) ||
            !game::hero_pawn::abilities::IsValid(event.command))
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
        message.kind = protocol::PlayerActionKind::HeroAbility;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.abilityId = static_cast<std::uint32_t>(event.ability);
        message.heroAbilityCommand = event.command;
        message.heroAbilityProgressionState = event.progressionState;
        message.targetPlayerActorId = combatants_ != nullptr
            ? combatants_->FindActor(event.targetCreature) : 0;
        message.targetThingUid = message.targetPlayerActorId == 0
            ? (identities_ != nullptr
                ? identities_->CanonicalizeLocalObservation(
                    event.targetThingUid)
                : event.targetThingUid)
            : 0;
        message.mapName = state->mapName;
        message.semanticName = "HeroAbility";
        if (!EnsurePresentationTiming(
                message, event.observedAt,
                presentation::RemotePlayerActionPresentation::DefaultDurationMs(
                    message.kind)))
        {
            return true;
        }
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu ability_id=%u name=%s command=%u progression_state=%d target_player=%llu target_uid=%016llX map=%s phase=%s",
            static_cast<unsigned long long>(message.ownerActorId),
            static_cast<unsigned long long>(message.actionId),
            message.abilityId,
            game::hero_pawn::abilities::Name(event.ability),
            static_cast<unsigned int>(message.heroAbilityCommand),
            message.heroAbilityProgressionState,
            static_cast<unsigned long long>(message.targetPlayerActorId),
            static_cast<unsigned long long>(message.targetThingUid),
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform" : "intent");
        diagnostics_.Event("MultiplayerLocalHeroAbilityCaptured", detail);
        const std::uint64_t actionId = message.actionId;
        const bool queued = Queue(std::move(message));
        if (queued)
        {
            char queuedDetail[160] = {};
            std::snprintf(
                queuedDetail,
                sizeof(queuedDetail),
                "actor_id=%llu action_id=%llu pending=%zu",
                static_cast<unsigned long long>(localActorId_),
                static_cast<unsigned long long>(actionId),
                pendingMessages_.size());
            diagnostics_.Event(
                "MultiplayerLocalHeroAbilityQueued", queuedDetail);
        }
        return queued;
    }

}
