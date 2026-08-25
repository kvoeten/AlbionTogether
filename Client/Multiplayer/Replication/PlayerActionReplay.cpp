#include "PlayerActionReplication.h"

#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <Windows.h>

#include <cstdio>
#include <unordered_set>

namespace fable::multiplayer::replication
{
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
            const RemotePlayerLifecycle* const lifecycle =
                remoteChannels_->FindLifecycle(message.ownerActorId);
            if (lifecycle != nullptr &&
                replay.sourceConnectionNonce != 0 &&
                lifecycle->connectionNonce != 0 &&
                replay.sourceConnectionNonce != lifecycle->connectionNonce)
            {
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (owner != nullptr &&
                (owner->authorityEpoch != message.authorityEpoch ||
                    owner->mapName != message.mapName))
            {
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (lifecycle != nullptr &&
                !remoteChannels_->IsLifecycleActive(
                    message.ownerActorId,
                    message.actorGeneration,
                    message.mapEpoch))
            {
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (owner == nullptr && lifecycle == nullptr)
            {
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            const bool sameMap = localHero_->IsWorldReady() &&
                localHero_->MapName() == message.mapName;
            const bool nativeOwnerReady = sameMap &&
                remotePlayers_->IsLifecycleActive(
                    message.ownerActorId,
                    message.actorGeneration,
                    message.mapEpoch);
            if (sameMap && !nativeOwnerReady)
            {
                // Construct and action share an ordered stream, but native
                // Hero materialization may still take several frames. Hold
                // the action outside the native stack until the presentation
                // has applied its complete baseline and reached Active.
                replay.nativeReadyAt = 0;
                replay.nextAttemptAt = now + ReplayRetryMilliseconds;
                blockedActors.insert(message.ownerActorId);
                ++iterator;
                continue;
            }
            if (nativeOwnerReady && replay.nativeReadyAt == 0)
            {
                replay.nativeReadyAt = now;
            }
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
            const std::uint64_t nativeAttemptAge =
                replay.nativeReadyAt != 0 && now >= replay.nativeReadyAt
                ? now - replay.nativeReadyAt
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
            if (owner != nullptr && sameMap && targetReady &&
                message.kind == protocol::PlayerActionKind::RangedAimEnd &&
                remotePlayers_->EndRangedAim(message.ownerActorId))
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu action_id=%llu semantic=%s",
                    static_cast<unsigned long long>(message.ownerActorId),
                    static_cast<unsigned long long>(message.actionId),
                    message.semanticName.c_str());
                diagnostics_.Event(
                    "MultiplayerRemoteRangedAimEndReplayed", detail);
                iterator = pendingReplays_.erase(iterator);
                continue;
            }
            if (heroAbilityReady &&
                nativeAttemptAge >= NativeReplayFailureGraceMilliseconds)
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
                (message.kind == protocol::PlayerActionKind::AbilityRequest ||
                    message.kind == protocol::PlayerActionKind::RangedAim) &&
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
}
