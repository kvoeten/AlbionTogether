#include "EntityActionReplay.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"

#include <cstdio>

namespace fable::multiplayer::replication::entity_actions
{
    void EntityActionReplay::Initialize(
        const std::uint64_t localActorId,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityPresenceReplication& presence,
        combat::PlayerCombatantDirectory& combatants,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics) noexcept
    {
        localActorId_ = localActorId;
        lifecycle_ = &lifecycle;
        presence_ = &presence;
        combatants_ = &combatants;
        combat_ = &combat;
        diagnostics_ = diagnostics;
    }

    bool EntityActionReplay::ReplayAbility(ActiveAction& action)
    {
        if (!action.nativeAbility || action.abilityId == 0 ||
            action.ownerActorId == localActorId_ || action.abilityReplayed ||
            action.abilityReplayFailed)
        {
            return true;
        }
        if (presence_ == nullptr || lifecycle_ == nullptr || combat_ == nullptr)
        {
            return false;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(action.entityUid);
        const entities::LiveEntityRecord* const live =
            presence_->LiveEntities().Find(action.entityUid);
        if (world == nullptr || live == nullptr || live->thing == nullptr ||
            !live->creature || !world->live || !world->available ||
            !world->creature || world->generation != action.entityGeneration ||
            world->mapEpoch != action.mapEpoch ||
            world->mapName != action.mapName)
        {
            return true;
        }
        if (!combat_->SubmitReplicatedAbility(
                live->thing, action.abilityId, action.abilityCharge))
        {
            action.abilityReplayFailed = true;
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u action_id=%llu ability_id=%u charge=%.3f",
                static_cast<unsigned long long>(action.entityUid),
                action.entityGeneration,
                static_cast<unsigned long long>(action.actionId),
                action.abilityId,
                action.abilityCharge);
            diagnostics_.Event("MultiplayerEntityAbilityRejected", detail);
            return true;
        }
        action.abilityReplayed = true;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u action_id=%llu owner=%llu ability_id=%u charge=%.3f",
            static_cast<unsigned long long>(action.entityUid),
            action.entityGeneration,
            static_cast<unsigned long long>(action.actionId),
            static_cast<unsigned long long>(action.ownerActorId),
            action.abilityId,
            action.abilityCharge);
        diagnostics_.Event("MultiplayerEntityAbilitySubmitted", detail);
        return true;
    }

    bool EntityActionReplay::ReplayNativeAction(ActiveAction& action)
    {
        // Abilities reconstruct their own native action graphs. This path is
        // for the direct AI melee action submitted after an NPC has made its
        // combat decision.
        if (action.nativeAbility || action.ownerActorId == localActorId_ ||
            action.nativeReplayed || action.nativeReplayFailed ||
            action.semanticName != "CCreatureAction_InterruptableMidAttack")
        {
            return true;
        }
        if (presence_ == nullptr || lifecycle_ == nullptr || combat_ == nullptr ||
            combatants_ == nullptr)
        {
            return false;
        }
        const std::uint64_t now = GetTickCount64();
        if (now < action.nextNativeReplayAt)
        {
            return true;
        }

        const entities::WorldEntityRecord* const sourceWorld =
            lifecycle_->Directory().Find(action.entityUid);
        const entities::LiveEntityRecord* const sourceLive =
            presence_->LiveEntities().Find(action.entityUid);
        if (sourceWorld == nullptr || sourceLive == nullptr ||
            sourceLive->thing == nullptr || !sourceLive->creature ||
            !sourceWorld->live || !sourceWorld->available ||
            !sourceWorld->creature ||
            sourceWorld->generation != action.entityGeneration ||
            sourceWorld->mapEpoch != action.mapEpoch ||
            sourceWorld->mapName != action.mapName)
        {
            return true;
        }

        void* targetCreature = nullptr;
        if (action.targetPlayerActorId != 0)
        {
            targetCreature = combatants_->FindCreature(
                action.targetPlayerActorId);
        }
        else if (action.targetEntityUid != 0)
        {
            const entities::WorldEntityRecord* const targetWorld =
                lifecycle_->Directory().Find(action.targetEntityUid);
            const entities::LiveEntityRecord* const targetLive =
                presence_->LiveEntities().Find(action.targetEntityUid);
            if (targetWorld != nullptr && targetLive != nullptr &&
                targetLive->thing != nullptr && targetLive->creature &&
                targetWorld->live && targetWorld->available &&
                targetWorld->creature &&
                targetWorld->generation == action.targetEntityGeneration &&
                targetWorld->mapName == action.mapName)
            {
                targetCreature = targetLive->thing;
            }
        }
        if (targetCreature == nullptr)
        {
            return true;
        }

        ++action.nativeReplayAttempts;
        action.nextNativeReplayAt = now + 100;
        if (!combat_->SubmitReplicatedImmediateAttack(
                sourceLive->thing, targetCreature))
        {
            if (action.nativeReplayAttempts >= 30)
            {
                action.nativeReplayFailed = true;
                char detail[384] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "thing_uid=%016llX generation=%u action_id=%llu target_entity=%016llX target_player=%llu attempts=%u",
                    static_cast<unsigned long long>(action.entityUid),
                    action.entityGeneration,
                    static_cast<unsigned long long>(action.actionId),
                    static_cast<unsigned long long>(action.targetEntityUid),
                    static_cast<unsigned long long>(
                        action.targetPlayerActorId),
                    action.nativeReplayAttempts);
                diagnostics_.Event(
                    "MultiplayerEntityNativeActionRejected", detail);
            }
            return true;
        }

        action.nativeReplayed = true;
        char detail[448] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u action_id=%llu owner=%llu semantic=%s target_entity=%016llX target_player=%llu attempts=%u",
            static_cast<unsigned long long>(action.entityUid),
            action.entityGeneration,
            static_cast<unsigned long long>(action.actionId),
            static_cast<unsigned long long>(action.ownerActorId),
            action.semanticName.c_str(),
            static_cast<unsigned long long>(action.targetEntityUid),
            static_cast<unsigned long long>(action.targetPlayerActorId),
            action.nativeReplayAttempts);
        diagnostics_.Event("MultiplayerEntityNativeActionSubmitted", detail);
        return true;
    }

    void EntityActionReplay::Shutdown() noexcept
    {
        lifecycle_ = nullptr;
        presence_ = nullptr;
        combatants_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
    }
}
