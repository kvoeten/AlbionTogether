#include "RemotePlayerActionPresentation.h"

#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/UdpPeer.h"
#include "RemotePlayerRegistry.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace fable::multiplayer::presentation
{
    namespace
    {
        using protocol::PlayerActionKind;

        std::uint64_t SessionAge(
            const protocol::SessionTimeMs now,
            const protocol::SessionTimeMs started) noexcept
        {
            if (!protocol::IsSessionTimeSet(started) ||
                !protocol::IsSessionTimeAtOrAfter(now, started))
            {
                return 0;
            }
            return static_cast<std::uint32_t>(now - started);
        }

    }

    bool RemotePlayerActionPresentation::Initialize(
        UdpPeer& transport,
        replication::LocalHeroReplication& localHero,
        replication::RemotePlayerChannels& remoteChannels,
        RemotePlayerRegistry& remotePlayers,
        entities::EntityPresenceReplication& presence,
        combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics,
        const std::uint64_t localActorId) noexcept
    {
        Shutdown();
        transport_ = &transport;
        localHero_ = &localHero;
        remoteChannels_ = &remoteChannels;
        remotePlayers_ = &remotePlayers;
        presence_ = &presence;
        combatants_ = &combatants;
        diagnostics_ = diagnostics;
        localActorId_ = localActorId;
        nextRevision_ = 0;
        initialized_ = true;
        return true;
    }

    std::uint32_t RemotePlayerActionPresentation::DefaultDurationMs(
        const PlayerActionKind kind) noexcept
    {
        switch (kind)
        {
        case PlayerActionKind::RangedAim:
            return 0; // held until RangedAimEnd or a newer combat action
        case PlayerActionKind::RangedAimEnd:
            return 250;
        case PlayerActionKind::Expression:
            return 1'500;
        case PlayerActionKind::HeroAbility:
            return 1'500;
        case PlayerActionKind::AbilityRequest:
            return 1'200;
        case PlayerActionKind::WeaponTransition:
            return 0; // compatibility-only and never materialized
        }
        return 1'200;
    }

    bool RemotePlayerActionPresentation::IsReplayEligible(
        const PlayerActionKind kind,
        const std::uint64_t sessionAgeMs,
        const std::uint32_t expectedDurationMs) noexcept
    {
        // Aim begin/end are durable mode mutations, not disposable one-shot
        // animations. A delayed end must still close the held remote mode.
        if (kind == PlayerActionKind::RangedAim ||
            kind == PlayerActionKind::RangedAimEnd)
        {
            return true;
        }
        if (expectedDurationMs == 0 || sessionAgeMs > expectedDurationMs)
        {
            return false;
        }
        // Keep ordinary reliable-path jitter (up to 250 ms) but never start
        // a short-lived one-shot from frame zero after most of its window.
        const std::uint64_t durationBound = std::max<std::uint64_t>(
            1,
            static_cast<std::uint64_t>(expectedDurationMs) / 4);
        const std::uint64_t toleratedLateness = std::min<std::uint64_t>(
            250,
            durationBound);
        return sessionAgeMs <= toleratedLateness;
    }

    bool RemotePlayerActionPresentation::IsRevisionNewer(
        const std::uint32_t incoming,
        const std::uint32_t current) noexcept
    {
        return incoming != current &&
            static_cast<std::uint32_t>(incoming - current) <
                0x80000000u;
    }

    bool RemotePlayerActionPresentation::EnsureTiming(
        protocol::PlayerActionMessage& message,
        UdpPeer& transport,
        const std::uint64_t localObservedAt,
        const std::uint32_t durationMs,
        std::uint32_t& nextRevision) noexcept
    {
        if (!protocol::IsSessionTimeSet(message.startedAtSessionTimeMs))
        {
            const std::uint64_t observed = localObservedAt != 0
                ? localObservedAt
                : GetTickCount64();
            message.startedAtSessionTimeMs = protocol::ToSessionTime(
                transport.LocalToSessionTimeMilliseconds(observed));
        }
        if (message.expectedDurationMs == 0 && durationMs != 0)
        {
            message.expectedDurationMs = durationMs;
        }
        if (message.presentationRevision == 0)
        {
            ++nextRevision;
            if (nextRevision == 0)
            {
                ++nextRevision;
            }
            message.presentationRevision = nextRevision;
        }
        return protocol::IsSessionTimeSet(message.startedAtSessionTimeMs) &&
            message.presentationRevision != 0;
    }

    RemotePlayerActionPresentation::Lane
    RemotePlayerActionPresentation::LaneFor(
        const PlayerActionKind kind) noexcept
    {
        switch (kind)
        {
        case PlayerActionKind::HeroAbility:
            return Lane::HeroAbility;
        case PlayerActionKind::Expression:
            return Lane::Expression;
        case PlayerActionKind::AbilityRequest:
        case PlayerActionKind::RangedAim:
        case PlayerActionKind::RangedAimEnd:
            return Lane::Combat;
        case PlayerActionKind::WeaponTransition:
            break;
        }
        return Lane::Count;
    }

    RemotePlayerActionPresentation::ActorSlots*
    RemotePlayerActionPresentation::FindActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0)
        {
            return nullptr;
        }
        for (auto& actor : actors_)
        {
            if (actor.actorId == actorId)
            {
                return &actor;
            }
        }
        return nullptr;
    }

    const RemotePlayerActionPresentation::ActorSlots*
    RemotePlayerActionPresentation::FindActor(
        const std::uint64_t actorId) const noexcept
    {
        if (actorId == 0)
        {
            return nullptr;
        }
        for (const auto& actor : actors_)
        {
            if (actor.actorId == actorId)
            {
                return &actor;
            }
        }
        return nullptr;
    }

    RemotePlayerActionPresentation::ActorSlots*
    RemotePlayerActionPresentation::GetOrAllocateActor(
        const std::uint64_t actorId) noexcept
    {
        if (auto* existing = FindActor(actorId); existing != nullptr)
        {
            return existing;
        }
        for (auto& actor : actors_)
        {
            if (actor.actorId == 0)
            {
                actor = {};
                actor.actorId = actorId;
                return &actor;
            }
        }
        // A disconnected actor can leave no explicit invalidation behind.
        // Evict the least recently offered fixed slot rather than growing a
        // replay history or allowing one actor to consume all memory.
        auto* victim = &actors_.front();
        std::uint64_t oldest = UINT64_MAX;
        for (auto& actor : actors_)
        {
            std::uint64_t newest = 0;
            for (const auto& slot : actor.lanes)
            {
                newest = newest > slot.offeredAtLocalMs
                    ? newest
                    : slot.offeredAtLocalMs;
            }
            if (newest < oldest)
            {
                oldest = newest;
                victim = &actor;
            }
        }
        *victim = {};
        victim->actorId = actorId;
        return victim;
    }

    bool RemotePlayerActionPresentation::Offer(
        protocol::PlayerActionMessage message,
        const std::uint64_t sourceConnectionNonce)
    {
        if (!initialized_ || transport_ == nullptr ||
            message.ownerActorId == 0 || message.actionId == 0)
        {
            return false;
        }
        // Durable actor-state equipment transitions superseded this legacy
        // action. Keep the packet decoder compatible, but never let one block
        // or occupy a presentation lane.
        if (message.kind == PlayerActionKind::WeaponTransition)
        {
            diagnostics_.Event(
                "MultiplayerRemoteWeaponTransitionDiscarded",
                "legacy action ignored; equipment RepNotify owns presentation");
            return true;
        }
        const Lane lane = LaneFor(message.kind);
        if (lane == Lane::Count ||
            !EnsureTiming(
                message,
                *transport_,
                0,
                DefaultDurationMs(message.kind),
                nextRevision_))
        {
            return false;
        }
        ActorSlots* const actor = GetOrAllocateActor(message.ownerActorId);
        if (actor == nullptr)
        {
            return false;
        }
        if (actor->latestRevision != 0 &&
            (actor->authorityEpoch != message.authorityEpoch ||
                actor->actorGeneration != message.actorGeneration ||
                actor->mapEpoch != message.mapEpoch ||
                (actor->sourceConnectionNonce != 0 &&
                    sourceConnectionNonce != 0 &&
                    actor->sourceConnectionNonce != sourceConnectionNonce)))
        {
            *actor = {};
            actor->actorId = message.ownerActorId;
        }
        if (actor->latestRevision != 0 && !IsRevisionNewer(
                message.presentationRevision,
                actor->latestRevision))
        {
            return true;
        }
        actor->latestRevision = message.presentationRevision;
        actor->authorityEpoch = message.authorityEpoch;
        actor->actorGeneration = message.actorGeneration;
        actor->mapEpoch = message.mapEpoch;
        actor->sourceConnectionNonce = sourceConnectionNonce;
        // Native Fable actors have one action stack. Lanes bound the retained
        // state, while a newer revision interrupts every older presentation
        // so cross-lane actions cannot submit concurrently.
        for (auto& older : actor->lanes)
        {
            if (older.active && older.message.presentationRevision !=
                    message.presentationRevision && IsRevisionNewer(
                        message.presentationRevision,
                        older.message.presentationRevision))
            {
                older = {};
            }
        }
        Slot& slot = actor->lanes[static_cast<std::size_t>(lane)];
        if (slot.active && !IsRevisionNewer(
                message.presentationRevision,
                slot.message.presentationRevision))
        {
            return true;
        }
        slot.message = std::move(message);
        slot.sourceConnectionNonce = sourceConnectionNonce;
        slot.offeredAtLocalMs = GetTickCount64();
        slot.nativeReadyAtLocalMs = 0;
        slot.active = true;
        return true;
    }

    void RemotePlayerActionPresentation::Retire(
        Slot& slot,
        const char* const reason)
    {
        if (slot.active && reason != nullptr)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu action_id=%llu lane_kind=%u reason=%s",
                static_cast<unsigned long long>(slot.message.ownerActorId),
                static_cast<unsigned long long>(slot.message.actionId),
                static_cast<unsigned int>(slot.message.kind),
                reason);
            diagnostics_.Event(
                "MultiplayerRemotePlayerActionRetired", detail);
        }
        slot = {};
    }

    bool RemotePlayerActionPresentation::Process()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            remoteChannels_ == nullptr || remotePlayers_ == nullptr)
        {
            return false;
        }
        const std::uint64_t nowLocal = GetTickCount64();
        const protocol::SessionTimeMs nowSession = protocol::ToSessionTime(
            transport_->SessionTimeMilliseconds());
        for (auto& actor : actors_)
        {
            if (actor.actorId == 0)
            {
                continue;
            }
            const PlayerState* const owner = remoteChannels_->Find(actor.actorId);
            const replication::RemotePlayerLifecycle* const lifecycle =
                remoteChannels_->FindLifecycle(actor.actorId);
            for (auto& slot : actor.lanes)
            {
                if (!slot.active)
                {
                    continue;
                }
                const auto& message = slot.message;
                if (lifecycle != nullptr &&
                    (slot.sourceConnectionNonce != 0 &&
                        lifecycle->connectionNonce != 0 &&
                        slot.sourceConnectionNonce != lifecycle->connectionNonce))
                {
                    Retire(slot, "connection-nonce-changed");
                    continue;
                }
                if (lifecycle != nullptr &&
                    (!lifecycle->active ||
                        lifecycle->actorGeneration != message.actorGeneration ||
                        lifecycle->mapEpoch != message.mapEpoch))
                {
                    Retire(slot, "actor-lifecycle-changed");
                    continue;
                }
                if (owner != nullptr &&
                    (owner->authorityEpoch != message.authorityEpoch ||
                        owner->mapName != message.mapName))
                {
                    Retire(slot, "actor-state-changed");
                    continue;
                }
                if (owner == nullptr && lifecycle == nullptr)
                {
                    Retire(slot, "actor-no-longer-known");
                    continue;
                }
                const std::uint64_t localAge = nowLocal >= slot.offeredAtLocalMs
                    ? nowLocal - slot.offeredAtLocalMs
                    : 0;
                if (!protocol::IsSessionTimeAtOrAfter(
                        nowSession, message.startedAtSessionTimeMs))
                {
                    // A synchronized action must not start before its owner
                    // timestamp. This is normally only a few milliseconds of
                    // clock/path skew; retire it if that future never arrives.
                    if (localAge >= NativeReadinessGraceMs)
                    {
                        Retire(slot, "presentation-start-remained-in-future");
                    }
                    continue;
                }
                const std::uint64_t sessionAge = SessionAge(
                    nowSession, message.startedAtSessionTimeMs);
                const std::uint64_t duration = message.expectedDurationMs;
                if (!IsReplayEligible(
                        message.kind,
                        sessionAge,
                        message.expectedDurationMs) ||
                    (duration != 0 && localAge >
                        duration + NativeReadinessGraceMs))
                {
                    const bool expired = duration != 0 &&
                        (sessionAge > duration || localAge >
                            duration + NativeReadinessGraceMs);
                    Retire(
                        slot,
                        expired
                            ? "presentation-expired"
                            : "presentation-too-late");
                    continue;
                }
                if (!localHero_->IsWorldReady() ||
                    localHero_->MapName() != message.mapName)
                {
                    Retire(slot, "map-no-longer-active");
                    continue;
                }
                if (!remotePlayers_->IsLifecycleActive(
                        message.ownerActorId,
                        message.actorGeneration,
                        message.mapEpoch))
                {
                    if (localAge >= NativeReadinessGraceMs)
                    {
                        Retire(slot, "native-owner-not-ready");
                    }
                    continue;
                }
                if (slot.nativeReadyAtLocalMs != 0 &&
                    nowLocal < slot.nativeReadyAtLocalMs)
                {
                    continue;
                }
                if (slot.nativeReadyAtLocalMs == 0)
                {
                    slot.nativeReadyAtLocalMs = nowLocal;
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
                else if (message.targetThingUid != 0 && presence_ != nullptr)
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
                if (!targetReady)
                {
                    if (localAge >= TargetResolutionGraceMs)
                    {
                        Retire(slot, "target-no-longer-available");
                    }
                    continue;
                }

                bool accepted = false;
                switch (message.kind)
                {
                case PlayerActionKind::Expression:
                    accepted = remotePlayers_->PerformExpression(
                        message.ownerActorId,
                        message.semanticName,
                        targetCreature,
                        message.resolvedActionType,
                        message.resolvedAnimationId,
                        message.expressionDurationTicks,
                        message.expressionTriggerTicks);
                    break;
                case PlayerActionKind::HeroAbility:
                    accepted = remotePlayers_->PerformHeroAbility(
                        message.ownerActorId,
                        static_cast<game::hero_pawn::abilities::HeroAbility>(
                            message.abilityId),
                        message.heroAbilityCommand,
                        message.heroAbilityProgressionState,
                        targetCreature);
                    break;
                case PlayerActionKind::RangedAimEnd:
                    accepted = remotePlayers_->EndRangedAim(message.ownerActorId);
                    break;
                case PlayerActionKind::AbilityRequest:
                case PlayerActionKind::RangedAim:
                    accepted = remotePlayers_->PerformAbility(
                        message.ownerActorId,
                        message.weaponFamily,
                        message.requiredWeapons,
                        message.requiredMeleeAttachmentSlot,
                        message.requiredRangedAttachmentSlot,
                        message.abilityId,
                        message.charge,
                        targetCreature,
                        message.resolvedActionType,
                        message.resolvedAnimationId);
                    break;
                case PlayerActionKind::WeaponTransition:
                    break;
                }
                if (accepted)
                {
                    slot = {};
                }
                else if (nowLocal >= slot.nativeReadyAtLocalMs)
                {
                    slot.nativeReadyAtLocalMs = nowLocal + NativeRetryMs;
                    if (nowLocal - slot.offeredAtLocalMs >=
                        NativeReadinessGraceMs)
                    {
                        Retire(slot, "native-replay-rejected");
                    }
                }
            }
        }
        return true;
    }

    void RemotePlayerActionPresentation::InvalidateActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0)
        {
            return;
        }
        for (auto& actor : actors_)
        {
            for (auto& slot : actor.lanes)
            {
                if (slot.active &&
                    (actor.actorId == actorId ||
                        slot.message.targetPlayerActorId == actorId))
                {
                    slot = {};
                }
            }
            bool any = false;
            for (const auto& slot : actor.lanes)
            {
                any = any || slot.active;
            }
            if (!any)
            {
                actor = {};
            }
        }
    }

    void RemotePlayerActionPresentation::InvalidateAllRemote() noexcept
    {
        for (auto& actor : actors_)
        {
            if (actor.actorId != localActorId_)
            {
                actor = {};
            }
        }
    }

    void RemotePlayerActionPresentation::Shutdown() noexcept
    {
        for (auto& actor : actors_)
        {
            actor = {};
        }
        transport_ = nullptr;
        localHero_ = nullptr;
        remoteChannels_ = nullptr;
        remotePlayers_ = nullptr;
        presence_ = nullptr;
        combatants_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        nextRevision_ = 0;
        initialized_ = false;
    }
}
