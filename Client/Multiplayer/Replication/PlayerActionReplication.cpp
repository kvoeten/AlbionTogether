#include "PlayerActionReplication.h"

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
        acceptingEvents_.store(true, std::memory_order_release);
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
            state->authorityEpoch == 0 || state->mapName.empty())
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
            state->authorityEpoch == 0 || state->mapName.empty())
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
                std::move(message), transportMessage.sourceActorId);
        }
        if (message.phase != protocol::PlayerActionPhase::Perform)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionRejected",
                "guest received a non-authoritative player action");
            return true;
        }
        return AcceptAuthoritative(std::move(message));
    }

    bool PlayerActionReplication::AcceptIntent(
        protocol::PlayerActionMessage message,
        std::uint64_t sourceActorId)
    {
        const PlayerState* const owner = remoteChannels_->Find(sourceActorId);
        if (owner == nullptr || owner->actorId != message.ownerActorId ||
            owner->authorityEpoch != message.authorityEpoch ||
            owner->mapName != message.mapName)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionStale",
                "player action intent did not match its current actor channel");
            return true;
        }
        message.phase = protocol::PlayerActionPhase::Perform;
        if (!Queue(message) || !QueueReplay(std::move(message)))
        {
            return false;
        }
        return PublishPending();
    }

    bool PlayerActionReplication::AcceptAuthoritative(
        protocol::PlayerActionMessage message)
    {
        if (message.ownerActorId == localActorId_)
        {
            return true;
        }
        return QueueReplay(std::move(message));
    }

    bool PlayerActionReplication::Queue(
        protocol::PlayerActionMessage message)
    {
        if (pendingMessages_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player action publication queue is full");
            return false;
        }
        pendingMessages_.push_back(std::move(message));
        return true;
    }

    bool PlayerActionReplication::QueueReplay(
        protocol::PlayerActionMessage message)
    {
        if (pendingReplays_.size() >= PendingReplayCapacity)
        {
            diagnostics_.Event(
                "MultiplayerPlayerActionOverflow",
                "bounded player native-action submission queue is full");
            return false;
        }
        pendingReplays_.push_back({
            std::move(message), GetTickCount64(), 0});
        return true;
    }

    bool PlayerActionReplication::PublishPending()
    {
        while (!pendingMessages_.empty())
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodePlayerActionMessage(
                    pendingMessages_.front(),
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    protocol::PacketType::PlayerAction,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                if (!publishBackpressured_)
                {
                    diagnostics_.Event(
                        "MultiplayerPlayerActionPublishDeferred",
                        "ordered traffic is draining before queued player actions");
                    publishBackpressured_ = true;
                }
                return true;
            }
            if (pendingMessages_.front().kind ==
                protocol::PlayerActionKind::HeroAbility)
            {
                char detail[160] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u command=%u",
                    static_cast<unsigned long long>(
                        pendingMessages_.front().ownerActorId),
                    static_cast<unsigned long long>(
                        pendingMessages_.front().actionId),
                    pendingMessages_.front().abilityId,
                    static_cast<unsigned int>(
                        pendingMessages_.front().heroAbilityCommand));
                diagnostics_.Event(
                    "MultiplayerLocalHeroAbilityPublished", detail);
            }
            pendingMessages_.pop_front();
        }
        if (publishBackpressured_)
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
        std::deque<game::creature::combat::CreatureAbilityEvent> abilities;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            actions;
        std::deque<game::hero_pawn::abilities::HeroAbilityEvent>
            heroAbilities;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            abilities.swap(inboundAbilities_);
            actions.swap(inboundActions_);
            heroAbilities.swap(inboundHeroAbilities_);
        }
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
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
        }
        while (unmatchedActions_.size() > PendingEventCapacity)
        {
            unmatchedActions_.pop_front();
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
        }
        while (pendingWeaponTransitions_.size() > PendingEventCapacity)
        {
            pendingWeaponTransitions_.pop_front();
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
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

    bool PlayerActionReplication::ReplayPending()
    {
        const std::uint64_t now = GetTickCount64();
        std::unordered_set<std::uint64_t> blockedActors;
        for (auto iterator = pendingReplays_.begin();
             iterator != pendingReplays_.end();)
        {
            PendingReplay& replay = *iterator;
            const protocol::PlayerActionMessage& message = iterator->message;
            if (blockedActors.find(message.ownerActorId) !=
                blockedActors.end())
            {
                ++iterator;
                continue;
            }
            const PlayerState* const owner = remoteChannels_->Find(
                message.ownerActorId);
            if (owner != nullptr &&
                (owner->authorityEpoch != message.authorityEpoch ||
                    owner->mapName != message.mapName))
            {
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            const bool sameMap = localHero_->IsWorldReady() &&
                localHero_->MapName() == message.mapName;
            void* targetCreature = nullptr;
            bool targetReady = message.targetPlayerActorId == 0 &&
                message.targetThingUid == 0;
            if (message.targetPlayerActorId != 0 && combatants_ != nullptr)
            {
                targetCreature = combatants_->FindCreature(
                    message.targetPlayerActorId);
                targetReady = targetCreature != nullptr;
            }
            else if (!targetReady && presence_ != nullptr)
            {
                const entities::LiveEntityRecord* const target =
                    presence_->LiveEntities().Find(message.targetThingUid);
                if (target != nullptr && target->thing != nullptr &&
                    target->creature)
                {
                    targetCreature = target->thing;
                    targetReady = true;
                }
            }
            const std::uint64_t age = replay.queuedAt != 0 &&
                    now >= replay.queuedAt
                ? now - replay.queuedAt
                : 0;
            const bool targetExpired = !targetReady &&
                age >= TargetResolutionGraceMilliseconds;
            if (!sameMap || targetExpired)
            {
                char detail[384] = {};
                const char* const reason = !sameMap
                    ? "map-no-longer-active"
                    : "target-no-longer-available";
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u target_player=%llu target_uid=%016llX age_ms=%llu reason=%s",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    message.abilityId,
                    static_cast<unsigned long long>(
                        message.targetPlayerActorId),
                    static_cast<unsigned long long>(message.targetThingUid),
                    static_cast<unsigned long long>(age),
                    reason);
                diagnostics_.Event(
                    "MultiplayerRemotePlayerActionRetired", detail);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (replay.nextAttemptAt != 0 && now < replay.nextAttemptAt)
            {
                blockedActors.insert(message.ownerActorId);
                ++iterator;
                continue;
            }
            const bool heroAbilityReady = owner != nullptr && sameMap &&
                targetReady &&
                message.kind == protocol::PlayerActionKind::HeroAbility;
            bool heroAbilityAccepted = false;
            if (heroAbilityReady)
            {
                heroAbilityAccepted = remotePlayers_->PerformHeroAbility(
                    message.ownerActorId,
                    static_cast<game::hero_pawn::abilities::HeroAbility>(
                        message.abilityId),
                    message.heroAbilityCommand,
                    message.heroAbilityProgressionState,
                    targetCreature);
            }
            if (message.kind == protocol::PlayerActionKind::HeroAbility &&
                !replay.diagnosticEmitted)
            {
                replay.diagnosticEmitted = true;
                char attempt[384] = {};
                std::snprintf(
                    attempt,
                    sizeof(attempt),
                    "actor_id=%llu action_id=%llu owner_found=%s same_map=%s target_ready=%s target=%p attempted=%s accepted=%s age_ms=%llu",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    owner != nullptr ? "true" : "false",
                    sameMap ? "true" : "false",
                    targetReady ? "true" : "false",
                    targetCreature,
                    heroAbilityReady ? "true" : "false",
                    heroAbilityAccepted ? "true" : "false",
                    static_cast<unsigned long long>(age));
                diagnostics_.Event(
                    "MultiplayerRemoteHeroAbilityReplayAttempt", attempt);
            }
            if (heroAbilityAccepted)
            {
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u name=%s command=%u target=%p",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    message.abilityId,
                    game::hero_pawn::abilities::Name(
                        static_cast<game::hero_pawn::abilities::HeroAbility>(
                            message.abilityId)),
                    static_cast<unsigned int>(message.heroAbilityCommand),
                    targetCreature);
                diagnostics_.Event(
                    "MultiplayerRemoteHeroAbilityReplayed", detail);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (heroAbilityReady &&
                age >= NativeReplayFailureGraceMilliseconds)
            {
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u command=%u progression_state=%d age_ms=%llu reason=native-replay-rejected",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    message.abilityId,
                    static_cast<unsigned int>(message.heroAbilityCommand),
                    message.heroAbilityProgressionState,
                    static_cast<unsigned long long>(age));
                diagnostics_.Event(
                    "MultiplayerRemotePlayerActionRetired", detail);
                blockedActors.insert(message.ownerActorId);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (owner != nullptr && sameMap && targetReady &&
                message.kind == protocol::PlayerActionKind::AbilityRequest &&
                remotePlayers_->PerformAbility(
                    message.ownerActorId,
                    message.weaponFamily,
                    message.requiredWeapons,
                    message.requiredMeleeAttachmentSlot,
                    message.requiredRangedAttachmentSlot,
                    message.abilityId,
                    message.charge,
                    targetCreature,
                    message.resolvedActionType,
                    message.resolvedAnimationId))
            {
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu ability_id=%u charge=%.3f semantic=%s native_action=%s source_animation_id=%u",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    message.abilityId,
                    message.charge,
                    message.semanticName.c_str(),
                    message.resolvedActionType.empty()
                        ? "<unresolved>"
                        : message.resolvedActionType.c_str(),
                    message.resolvedAnimationId);
                diagnostics_.Event(
                    "MultiplayerRemotePlayerAbilitySubmitted", detail);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (owner != nullptr && sameMap && targetReady &&
                message.kind ==
                    protocol::PlayerActionKind::WeaponTransition &&
                remotePlayers_->PerformWeaponTransition(
                    message.ownerActorId,
                    message.weaponFamily,
                    message.requiredWeapons,
                    message.requiredMeleeAttachmentSlot,
                    message.requiredRangedAttachmentSlot,
                    message.resolvedActionType,
                    message.resolvedAnimationId))
            {
                char detail[384] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu family=%u melee=%d melee_slot=%u ranged=%d ranged_slot=%u native_action=%s animation_id=%u",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    static_cast<unsigned int>(message.weaponFamily),
                    message.requiredWeapons.meleeDefinitionIndex,
                    message.requiredMeleeAttachmentSlot,
                    message.requiredWeapons.rangedDefinitionIndex,
                    message.requiredRangedAttachmentSlot,
                    message.resolvedActionType.c_str(),
                    message.resolvedAnimationId);
                diagnostics_.Event(
                    "MultiplayerRemoteWeaponTransitionSubmitted", detail);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            replay.nextAttemptAt = now + ReplayRetryMilliseconds;
            blockedActors.insert(message.ownerActorId);
            ++iterator;
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
            static_cast<PlayerActionReplication*>(context)->EnqueueAbility(
                event);
        }
    }

    void PlayerActionReplication::CaptureAction(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<PlayerActionReplication*>(context)->EnqueueAction(
                event);
        }
    }

    void PlayerActionReplication::CaptureHeroAbility(
        void* context,
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<PlayerActionReplication*>(context)->
                EnqueueHeroAbility(event);
        }
    }

    void PlayerActionReplication::EnqueueAbility(
        const game::creature::combat::CreatureAbilityEvent& event)
        noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (inboundAbilities_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        inboundAbilities_.push_back(event);
    }

    void PlayerActionReplication::EnqueueAction(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire) ||
            event.phase != game::creature::actions::
                CreatureActionLifecyclePhase::Submitted ||
            (std::strstr(event.actionType, "InterruptableMidAttack") ==
                    nullptr &&
                std::strstr(event.actionType, "InterruptableNearAttack") ==
                    nullptr &&
                !IsWeaponTransitionAction(event.actionType)))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (inboundActions_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        inboundActions_.push_back(event);
    }

    void PlayerActionReplication::EnqueueHeroAbility(
        const game::hero_pawn::abilities::HeroAbilityEvent& event) noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (inboundHeroAbilities_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        inboundHeroAbilities_.push_back(event);
    }

    void PlayerActionReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
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
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            inboundAbilities_.clear();
            inboundActions_.clear();
            inboundHeroAbilities_.clear();
        }
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
        droppedEvents_.store(0, std::memory_order_release);
        publishBackpressured_ = false;
        initialized_ = false;
    }
}
