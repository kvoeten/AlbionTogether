#include "LocalPlayerActionCapture.h"

#include "PlayerActionSemantics.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Presentation/RemotePlayerActionPresentation.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace
{
    constexpr std::uint32_t HeroAttackAbilityId = 1101;
}

namespace fable::multiplayer::replication
{
    void LocalPlayerActionCapture::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        UdpPeer& transport,
        LocalHeroReplication& localHero,
        combat::PlayerCombatantDirectory& combatants,
        entities::EntityNetworkIdentityRegistry& identities,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        localHero_ = &localHero;
        combatants_ = &combatants;
        identities_ = &identities;
        combat_ = &combat;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        eventQueue_.SetAccepting(true);
        if (!combat_->AddAbilitySink(
                &LocalPlayerActionCapture::CaptureAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-ability-observer");
            Shutdown();
            return;
        }
        if (!abilities_->AddEventSink(
                &LocalPlayerActionCapture::CaptureHeroAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-hero-ability-observer");
            Shutdown();
            return;
        }
        initialized_ = true;
    }

    bool LocalPlayerActionCapture::AttachActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled() ||
            !observer.AddEventSink(
                &LocalPlayerActionCapture::CaptureAction, this))
        {
            return false;
        }
        actionObserver_ = &observer;
        return true;
    }

    bool LocalPlayerActionCapture::AttachModeObserver(
        game::creature::locomotion::CreatureModeManagerObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled() ||
            !observer.AddModeSourceEventSink(
                &LocalPlayerActionCapture::CaptureModeSource, this))
        {
            return false;
        }
        modeObserver_ = &observer;
        return true;
    }

    bool LocalPlayerActionCapture::CapturePending()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            combat_ == nullptr || abilities_ == nullptr)
        {
            return false;
        }
        return PairAcceptedLocalActions();
    }

    const protocol::PlayerActionMessage*
        LocalPlayerActionCapture::PendingFront() const noexcept
    {
        return pendingMessages_.empty() ? nullptr : &pendingMessages_.front();
    }

    void LocalPlayerActionCapture::PopPending() noexcept
    {
        if (!pendingMessages_.empty())
        {
            pendingMessages_.pop_front();
        }
    }

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
        if (rangedAttack &&
            message.requiredWeapons.rangedDefinitionIndex > 0)
        {
            message.weaponFamily =
                game::creature::equipment::CreatureWeaponFamily::Ranged;
        }
        else if (event.attackCommand &&
            message.weaponFamily ==
                game::creature::equipment::CreatureWeaponFamily::None &&
            message.requiredWeapons.meleeDefinitionIndex > 0)
        {
            message.weaponFamily =
                game::creature::equipment::CreatureWeaponFamily::Melee;
        }
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

    bool LocalPlayerActionCapture::CaptureLocalWeaponTransition(
        const game::creature::actions::CreatureActionLifecycleEvent& action,
        const game::hero_pawn::equipment::HeroEquipmentState& equipment)
    {
        const PlayerState* const state = localHero_->CurrentState();
        if (!action.accepted || action.animationId == 0 ||
            action.creature == nullptr ||
            action.creature != localHero_->NativeHero() ||
            !equipment.IsSane() || state == nullptr ||
            state->actorId != localActorId_ || state->authorityEpoch == 0 ||
            state->actorGeneration == 0 || state->mapEpoch == 0 ||
            state->mapName.empty())
        {
            return true;
        }

        const std::uint64_t transitionActionId = NextActionId();
        const protocol::SessionTimeMs startedAt = protocol::ToSessionTime(
            transport_->LocalToSessionTimeMilliseconds(action.observedAt));
        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu family=%u melee=%d melee_slot=%u ranged=%d ranged_slot=%u native_action=%s animation_id=%u map=%s phase=%s",
            static_cast<unsigned long long>(localActorId_),
            static_cast<unsigned long long>(transitionActionId),
            static_cast<unsigned int>(equipment.activeFamily),
            equipment.meleeDefinitionIndex,
            equipment.meleeAttachmentSlot,
            equipment.rangedDefinitionIndex,
            equipment.rangedAttachmentSlot,
            action.actionType,
            action.animationId,
            state->mapName.c_str(),
            role_ == PeerRole::Host ? "perform" : "intent");
        diagnostics_.Event("MultiplayerLocalWeaponTransitionCaptured", detail);
        localHero_->MarkEquipmentTransition(
            transitionActionId,
            startedAt,
            action.animationId,
            WeaponTransitionDurationMilliseconds,
            WeaponAttachmentNotifyMilliseconds);
        return true;
    }

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
        message.abilityId = aimStart ? 0 : HeroAttackAbilityId;
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

    bool LocalPlayerActionCapture::PairAcceptedLocalActions()
    {
        PlayerActionEventQueue::Batch batch;
        eventQueue_.Drain(batch);
        auto& abilities = batch.abilities;
        auto& actions = batch.actions;
        auto& heroAbilities = batch.heroAbilities;
        auto& modeSources = batch.modeSources;
        while (!heroAbilities.empty())
        {
            if (!CaptureLocalHeroAbility(heroAbilities.front()))
            {
                return false;
            }
            heroAbilities.pop_front();
        }
        while (!abilities.empty())
        {
            unmatchedAbilities_.push_back(abilities.front());
            abilities.pop_front();
        }
        while (!actions.empty())
        {
            if (player_action_semantics::IsExpression(actions.front().actionType))
            {
                if (!CaptureLocalExpression(actions.front()))
                {
                    return false;
                }
            }
            else if (player_action_semantics::IsWeaponTransition(
                         actions.front().actionType))
            {
                pendingWeaponTransitions_.push_back(actions.front());
            }
            else if (player_action_semantics::IsRangedAimStart(
                         actions.front().actionType) ||
                     player_action_semantics::IsRangedFire(
                         actions.front().actionType))
            {
                if (!CaptureLocalRangedAction(actions.front()))
                {
                    return false;
                }
            }
            else
            {
                unmatchedActions_.push_back(actions.front());
            }
            actions.pop_front();
        }
        // Process mode exits after accepted actions from the same drain. A
        // fire action therefore remains ordered before the native mode exit.
        while (!modeSources.empty())
        {
            if (!CaptureLocalRangedAimEnd(modeSources.front()))
            {
                return false;
            }
            modeSources.pop_front();
        }
        while (unmatchedAbilities_.size() > PendingEventCapacity)
        {
            unmatchedAbilities_.pop_front();
        }
        while (unmatchedActions_.size() > PendingEventCapacity)
        {
            unmatchedActions_.pop_front();
        }
        while (pendingWeaponTransitions_.size() > PendingEventCapacity)
        {
            pendingWeaponTransitions_.pop_front();
        }

        const std::uint64_t now = GetTickCount64();
        void* const localHero = localHero_ != nullptr
            ? localHero_->NativeHero() : nullptr;
        for (auto transition = pendingWeaponTransitions_.begin();
             transition != pendingWeaponTransitions_.end();)
        {
            if (transition->creature == nullptr ||
                transition->creature != localHero || !transition->accepted)
            {
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            const std::uint64_t lastMutationAt = localHero_ != nullptr
                ? localHero_->LastEquipmentMutationAt() : 0;
            const bool actionMutationObserved = transition->observedAt != 0 &&
                lastMutationAt >= transition->observedAt;
            if (!actionMutationObserved ||
                now < lastMutationAt + WeaponTransitionMutationSettleMilliseconds)
            {
                if (transition->observedAt != 0 &&
                    now > transition->observedAt +
                        WeaponTransitionCaptureWindowMilliseconds)
                {
                    diagnostics_.Event(
                        "MultiplayerLocalWeaponTransitionSuppressed",
                        "final-carry-state-not-observed");
                    transition = pendingWeaponTransitions_.erase(transition);
                    continue;
                }
                ++transition;
                continue;
            }
            game::hero_pawn::equipment::HeroEquipmentState equipment;
            const bool captured = game::hero_pawn::equipment::native::
                HeroWeaponComponent::Capture(localHero, equipment);
            const bool finalStateReady = captured && equipment.IsSane() &&
                (player_action_semantics::IsUnsheathe(transition->actionType)
                    ? equipment.activeFamily != game::creature::equipment::
                          CreatureWeaponFamily::None
                    : equipment.activeFamily == game::creature::equipment::
                          CreatureWeaponFamily::None);
            if (finalStateReady)
            {
                if (!CaptureLocalWeaponTransition(*transition, equipment))
                {
                    return false;
                }
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            if (transition->observedAt != 0 &&
                now > transition->observedAt +
                    WeaponTransitionCaptureWindowMilliseconds)
            {
                diagnostics_.Event(
                    "MultiplayerLocalWeaponTransitionSuppressed",
                    "final-carry-state-not-observed");
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            ++transition;
        }

        for (auto ability = unmatchedAbilities_.begin();
             ability != unmatchedAbilities_.end();)
        {
            if (ability->sourceCreature == nullptr ||
                ability->sourceCreature != localHero)
            {
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            if (!ability->attackCommand)
            {
                if (!CaptureLocal(*ability))
                {
                    return false;
                }
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            auto best = unmatchedActions_.end();
            std::uint64_t bestDistance = ActionPairWindowMilliseconds + 1;
            for (auto action = unmatchedActions_.begin();
                 action != unmatchedActions_.end(); ++action)
            {
                if (action->creature != ability->sourceCreature ||
                    action->threadId != ability->threadId)
                {
                    continue;
                }
                const std::uint64_t distance = action->observedAt >=
                    ability->observedAt
                    ? action->observedAt - ability->observedAt
                    : ability->observedAt - action->observedAt;
                if (distance <= ActionPairWindowMilliseconds &&
                    distance < bestDistance)
                {
                    best = action;
                    bestDistance = distance;
                }
            }
            if (best != unmatchedActions_.end())
            {
                if (best->accepted)
                {
                    if (!CaptureLocal(*ability, &*best))
                    {
                        return false;
                    }
                }
                else
                {
                    diagnostics_.Event(
                        "MultiplayerLocalPlayerActionSuppressed",
                        "retail-action-rejected");
                }
                unmatchedActions_.erase(best);
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            if (ability->observedAt != 0 &&
                now > ability->observedAt + ActionPairWindowMilliseconds)
            {
                diagnostics_.Event(
                    "MultiplayerLocalPlayerActionSuppressed",
                    "attack request had no accepted native Hero action");
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            ++ability;
        }
        for (auto action = unmatchedActions_.begin();
             action != unmatchedActions_.end();)
        {
            if (action->observedAt == 0 ||
                now <= action->observedAt + ActionPairWindowMilliseconds)
            {
                ++action;
                continue;
            }
            action = unmatchedActions_.erase(action);
        }
        return true;
    }

    void LocalPlayerActionCapture::CaptureAbility(
        void* context,
        const game::creature::combat::CreatureAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureAction(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureModeSource(
        void* context,
        const game::creature::locomotion::CreatureModeSourceEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureHeroAbility(
        void* context,
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::Shutdown() noexcept
    {
        eventQueue_.SetAccepting(false);
        if (actionObserver_ != nullptr)
        {
            actionObserver_->RemoveEventSink(
                &LocalPlayerActionCapture::CaptureAction, this);
        }
        if (modeObserver_ != nullptr)
        {
            modeObserver_->RemoveModeSourceEventSink(
                &LocalPlayerActionCapture::CaptureModeSource, this);
        }
        if (combat_ != nullptr)
        {
            combat_->RemoveAbilitySink(
                &LocalPlayerActionCapture::CaptureAbility, this);
        }
        if (abilities_ != nullptr)
        {
            abilities_->RemoveEventSink(
                &LocalPlayerActionCapture::CaptureHeroAbility, this);
        }
        eventQueue_.Clear();
        unmatchedAbilities_.clear();
        unmatchedActions_.clear();
        pendingWeaponTransitions_.clear();
        pendingMessages_.clear();
        transport_ = nullptr;
        localHero_ = nullptr;
        combatants_ = nullptr;
        identities_ = nullptr;
        combat_ = nullptr;
        abilities_ = nullptr;
        actionObserver_ = nullptr;
        modeObserver_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextActionId_ = 0;
        nextPresentationRevision_ = 0;
        initialized_ = false;
    }
}
