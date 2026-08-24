#include "CombatHitCandidateBuilder.h"

#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cmath>

namespace
{
    std::size_t Mix(std::size_t hash, std::uint64_t value) noexcept
    {
        hash ^= static_cast<std::size_t>(value) +
            static_cast<std::size_t>(0x9E3779B9u) +
            (hash << 6) + (hash >> 2);
        return hash;
    }
}

namespace fable::multiplayer::combat
{
    std::size_t CombatHitCandidateBuilder::SourceHash::operator()(
        const SourceKey& key) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(key.action.source.kind);
        hash = Mix(hash, key.action.source.subjectId);
        hash = Mix(hash, key.action.source.generation);
        hash = Mix(hash, key.action.source.mapEpoch);
        hash = Mix(hash, key.action.actionId);
        return Mix(hash, key.action.actionEpoch);
    }

    void CombatHitCandidateBuilder::Initialize(
        const std::uint64_t localActorId,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        replication::LocalHeroReplication& localHero,
        replication::RemotePlayerChannels& remotePlayers,
        PlayerCombatantDirectory& combatants,
        CombatActionLedger& ledger) noexcept
    {
        Clear();
        localActorId_ = localActorId;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        localHero_ = &localHero;
        remotePlayers_ = &remotePlayers;
        combatants_ = &combatants;
        ledger_ = &ledger;
        ordinals_.reserve(MaximumOrdinalEntries);
    }

    bool CombatHitCandidateBuilder::ResolveParticipant(
        const std::uint64_t localThingUid,
        Participant& participant) const noexcept
    {
        participant = {};
        if (localThingUid == 0 || combatants_ == nullptr)
        {
            return false;
        }
        const std::uint64_t actorId =
            combatants_->FindActorByThingUid(localThingUid);
        if (actorId != 0)
        {
            const PlayerState* const local = localHero_ != nullptr
                ? localHero_->CurrentState()
                : nullptr;
            if (local != nullptr && local->actorId == actorId)
            {
                participant.lifecycle = {
                    CombatSubjectKind::PlayerActor,
                    actorId,
                    local->actorGeneration,
                    local->mapEpoch};
                participant.authorityEpoch = local->authorityEpoch;
                participant.mapId = local->mapId;
                participant.ownerActorId = actorId;
                return participant.lifecycle.IsValid() &&
                    participant.authorityEpoch != 0 && participant.mapId != 0;
            }
            const PlayerState* const state = remotePlayers_ != nullptr
                ? remotePlayers_->Find(actorId)
                : nullptr;
            const replication::RemotePlayerLifecycle* const lifecycle =
                remotePlayers_ != nullptr
                ? remotePlayers_->FindLifecycle(actorId)
                : nullptr;
            if (state == nullptr || lifecycle == nullptr || !lifecycle->active)
            {
                return false;
            }
            participant.lifecycle = {
                CombatSubjectKind::PlayerActor,
                actorId,
                lifecycle->actorGeneration,
                lifecycle->mapEpoch};
            participant.authorityEpoch = state->authorityEpoch;
            participant.mapId = state->mapId;
            participant.ownerActorId = actorId;
            return participant.lifecycle.IsValid() &&
                participant.authorityEpoch != 0 && participant.mapId != 0;
        }

        if (identities_ == nullptr || lifecycle_ == nullptr)
        {
            return false;
        }
        const std::uint64_t canonical =
            identities_->CanonicalizeLocalObservation(localThingUid);
        const entities::WorldEntityRecord* const world =
            lifecycle_->Directory().Find(canonical);
        if (canonical == 0 || world == nullptr || !world->live ||
            !world->available || !world->creature || world->generation == 0 ||
            world->mapEpoch == 0 || world->mapId == 0 ||
            world->simulationOwnerActorId == 0)
        {
            return false;
        }
        participant.lifecycle = {
            CombatSubjectKind::WorldEntity,
            canonical,
            world->generation,
            world->mapEpoch};
        participant.mapId = world->mapId;
        participant.ownerActorId = world->simulationOwnerActorId;
        return true;
    }

    std::uint32_t CombatHitCandidateBuilder::NextOrdinal(
        const CombatSourceAction& action) noexcept
    {
        const SourceKey key{action};
        auto found = ordinals_.find(key);
        if (found == ordinals_.end())
        {
            if (ordinals_.size() >= MaximumOrdinalEntries)
            {
                EvictOldestOrdinal();
            }
            found = ordinals_.emplace(key, OrdinalRecord{}).first;
        }
        ++found->second.ordinal;
        found->second.serial = ++nextSerial_;
        return found->second.ordinal <= 4'095
            ? found->second.ordinal
            : 0;
    }

    void CombatHitCandidateBuilder::EvictOldestOrdinal() noexcept
    {
        auto oldest = ordinals_.end();
        for (auto current = ordinals_.begin(); current != ordinals_.end();
             ++current)
        {
            if (oldest == ordinals_.end() ||
                current->second.serial < oldest->second.serial)
            {
                oldest = current;
            }
        }
        if (oldest != ordinals_.end())
        {
            ordinals_.erase(oldest);
        }
    }

    std::uint32_t CombatHitCandidateBuilder::ReactionFlags(
        const game::creature::combat::ResolvedHitEvent& event) noexcept
    {
        using namespace protocol::combat_hit_reaction_flag;
        std::uint32_t flags = 0;
        if (event.knockDown) flags |= KnockDown;
        if (event.decapitate) flags |= Decapitate;
        if (event.blockable) flags |= Blockable;
        if (event.flourish) flags |= Flourish;
        if (event.epicSpell) flags |= EpicSpell;
        if (event.blockCounter) flags |= BlockCounter;
        if (event.playHitResponse) flags |= PlayHitResponse;
        if (event.playHitResponseOverrideSet) flags |= PlayHitResponseOverrideSet;
        if (event.moveBack) flags |= MoveBack;
        if (event.createParticleEffectOnHit) flags |= CreateParticleEffectOnHit;
        if (event.createDustParticleEffectOnHit)
            flags |= CreateDustParticleEffectOnHit;
        if (event.guaranteeHit) flags |= GuaranteeHit;
        if (event.blocked) flags |= Blocked;
        if (event.hitNegated) flags |= HitNegated;
        if (event.causeRecoil) flags |= CauseRecoil;
        if (event.hasCurrentHealth && event.currentHealth <= 0.01f)
            flags |= Killed;
        if (event.hasPreviousHealth && event.hasCurrentHealth &&
            event.currentHealth > event.previousHealth + 0.01f)
            flags |= Healing;
        return flags;
    }

    bool CombatHitCandidateBuilder::Build(
        const game::creature::combat::ResolvedHitEvent& event,
        protocol::CombatHitMessage& candidate)
    {
        candidate = {};
        if (ledger_ == nullptr || !event.hasPreviousHealth ||
            !event.hasCurrentHealth || !std::isfinite(event.previousHealth) ||
            !std::isfinite(event.currentHealth) ||
            !std::isfinite(event.maximumHealth) ||
            event.maximumHealth <= 0.0f)
        {
            return false;
        }
        Participant source;
        Participant target;
        if (!ResolveParticipant(event.sourceThingUid, source) ||
            !ResolveParticipant(event.targetThingUid, target) ||
            source.mapId != target.mapId)
        {
            return false;
        }
        // A native hit belongs to the source's simulation owner. The target
        // may be a protected remote presentation; the host validates the
        // candidate and routes the authoritative result to the target owner.
        // Requiring target ownership here would force a presentation-only
        // replay to resolve a second retail collision on another process.
        if (source.lifecycle.kind == CombatSubjectKind::PlayerActor)
        {
            if (source.lifecycle.subjectId != localActorId_)
            {
                return false;
            }
        }
        else
        {
            const entities::WorldEntityRecord* const world =
                lifecycle_->Directory().Find(source.lifecycle.subjectId);
            if (world == nullptr ||
                world->simulationOwnerActorId == 0 ||
                world->simulationOwnerActorId != localActorId_)
            {
                return false;
            }
        }

        CombatSourceAction sourceAction;
        if (!ledger_->FindAt(
                source.lifecycle, event.observedAt, sourceAction))
        {
            return false;
        }
        const std::uint32_t ordinal = NextOrdinal(sourceAction);
        if (ordinal == 0)
        {
            return false;
        }
        ++nextCandidateSequence_;
        if (nextCandidateSequence_ == 0)
        {
            ++nextCandidateSequence_;
        }

        candidate.phase = protocol::CombatHitPhase::Candidate;
        candidate.sourceDomain = source.lifecycle.kind ==
                CombatSubjectKind::PlayerActor
            ? protocol::CombatActionDomain::Player
            : protocol::CombatActionDomain::WorldEntity;
        candidate.targetKind = target.lifecycle.kind ==
                CombatSubjectKind::PlayerActor
            ? protocol::CombatParticipantKind::Player
            : protocol::CombatParticipantKind::WorldEntity;
        candidate.sourceActionId = sourceAction.actionId;
        candidate.sourceId = source.lifecycle.subjectId;
        candidate.sourceOwnerActorId = source.ownerActorId;
        candidate.sourceAuthorityEpoch =
            candidate.sourceDomain == protocol::CombatActionDomain::Player
            ? source.authorityEpoch
            : 0;
        candidate.sourceGeneration = source.lifecycle.generation;
        candidate.sourceMapEpoch = source.lifecycle.mapEpoch;
        candidate.sourceActionEpoch =
            candidate.sourceDomain == protocol::CombatActionDomain::WorldEntity
            ? sourceAction.actionEpoch
            : 0;
        candidate.sourceMapId = source.mapId;
        candidate.targetId = target.lifecycle.subjectId;
        candidate.targetAuthorityEpoch =
            candidate.targetKind == protocol::CombatParticipantKind::Player
            ? target.authorityEpoch
            : 0;
        candidate.targetGeneration = target.lifecycle.generation;
        candidate.targetMapEpoch = target.lifecycle.mapEpoch;
        candidate.targetMapId = target.mapId;
        candidate.candidateSequence = nextCandidateSequence_;
        candidate.hitOrdinal = ordinal;
        candidate.healthBefore = event.previousHealth;
        candidate.healthAfter = event.currentHealth;
        candidate.maximumHealth = event.maximumHealth;
        candidate.reactionFlags = ReactionFlags(event);
        if (event.reactionAnimationId != 0)
        {
            candidate.reactionFlags |=
                protocol::combat_hit_reaction_flag::HasReactionId;
            candidate.reactionId = event.reactionAnimationId;
        }
        if (event.positionFlag != 0)
        {
            candidate.impactFlags |=
                protocol::combat_hit_impact_flag::HasPosition;
            candidate.impactPosition = {
                event.position.x, event.position.y, event.position.z};
        }
        if (event.directionFlag != 0)
        {
            candidate.impactFlags |=
                protocol::combat_hit_impact_flag::HasDirection;
            candidate.impactDirection = {
                event.direction.x, event.direction.y, event.direction.z};
        }
        candidate.resolverActorId = localActorId_;
        return true;
    }

    void CombatHitCandidateBuilder::Clear() noexcept
    {
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        localHero_ = nullptr;
        remotePlayers_ = nullptr;
        combatants_ = nullptr;
        ledger_ = nullptr;
        ordinals_.clear();
        localActorId_ = 0;
        nextCandidateSequence_ = 0;
        nextSerial_ = 0;
    }
}
