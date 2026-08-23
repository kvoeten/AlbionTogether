#include "PlayerActionReplication.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace
{
    bool IsWeaponTransitionAction(const char* actionType) noexcept
    {
        return actionType != nullptr &&
            (std::strstr(actionType, "UnsheatheItemFromInventory") !=
                    nullptr ||
                std::strstr(actionType, "SheatheItemToInventory") !=
                    nullptr);
    }

    bool IsUnsheatheAction(const char* actionType) noexcept
    {
        return actionType != nullptr &&
            std::strstr(actionType, "UnsheatheItemFromInventory") != nullptr;
    }
}

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolvePlayerActionSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.actions.playerActions;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkPlayerAction,
    0x1004u,
    "player-action",
    400u,
    "multiplayer-player-action-dispatch",
    ResolvePlayerActionSink);

namespace fable::multiplayer::replication
{
    void PlayerActionReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        LocalHeroReplication& localHero,
        RemotePlayerChannels& remoteChannels,
        presentation::RemotePlayerRegistry& remotePlayers,
        entities::EntityNetworkIdentityRegistry& identities,
        entities::EntityPresenceReplication& presence,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        localHero_ = &localHero;
        remoteChannels_ = &remoteChannels;
        remotePlayers_ = &remotePlayers;
        identities_ = &identities;
        presence_ = &presence;
        combatants_ = &combatants;
        combat_ = &combat;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        eventQueue_.SetAccepting(true);
        if (!combat_->AddAbilitySink(
                &PlayerActionReplication::CaptureAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-ability-observer");
            Shutdown();
            return;
        }
        if (!abilities_->AddEventSink(
                &PlayerActionReplication::CaptureHeroAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-hero-ability-observer");
            Shutdown();
            return;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerPlayerActionReady",
            "accepted Hero actions use native actor-scoped action submission");
    }

    bool PlayerActionReplication::AttachActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled() ||
            !observer.AddEventSink(
                &PlayerActionReplication::CaptureAction, this))
        {
            return false;
        }
        actionObserver_ = &observer;
        diagnostics_.Event(
            "MultiplayerPlayerActionLifecycleAttached",
            "input requests are causally paired with Fable's accepted native Hero action");
        return true;
    }

    bool PlayerActionReplication::ProcessPending()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            remoteChannels_ == nullptr || remotePlayers_ == nullptr ||
            combat_ == nullptr || abilities_ == nullptr)
        {
            return false;
        }
        if (!PublishPending())
        {
            return false;
        }

        if (!PairAcceptedLocalActions())
        {
            return false;
        }
        return PublishPending() && ReplayPending();
    }

    bool PlayerActionReplication::CaptureLocal(
        const game::creature::combat::CreatureAbilityEvent& event,
        const game::creature::actions::CreatureActionLifecycleEvent*
            resolvedAction)
    {
        if (event.abilityId == 0 || event.sourceCreature == nullptr ||
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
        if (event.attackCommand &&
            message.weaponFamily ==
                game::creature::equipment::CreatureWeaponFamily::None &&
            message.requiredWeapons.meleeDefinitionIndex > 0)
        {
            // ATTACK is observed before some Hero actions finish their native
            // unsheathe. Carry the intended family in the ordered action so
            // the remote AI proxy can prepare the same weapon first.
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
            ? "AttackAbility"
            : "CreatureAbility";
        if (resolvedAction != nullptr)
        {
            message.resolvedAnimationId = resolvedAction->animationId;
            message.resolvedActionType = resolvedAction->actionType;
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
            static_cast<unsigned long long>(
                message.targetPlayerActorId),
            static_cast<unsigned long long>(message.targetThingUid),
            message.semanticName.c_str(),
            message.resolvedActionType.empty()
                ? "<unresolved>"
                : message.resolvedActionType.c_str(),
            message.resolvedAnimationId,
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform"
                : "intent");
        diagnostics_.Event("MultiplayerLocalPlayerActionCaptured", detail);
        return Queue(std::move(message));
    }

    bool PlayerActionReplication::CaptureLocalWeaponTransition(
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

        protocol::PlayerActionMessage message;
        message.phase = role_ == PeerRole::Host
            ? protocol::PlayerActionPhase::Perform
            : protocol::PlayerActionPhase::Intent;
        message.kind = protocol::PlayerActionKind::WeaponTransition;
        message.ownerActorId = localActorId_;
        message.actionId = NextActionId();
        message.authorityEpoch = state->authorityEpoch;
        message.actorGeneration = state->actorGeneration;
        message.mapEpoch = state->mapEpoch;
        message.weaponFamily = equipment.activeFamily;
        message.requiredWeapons = equipment.WeaponDefinitions();
        message.requiredMeleeAttachmentSlot =
            equipment.meleeAttachmentSlot;
        message.requiredRangedAttachmentSlot =
            equipment.rangedAttachmentSlot;
        message.resolvedAnimationId = action.animationId;
        message.mapName = state->mapName;
        message.semanticName = "WeaponTransition";
        message.resolvedActionType = action.actionType;

        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu family=%u melee=%d melee_slot=%u ranged=%d ranged_slot=%u native_action=%s animation_id=%u map=%s phase=%s",
            static_cast<unsigned long long>(message.ownerActorId),
            static_cast<unsigned long long>(message.actionId),
            static_cast<unsigned int>(message.weaponFamily),
            message.requiredWeapons.meleeDefinitionIndex,
            message.requiredMeleeAttachmentSlot,
            message.requiredWeapons.rangedDefinitionIndex,
            message.requiredRangedAttachmentSlot,
            message.resolvedActionType.c_str(),
            message.resolvedAnimationId,
            message.mapName.c_str(),
            message.phase == protocol::PlayerActionPhase::Perform
                ? "perform"
                : "intent");
        diagnostics_.Event(
            "MultiplayerLocalWeaponTransitionCaptured", detail);
        return Queue(std::move(message));
    }

    bool PlayerActionReplication::CaptureLocalHeroAbility(
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (event.sourceCreature == nullptr ||
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
            ? combatants_->FindActor(event.targetCreature)
            : 0;
        message.targetThingUid = message.targetPlayerActorId == 0
            ? (identities_ != nullptr
                ? identities_->CanonicalizeLocalObservation(
                    event.targetThingUid)
                : event.targetThingUid)
            : 0;
        message.mapName = state->mapName;
        message.semanticName = "HeroAbility";

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
                ? "perform"
                : "intent");
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

    bool PlayerActionReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ ||
            transportMessage.type != protocol::PacketType::PlayerAction)
        {
            return false;
        }
        protocol::PlayerActionMessage message;
        if (!protocol::DecodePlayerActionMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionRejected",
                "invalid player action payload");
            return true;
        }
        if (message.kind == protocol::PlayerActionKind::HeroAbility)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source_actor_id=%llu owner_actor_id=%llu action_id=%llu ability_id=%u command=%u phase=%u",
                static_cast<unsigned long long>(
                    transportMessage.sourceActorId),
                static_cast<unsigned long long>(message.ownerActorId),
                static_cast<unsigned long long>(message.actionId),
                message.abilityId,
                static_cast<unsigned int>(message.heroAbilityCommand),
                static_cast<unsigned int>(message.phase));
            diagnostics_.Event(
                "MultiplayerRemoteHeroAbilityReceived", detail);
        }
        if (role_ == PeerRole::Host)
        {
            if (message.phase != protocol::PlayerActionPhase::Intent ||
                message.ownerActorId != transportMessage.sourceActorId)
            {
                diagnostics_.Event(
                    "MultiplayerPlayerActionRejected",
                    "guest attempted to author an invalid player action");
                return true;
            }
            return AcceptIntent(
                std::move(message),
                transportMessage.sourceActorId,
                transportMessage.connectionNonce);
        }
        if (message.phase != protocol::PlayerActionPhase::Perform)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionRejected",
                "guest received a non-authoritative player action");
            return true;
        }
        return AcceptAuthoritative(
            std::move(message), transportMessage.connectionNonce);
    }

    bool PlayerActionReplication::AcceptIntent(
        protocol::PlayerActionMessage message,
        std::uint64_t sourceActorId,
        const std::uint64_t sourceConnectionNonce)
    {
        const PlayerState* const owner = remoteChannels_->Find(sourceActorId);
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_->FindLifecycle(sourceActorId);
        if (owner == nullptr || owner->actorId != message.ownerActorId ||
            owner->authorityEpoch != message.authorityEpoch ||
            lifecycle == nullptr || !lifecycle->active ||
            (lifecycle->connectionNonce != 0 &&
                lifecycle->connectionNonce != sourceConnectionNonce) ||
            lifecycle->actorGeneration != message.actorGeneration ||
            lifecycle->mapEpoch != message.mapEpoch ||
            owner->mapName != message.mapName)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionStale",
                "player action intent did not match its current actor channel");
            return true;
        }
        message.phase = protocol::PlayerActionPhase::Perform;
        if (!Queue(message, sourceConnectionNonce) || !QueueReplay(
                std::move(message), sourceConnectionNonce))
        {
            return false;
        }
        return PublishPending();
    }

    bool PlayerActionReplication::AcceptAuthoritative(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        if (message.ownerActorId == localActorId_)
        {
            return true;
        }
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_->FindLifecycle(message.ownerActorId);
        if (lifecycle != nullptr &&
            (!lifecycle->active ||
                (lifecycle->connectionNonce != 0 &&
                    lifecycle->connectionNonce != sourceConnectionNonce) ||
                lifecycle->actorGeneration !=
                message.actorGeneration || lifecycle->mapEpoch !=
                message.mapEpoch))
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionStale",
                "authoritative player action did not match its lifecycle channel");
            return true;
        }
        return QueueReplay(std::move(message), sourceConnectionNonce);
    }

    bool PlayerActionReplication::Queue(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        std::size_t actorMessages = 0;
        for (const auto& pending : pendingMessages_)
        {
            if (pending.message.ownerActorId == message.ownerActorId)
            {
                ++actorMessages;
            }
        }
        if (actorMessages >= PendingMessageCapacity / 4)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player action publication queue is full for actor lifecycle");
            return false;
        }
        if (pendingMessages_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player action publication queue is full");
            return false;
        }
        pendingMessages_.push_back({
            std::move(message), sourceConnectionNonce});
        return true;
    }

    bool PlayerActionReplication::QueueReplay(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        std::size_t actorReplays = 0;
        for (const auto& pending : pendingReplays_)
        {
            if (pending.message.ownerActorId == message.ownerActorId)
            {
                ++actorReplays;
            }
        }
        if (actorReplays >= PendingReplayCapacity / 4)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player native-action queue is full for actor lifecycle");
            return false;
        }
        if (pendingReplays_.size() >= PendingReplayCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player native-action submission queue is full");
            return false;
        }
        pendingReplays_.push_back({
            std::move(message),
            GetTickCount64(),
            0,
            0,
            sourceConnectionNonce});
        return true;
    }

    bool PlayerActionReplication::PublishPending()
    {
        const std::size_t scheduled = pendingMessages_.size();
        std::unordered_set<std::uint64_t> attemptedActors;
        bool deferred = false;
        for (std::size_t attempt = 0;
             attempt < scheduled && !pendingMessages_.empty(); ++attempt)
        {
            const protocol::PlayerActionMessage& queued =
                pendingMessages_.front().message;
            const PlayerState* owner = queued.ownerActorId == localActorId_
                ? localHero_->CurrentState()
                : remoteChannels_->Find(queued.ownerActorId);
            const RemotePlayerLifecycle* lifecycle =
                queued.ownerActorId == localActorId_
                ? nullptr
                : remoteChannels_->FindLifecycle(queued.ownerActorId);
            const bool lifecycleMatches = queued.ownerActorId == localActorId_
                ? owner != nullptr && owner->actorGeneration ==
                        queued.actorGeneration && owner->mapEpoch ==
                        queued.mapEpoch
                : lifecycle != nullptr && lifecycle->active &&
                    lifecycle->actorGeneration == queued.actorGeneration &&
                    lifecycle->mapEpoch == queued.mapEpoch;
            if (owner == nullptr || owner->authorityEpoch !=
                    queued.authorityEpoch || !lifecycleMatches ||
                owner->mapName != queued.mapName)
            {
                pendingMessages_.pop_front();
                continue;
            }
            if (!attemptedActors.insert(queued.ownerActorId).second)
            {
                pendingMessages_.push_back(
                    std::move(pendingMessages_.front()));
                pendingMessages_.pop_front();
                continue;
            }
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodePlayerActionMessage(
                    pendingMessages_.front().message,
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    reliable_stream::Actor(queued.ownerActorId),
                    protocol::PacketType::PlayerAction,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                deferred = true;
                pendingMessages_.push_back(
                    std::move(pendingMessages_.front()));
                pendingMessages_.pop_front();
                continue;
            }
            if (pendingMessages_.front().message.kind ==
                protocol::PlayerActionKind::HeroAbility)
            {
                char detail[160] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u command=%u",
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.ownerActorId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().message.actionId),
                    pendingMessages_.front().message.abilityId,
                    static_cast<unsigned int>(
                        pendingMessages_.front().message.heroAbilityCommand));
                diagnostics_.Event(
                    "MultiplayerLocalHeroAbilityPublished", detail);
            }
            pendingMessages_.pop_front();
        }
        if (deferred && !publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionPublishDeferred",
                "one or more actor action streams are waiting for transport capacity");
            publishBackpressured_ = true;
        }
        else if (!deferred && publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionPublishResumed",
                "queued player actions entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    bool PlayerActionReplication::PairAcceptedLocalActions()
    {
        PlayerActionEventQueue::Batch batch;
        eventQueue_.Drain(batch);
        auto& abilities = batch.abilities;
        auto& actions = batch.actions;
        auto& heroAbilities = batch.heroAbilities;
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
            if (IsWeaponTransitionAction(actions.front().actionType))
            {
                pendingWeaponTransitions_.push_back(actions.front());
            }
            else
            {
                unmatchedActions_.push_back(actions.front());
            }
            actions.pop_front();
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
            ? localHero_->NativeHero()
            : nullptr;
        for (auto transition = pendingWeaponTransitions_.begin();
             transition != pendingWeaponTransitions_.end();)
        {
            if (transition->creature == nullptr ||
                transition->creature != localHero || !transition->accepted)
            {
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            if (transition->observedAt != 0 &&
                now < transition->observedAt +
                    WeaponTransitionMutationSettleMilliseconds)
            {
                ++transition;
                continue;
            }
            game::hero_pawn::equipment::HeroEquipmentState equipment;
            const bool captured = game::hero_pawn::equipment::native::
                HeroWeaponComponent::Capture(localHero, equipment);
            const bool finalStateReady = captured && equipment.IsSane() &&
                (IsUnsheatheAction(transition->actionType)
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
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "native_action=%s animation_id=%u reason=final-carry-state-not-observed",
                    transition->actionType,
                    transition->animationId);
                diagnostics_.Event(
                    "MultiplayerLocalWeaponTransitionSuppressed", detail);
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
            std::uint64_t bestDistance =
                ActionPairWindowMilliseconds + 1;
            for (auto action = unmatchedActions_.begin();
                 action != unmatchedActions_.end(); ++action)
            {
                if (action->creature != ability->sourceCreature ||
                    action->threadId != ability->threadId)
                {
                    continue;
                }
                const std::uint64_t distance =
                    action->observedAt >= ability->observedAt
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
                    char detail[320] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "ability_id=%u native_action=%s reason=retail-action-rejected",
                        ability->abilityId,
                        best->actionType[0] != '\0'
                            ? best->actionType
                            : "<unknown>");
                    diagnostics_.Event(
                        "MultiplayerLocalPlayerActionSuppressed", detail);
                }
                unmatchedActions_.erase(best);
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }

            if (ability->observedAt != 0 &&
                now > ability->observedAt +
                    ActionPairWindowMilliseconds)
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
                now <= action->observedAt +
                    ActionPairWindowMilliseconds)
            {
                ++action;
                continue;
            }
            action = unmatchedActions_.erase(action);
        }
        return true;
    }

    std::uint64_t PlayerActionReplication::NextActionId() noexcept
    {
        ++nextActionId_;
        if (nextActionId_ == 0)
        {
            ++nextActionId_;
        }
        return nextActionId_;
    }

    void PlayerActionReplication::CaptureAbility(
        void* context,
        const game::creature::combat::CreatureAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<PlayerActionReplication*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void PlayerActionReplication::CaptureAction(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<PlayerActionReplication*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void PlayerActionReplication::CaptureHeroAbility(
        void* context,
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<PlayerActionReplication*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void PlayerActionReplication::InvalidateActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0)
        {
            return;
        }
        const PlayerState* const current = remoteChannels_ != nullptr
            ? remoteChannels_->Find(actorId)
            : nullptr;
        const RemotePlayerLifecycle* const lifecycle =
            remoteChannels_ != nullptr
                ? remoteChannels_->FindLifecycle(actorId)
                : nullptr;
        const auto isCurrentOwner = [&](
            const protocol::PlayerActionMessage& message,
            const std::uint64_t sourceConnectionNonce) noexcept
        {
            return current != nullptr && lifecycle != nullptr &&
                lifecycle->active &&
                current->authorityEpoch == message.authorityEpoch &&
                lifecycle->actorGeneration == message.actorGeneration &&
                lifecycle->mapEpoch == message.mapEpoch &&
                (lifecycle->connectionNonce == 0 ||
                    (sourceConnectionNonce != 0 &&
                        lifecycle->connectionNonce ==
                            sourceConnectionNonce));
        };
        for (auto pending = pendingMessages_.begin();
             pending != pendingMessages_.end();)
        {
            if ((pending->message.ownerActorId == actorId &&
                    !isCurrentOwner(
                        pending->message,
                        pending->sourceConnectionNonce)) ||
                pending->message.targetPlayerActorId == actorId)
            {
                pending = pendingMessages_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
        for (auto replay = pendingReplays_.begin();
             replay != pendingReplays_.end();)
        {
            if ((replay->message.ownerActorId == actorId &&
                    !isCurrentOwner(
                        replay->message,
                        replay->sourceConnectionNonce)) ||
                replay->message.targetPlayerActorId == actorId)
            {
                replay = pendingReplays_.erase(replay);
            }
            else
            {
                ++replay;
            }
        }
    }

    void PlayerActionReplication::InvalidateAllRemote() noexcept
    {
        for (auto pending = pendingMessages_.begin();
             pending != pendingMessages_.end();)
        {
            if (pending->message.ownerActorId != localActorId_ ||
                pending->message.targetPlayerActorId != 0)
            {
                pending = pendingMessages_.erase(pending);
            }
            else
            {
                ++pending;
            }
        }
        for (auto replay = pendingReplays_.begin();
             replay != pendingReplays_.end();)
        {
            if (replay->message.ownerActorId != localActorId_ ||
                replay->message.targetPlayerActorId != 0)
            {
                replay = pendingReplays_.erase(replay);
            }
            else
            {
                ++replay;
            }
        }
    }

    void PlayerActionReplication::Shutdown() noexcept
    {
        eventQueue_.SetAccepting(false);
        if (actionObserver_ != nullptr)
        {
            actionObserver_->RemoveEventSink(
                &PlayerActionReplication::CaptureAction, this);
        }
        if (combat_ != nullptr)
        {
            combat_->RemoveAbilitySink(
                &PlayerActionReplication::CaptureAbility, this);
        }
        if (abilities_ != nullptr)
        {
            abilities_->RemoveEventSink(
                &PlayerActionReplication::CaptureHeroAbility, this);
        }
        eventQueue_.Clear();
        unmatchedAbilities_.clear();
        unmatchedActions_.clear();
        pendingWeaponTransitions_.clear();
        pendingMessages_.clear();
        pendingReplays_.clear();
        transport_ = nullptr;
        localHero_ = nullptr;
        remoteChannels_ = nullptr;
        remotePlayers_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        combatants_ = nullptr;
        combat_ = nullptr;
        abilities_ = nullptr;
        actionObserver_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextActionId_ = 0;
        publishBackpressured_ = false;
        initialized_ = false;
    }
}
