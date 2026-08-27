#include "CombatHitApplicator.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Combat/CombatHitReactionPolicy.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/CombatHitMessage.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdio>

namespace fable::multiplayer::combat
{
    void CombatHitApplicator::Initialize(
        const std::uint64_t localActorId,
        PlayerCombatantDirectory& combatants,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        entities::EntityPresenceReplication& presence,
        replication::LocalHeroReplication& localHero,
        replication::RemotePlayerChannels& remotePlayers,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        localActorId_ = localActorId;
        combatants_ = &combatants;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        presence_ = &presence;
        localHero_ = &localHero;
        remotePlayers_ = &remotePlayers;
        combat_ = &combat;
        diagnostics_ = diagnostics;
    }

    bool CombatHitApplicator::ResolveLifecycle(
        const std::uint64_t thingUid,
        CombatLifecycle& lifecycle) const noexcept
    {
        lifecycle = {};
        if (thingUid == 0 || combatants_ == nullptr)
        {
            return false;
        }
        const std::uint64_t actorId =
            combatants_->FindActorByThingUid(thingUid);
        if (actorId != 0)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == actorId)
            {
                lifecycle = {
                    CombatSubjectKind::PlayerActor,
                    actorId,
                    local->actorGeneration,
                    local->mapEpoch};
                return lifecycle.IsValid();
            }
            const PlayerState* const remote = remotePlayers_ != nullptr
                ? remotePlayers_->Find(actorId)
                : nullptr;
            const replication::RemotePlayerLifecycle* const remoteLifecycle =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(actorId)
                : nullptr;
            if (remote == nullptr || remoteLifecycle == nullptr ||
                !remoteLifecycle->active)
            {
                return false;
            }
            lifecycle = {
                CombatSubjectKind::PlayerActor,
                actorId,
                remoteLifecycle->actorGeneration,
                remoteLifecycle->mapEpoch};
            return lifecycle.IsValid();
        }

        if (identities_ == nullptr || lifecycle_ == nullptr)
        {
            return false;
        }
        const std::uint64_t canonical =
            identities_->CanonicalizeLocalObservation(thingUid);
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(canonical);
        if (canonical == 0 || world == nullptr || !world->live ||
            !world->available || !world->creature)
        {
            return false;
        }
        lifecycle = {
            CombatSubjectKind::WorldEntity,
            canonical,
            world->generation,
            world->mapEpoch};
        return lifecycle.IsValid();
    }

    bool CombatHitApplicator::IsLocalTargetAuthority(
        const protocol::CombatHitMessage& result) const noexcept
    {
        if (result.targetKind == protocol::CombatParticipantKind::Player)
        {
            return result.targetId == localActorId_;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_ != nullptr
                ? lifecycle_->Directory().Find(result.targetId)
                : nullptr;
        return world != nullptr && world->live && world->available &&
            world->creature &&
            world->generation == result.targetGeneration &&
            world->mapEpoch == result.targetMapEpoch &&
            world->simulationOwnerActorId == localActorId_;
    }

    bool CombatHitApplicator::ObserveNativeHit(
        const game::creature::combat::ResolvedHitEvent& event) noexcept
    {
        replication::CombatHitObservation observation;
        if (!ResolveLifecycle(event.sourceThingUid, observation.source) ||
            !ResolveLifecycle(event.targetThingUid, observation.target))
        {
            return false;
        }
        // A resolved hit does not imply that retail accepted a victim action.
        // Suppression is safe only when OnHit actually replaced the target's
        // active action with a concrete native response. Hit-parameter intent
        // alone previously hid the reliable fallback when native arbitration
        // rejected that response.
        observation.nativeReactionExpected =
            event.reactionActionType[0] != '\0';
        if (!observation.nativeReactionExpected)
        {
            // Participant resolution completed. Mark the event processed even
            // though there is no native reaction to correlate, so a deferred
            // retry cannot repeatedly re-observe the same hit.
            return true;
        }
        observation.observedAt = event.observedAt;
        if (event.positionFlag != 0)
        {
            observation.impactFlags |=
                protocol::combat_hit_impact_flag::HasPosition;
            observation.impactPosition = {
                event.position.x, event.position.y, event.position.z};
        }
        if (event.directionFlag != 0)
        {
            observation.impactFlags |=
                protocol::combat_hit_impact_flag::HasDirection;
            observation.impactDirection = {
                event.direction.x, event.direction.y, event.direction.z};
        }
        nativeObservations_.Observe(observation);
        return true;
    }

    void CombatHitApplicator::ClearNativeHitObservations() noexcept
    {
        nativeObservations_.Clear();
    }

    void* CombatHitApplicator::ResolveSource(
        const protocol::CombatHitMessage& result) const noexcept
    {
        if (result.sourceDomain == protocol::CombatActionDomain::Player)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == result.sourceId)
            {
                return local->authorityEpoch == result.sourceAuthorityEpoch &&
                        local->actorGeneration == result.sourceGeneration &&
                        local->mapEpoch == result.sourceMapEpoch &&
                        local->mapId == result.sourceMapId
                    ? localHero_->NativeHero()
                    : nullptr;
            }
            const replication::RemotePlayerLifecycle* const remote =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(result.sourceId)
                : nullptr;
            const PlayerState* const state = remotePlayers_ != nullptr
                ? remotePlayers_->Find(result.sourceId)
                : nullptr;
            if (remote == nullptr || state == nullptr || !remote->active ||
                state->authorityEpoch != result.sourceAuthorityEpoch ||
                remote->actorGeneration != result.sourceGeneration ||
                remote->mapEpoch != result.sourceMapEpoch ||
                state->mapId != result.sourceMapId)
            {
                return nullptr;
            }
            return combatants_ != nullptr
                ? combatants_->FindCreature(result.sourceId)
                : nullptr;
        }

        if (result.sourceDomain != protocol::CombatActionDomain::WorldEntity ||
            lifecycle_ == nullptr || presence_ == nullptr)
        {
            return nullptr;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(result.sourceId);
        const entities::LiveEntityRecord* const live =
            presence_->LiveEntities().Find(result.sourceId);
        return world != nullptr && live != nullptr && world->live &&
                world->available && world->creature && live->creature &&
                live->thing != nullptr &&
                world->generation == result.sourceGeneration &&
                world->mapEpoch == result.sourceMapEpoch &&
                world->mapId == result.sourceMapId &&
                live->mapId == result.sourceMapId
            ? live->thing
            : nullptr;
    }

    void* CombatHitApplicator::ResolveTarget(
        const protocol::CombatHitMessage& result) const noexcept
    {
        using protocol::CombatParticipantKind;
        if (result.targetKind == CombatParticipantKind::Player)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == result.targetId)
            {
                return local->authorityEpoch == result.targetAuthorityEpoch &&
                    local->actorGeneration == result.targetGeneration &&
                    local->mapEpoch == result.targetMapEpoch &&
                    local->mapId == result.targetMapId
                    ? localHero_->NativeHero()
                    : nullptr;
            }
            const replication::RemotePlayerLifecycle* const remote =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(result.targetId)
                : nullptr;
            const PlayerState* const state = remotePlayers_ != nullptr
                ? remotePlayers_->Find(result.targetId)
                : nullptr;
            if (remote == nullptr || state == nullptr || !remote->active ||
                state->authorityEpoch != result.targetAuthorityEpoch ||
                remote->actorGeneration != result.targetGeneration ||
                remote->mapEpoch != result.targetMapEpoch ||
                state->mapId != result.targetMapId)
            {
                return nullptr;
            }
            return combatants_ != nullptr
                ? combatants_->FindCreature(result.targetId)
                : nullptr;
        }

        if (result.targetKind != CombatParticipantKind::WorldEntity ||
            lifecycle_ == nullptr || identities_ == nullptr ||
            presence_ == nullptr)
        {
            return nullptr;
        }
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(result.targetId);
        if (world == nullptr || !world->live || !world->available ||
            !world->creature || world->generation != result.targetGeneration ||
            world->mapEpoch != result.targetMapEpoch ||
            world->mapId != result.targetMapId)
        {
            return nullptr;
        }
        const std::uint64_t localUid = identities_->FindLocal(result.targetId);
        const entities::LiveEntityRecord* live =
            presence_->LiveEntities().Find(result.targetId);
        if (live == nullptr && localUid != 0)
        {
            live = presence_->LiveEntities().Find(localUid);
        }
        return live != nullptr && live->creature &&
                live->mapId == result.targetMapId
            ? live->thing
            : nullptr;
    }

    bool CombatHitApplicator::Apply(
        const protocol::CombatHitMessage& result) noexcept
    {
        if (result.phase != protocol::CombatHitPhase::Result ||
            combat_ == nullptr)
        {
            return false;
        }
        void* const target = ResolveTarget(result);
        if (target == nullptr)
        {
            return false;
        }

        const bool localResolution = result.resolverActorId == localActorId_;
        const bool damaging =
            result.healthAfter + 0.001f < result.healthBefore;
        bool terminal = result.healthAfter <= 0.01f ||
            (result.reactionFlags &
                protocol::combat_hit_reaction_flag::Killed) != 0;
        bool resultDamageApplied = false;
        float effectiveHealth = result.healthAfter;
        float effectiveMaximum = result.maximumHealth;
        const CombatLifecycle terminalLifecycle{
            result.targetKind == protocol::CombatParticipantKind::Player
                ? CombatSubjectKind::PlayerActor
                : CombatSubjectKind::WorldEntity,
            result.targetId,
            result.targetGeneration,
            result.targetMapEpoch};
        bool terminalWorldEntity = terminal &&
            result.targetKind ==
                protocol::CombatParticipantKind::WorldEntity;
        CombatTerminalTransitionState::Progress terminalProgress =
            terminalWorldEntity
                ? terminalTransitions_.Get(terminalLifecycle)
                : CombatTerminalTransitionState::Progress{};
        if (damaging && !localResolution &&
            IsLocalTargetAuthority(result) &&
            (!terminalWorldEntity || !terminalProgress.healthHandled))
        {
            const float damage = result.healthBefore - result.healthAfter;
            if (!combat_->ApplyOwnedCombatDamage(target, damage) ||
                !combat_->ReadCombatHealth(
                    target, effectiveHealth, effectiveMaximum))
            {
                return false;
            }
            resultDamageApplied = true;
            terminal = terminal || effectiveHealth <= 0.01f;
            if (!terminalWorldEntity && terminal &&
                result.targetKind ==
                    protocol::CombatParticipantKind::WorldEntity)
            {
                terminalWorldEntity = true;
                terminalProgress = terminalTransitions_.Get(
                    terminalLifecycle);
            }
            if (terminalWorldEntity)
            {
                terminalTransitions_.MarkHealthHandled(terminalLifecycle);
                terminalProgress.healthHandled = true;
            }
        }
        // Keep the concrete strike response for the terminal hit, then submit
        // the real retail death action once for this creature incarnation.
        const bool victimReactionRequested =
            ShouldSubmitVictimReaction(result);
        const bool nativeReactionObserved = victimReactionRequested &&
            nativeObservations_.Consume(result, GetTickCount64());
        const bool resolverNativeReaction =
            victimReactionRequested && localResolution;
        const bool reactionEligible =
            ShouldReplayVictimReaction(result, localActorId_) &&
            (!terminalWorldEntity || !terminalProgress.reactionHandled);
        const char* reactionRoute = "none";
        if (resolverNativeReaction)
        {
            // Candidate creation is downstream of retail OnHit, so the source
            // resolver must never re-enter the target action stack when the
            // host echoes the authoritative result. The observation only
            // distinguishes concrete native arbitration for diagnostics.
            reactionRoute = nativeReactionObserved
                ? "resolver-native-action"
                : "resolver-native-hit";
            if (terminalWorldEntity)
            {
                terminalTransitions_.MarkReactionHandled(terminalLifecycle);
                terminalProgress.reactionHandled = true;
            }
        }
        else if (reactionEligible)
        {
            if (nativeReactionObserved)
            {
                reactionRoute = "observer-native";
                diagnostics_.Event(
                    "MultiplayerCombatHitReactionSuppressed",
                    "native victim reaction was already observed locally");
            }
            else
            {
                void* const source = ResolveSource(result);
                const bool knockDown = (result.reactionFlags &
                    protocol::combat_hit_reaction_flag::KnockDown) != 0;
                float position[3] = {
                    result.impactPosition.x,
                    result.impactPosition.y,
                    result.impactPosition.z};
                float direction[3] = {
                    result.impactDirection.x,
                    result.impactDirection.y,
                    result.impactDirection.z};
                if ((result.impactFlags &
                        protocol::combat_hit_impact_flag::HasPosition) == 0)
                {
                    position[0] = 0.0f;
                    position[1] = 0.0f;
                    position[2] = 0.0f;
                }
                if ((result.impactFlags &
                        protocol::combat_hit_impact_flag::HasDirection) == 0)
                {
                    // GenericStrikeResponse accepts a direction even when
                    // retail omitted one. Use a deterministic forward vector
                    // rather than dropping the remote victim reaction.
                    direction[0] = 0.0f;
                    direction[1] = 1.0f;
                    direction[2] = 0.0f;
                }
                if (source == nullptr ||
                    !combat_->SubmitReplicatedHitReaction(
                        target,
                        source,
                        position,
                        direction,
                        knockDown,
                        result.reactionId))
                {
                    return false;
                }
                reactionRoute = "observer-replay";
            }
            if (terminalWorldEntity)
            {
                terminalTransitions_.MarkReactionHandled(terminalLifecycle);
                terminalProgress.reactionHandled = true;
            }
        }

        const char* deathRoute = "none";
        if (terminalWorldEntity)
        {
            const bool retailOwnerDeath = localResolution &&
                IsLocalTargetAuthority(result);
            if (retailOwnerDeath)
            {
                terminalTransitions_.MarkDeathSubmitted(terminalLifecycle);
                terminalProgress.deathSubmitted = true;
                deathRoute = "owner-native";
            }
            else if (terminalProgress.deathSubmitted)
            {
                deathRoute = "already-submitted";
            }
            else
            {
                if (!combat_->SubmitReplicatedDeath(target))
                {
                    return false;
                }
                terminalTransitions_.MarkDeathSubmitted(terminalLifecycle);
                terminalProgress.deathSubmitted = true;
                deathRoute = IsLocalTargetAuthority(result)
                    ? "target-authority-replay"
                    : "observer-replay";
            }
        }

        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_kind=%u source=%016llX target_kind=%u target=%016llX reaction_route=%s death_route=%s health_route=%s generation=%u map_epoch=%u revision=%llu health=%.3f effective_health=%.3f maximum=%.3f terminal=%s reaction_flags=0x%08X",
            static_cast<unsigned int>(result.sourceDomain),
            static_cast<unsigned long long>(result.sourceId),
            static_cast<unsigned int>(result.targetKind),
            static_cast<unsigned long long>(result.targetId),
            reactionRoute,
            deathRoute,
            resultDamageApplied ? "target-owner-result" :
                (localResolution ? "source-native" : "observer"),
            result.targetGeneration,
            result.targetMapEpoch,
            static_cast<unsigned long long>(result.hostTargetRevision),
            result.healthAfter,
            effectiveHealth,
            result.maximumHealth,
            terminal ? "true" : "false",
            result.reactionFlags);
        diagnostics_.Event("MultiplayerCombatHitApplied", detail);
        return true;
    }

    void CombatHitApplicator::Shutdown() noexcept
    {
        combatants_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        localHero_ = nullptr;
        remotePlayers_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        nativeObservations_.Clear();
        terminalTransitions_.Clear();
    }
}
