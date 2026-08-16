#include "EntityActionReplication.h"

#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    bool IsMapScopedAction(
        fable::multiplayer::protocol::EntityActionKind kind) noexcept
    {
        using fable::multiplayer::protocol::EntityActionKind;
        return kind == EntityActionKind::Native ||
            kind == EntityActionKind::Movement;
    }

    unsigned int ActionPriority(
        const fable::multiplayer::protocol::EntityActionMessage& message)
        noexcept
    {
        using namespace fable::multiplayer::protocol;
        if (message.kind == EntityActionKind::Combat &&
            message.semanticName == "PlayerAttackEngagement")
        {
            return 4;
        }
        switch (message.kind)
        {
        case EntityActionKind::Conversation:
        case EntityActionKind::ConversationAnimation:
        case EntityActionKind::Trade:
            return 2;
        case EntityActionKind::Combat:
            return 3;
        case EntityActionKind::QuestOrCutscene:
            return 5;
        default:
            return 0;
        }
    }

    unsigned int LeasePriority(
        fable::multiplayer::protocol::ActionLeaseKind kind) noexcept
    {
        using fable::multiplayer::protocol::ActionLeaseKind;
        switch (kind)
        {
        case ActionLeaseKind::Ambient:
            return 1;
        case ActionLeaseKind::Conversation:
            return 2;
        case ActionLeaseKind::Combat:
            return 3;
        case ActionLeaseKind::PrimaryAttacker:
            return 4;
        case ActionLeaseKind::QuestOrCutscene:
            return 5;
        default:
            return 0;
        }
    }
}

namespace fable::multiplayer::replication
{
    void EntityActionReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        entities::EntityPresenceReplication& presence,
        game::creature::animation::CreatureAnimationService& animation,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        presence_ = &presence;
        animation_ = &animation;
        combat_ = &combat;
        diagnostics_ = diagnostics;
        acceptingEvents_.store(true, std::memory_order_release);
        combat_->SetPlayerAttackSink(
            &EntityActionReplication::CapturePlayerAttack,
            this);
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerEntityActionReady",
            "native creature actions use host-issued ordered action leases");
    }

    bool EntityActionReplication::Attach(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled())
        {
            return false;
        }
        if (observer_ != nullptr && observer_ != &observer)
        {
            observer_->SetEventSink(nullptr, nullptr);
        }
        observer_ = &observer;
        observer_->SetEventSink(&EntityActionReplication::CaptureEvent, this);
        diagnostics_.Event(
            "MultiplayerEntityActionAttached",
            "bounded native creature-action queue is active");
        return true;
    }

    bool EntityActionReplication::ProcessPending(
        const std::string& localMap,
        bool ownerRosterReady)
    {
        if (!initialized_ || authority_ == nullptr || lifecycle_ == nullptr ||
            identities_ == nullptr || presence_ == nullptr ||
            animation_ == nullptr)
        {
            return false;
        }
        if (!PublishPending() || !FinalizeCompletedHostActions())
        {
            return false;
        }
        PruneFencedActions();

        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            pending;
        std::deque<game::creature::combat::PlayerAttackEvent> attacks;
        {
            std::lock_guard<std::mutex> lock(pendingEventMutex_);
            pending.swap(pendingEvents_);
            attacks.swap(pendingPlayerAttacks_);
        }
        for (auto& event : pending)
        {
            event.thingUid = identities_->CanonicalizeLocalObservation(
                event.thingUid);
        }
        for (auto& attack : attacks)
        {
            attack.targetThingUid =
                identities_->CanonicalizeLocalObservation(
                attack.targetThingUid);
        }

        const authority::MapAuthorityLease* const map =
            authority_->FindMapLease(localMap);
        const std::uint32_t currentMapEpoch = map != nullptr
            ? map->epoch
            : 0;
        if (ownerRosterReady && map != nullptr && map->epoch != 0)
        {
            for (const auto& attack : attacks)
            {
                if (!BeginOrRefreshCombatEngagement(
                        attack,
                        localMap,
                        map->epoch))
                {
                    return false;
                }
            }
        }
        std::size_t suppressedActions = 0;
        std::size_t publishedActions = 0;
        using game::creature::actions::CreatureActionLifecyclePhase;
        for (const auto& event : pending)
        {
            // A completion belongs to the accepted native action that was
            // already fenced at submission time. Let it close even if the
            // authority lease changed while the action was running.
            if (event.phase == CreatureActionLifecyclePhase::Finished ||
                !event.accepted)
            {
                if (!ProcessEvent(event, localMap, currentMapEpoch))
                {
                    return false;
                }
                continue;
            }

            const entities::WorldEntityRecord* const entity =
                lifecycle_->Directory().Find(event.thingUid);
            const bool canPublish = ownerRosterReady && entity != nullptr &&
                entity->live && entity->available && entity->creature &&
                entity->mapName == localMap && entity->mapEpoch != 0 &&
                authority_->IsEntityPublisher(
                    {entity->thingUid, entity->generation},
                    entity->mapName,
                    localActorId_,
                    entity->mapEpoch);
            bool pendingLocalCombat = false;
            if (entity != nullptr)
            {
                const auto indexed = combatActionIds_.find(entity->thingUid);
                if (indexed != combatActionIds_.end())
                {
                    const auto engagement = activeActions_.find(
                        indexed->second);
                    pendingLocalCombat = engagement != activeActions_.end() &&
                        engagement->second.localOrigin &&
                        engagement->second.ownerActorId == localActorId_ &&
                        engagement->second.combatEngagement &&
                        !engagement->second.endQueued;
                }
            }
            if (!canPublish && !pendingLocalCombat)
            {
                ++suppressedActions;
                continue;
            }
            if (!ProcessEvent(event, localMap, entity->mapEpoch))
            {
                return false;
            }
            ++publishedActions;
        }
        if (suppressedActions != 0 && !nonOwnerActionReported_)
        {
            nonOwnerActionReported_ = true;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "suppressed=%zu published=%zu; only the map owner or active per-entity action owner may originate native actions",
                suppressedActions,
                publishedActions);
            diagnostics_.Event(
                "MultiplayerEntityActionSuppressed",
                detail);
        }
        else if (suppressedActions == 0)
        {
            nonOwnerActionReported_ = false;
        }

        if (!ReplayPendingAnimations())
        {
            return false;
        }
        if (!PublishPeerBaseline())
        {
            return false;
        }

        const unsigned int dropped =
            droppedEvents_.load(std::memory_order_acquire);
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            diagnostics_.Event(
                "MultiplayerEntityActionOverflow",
                "native action events exceeded the bounded queue");
            return false;
        }
        return ExpireCombatEngagements(GetTickCount64()) &&
            PublishPending() && FinalizeCompletedHostActions();
    }

    bool EntityActionReplication::ProcessEvent(
        const game::creature::actions::CreatureActionLifecycleEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        using game::creature::actions::CreatureActionLifecyclePhase;
        if (event.phase == CreatureActionLifecyclePhase::Submitted)
        {
            return !event.accepted ||
                BeginLocalAction(event, localMap, mapEpoch);
        }
        if (event.phase == CreatureActionLifecyclePhase::Finished)
        {
            return FinishLocalAction(event.action);
        }
        return true;
    }

    bool EntityActionReplication::BeginLocalAction(
        const game::creature::actions::CreatureActionLifecycleEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        if (event.thingUid == 0 || event.action == nullptr ||
            localActionIds_.find(event.action) != localActionIds_.end())
        {
            return true;
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(event.thingUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            entity->mapName != localMap)
        {
            return true;
        }

        ActiveAction active;
        active.entityUid = event.thingUid;
        active.entityGeneration = entity->generation;
        active.actionId = NextActionId();
        active.ownerActorId = localActorId_;
        active.mapEpoch = mapEpoch;
        active.kind = Classify(event.actionType);
        active.flags = FlagsFor(active.kind);
        active.animationId = event.animationId;
        active.animationFlags = 0;
        active.mapName = localMap;
        active.semanticName = event.actionType[0] != '\0'
            ? event.actionType
            : "UnknownNativeAction";
        active.nativeAction = event.action;
        active.localOrigin = true;
        const std::uint64_t actionId = active.actionId;
        activeActions_.emplace(actionId, active);
        localActionIds_[event.action] = actionId;

        protocol::EntityActionMessage intent = ToMessage(
            activeActions_.at(actionId),
            protocol::EntityActionPhase::Intent);
        if (role_ == PeerRole::Host)
        {
            return HostAcceptIntent(std::move(intent), localActorId_);
        }
        return Queue(std::move(intent));
    }

    bool EntityActionReplication::FinishLocalAction(void* nativeAction)
    {
        const auto local = localActionIds_.find(nativeAction);
        if (local == localActionIds_.end())
        {
            return true;
        }
        const auto active = activeActions_.find(local->second);
        localActionIds_.erase(local);
        if (active == activeActions_.end())
        {
            return true;
        }
        active->second.finished = true;
        if (active->second.actionEpoch == 0)
        {
            return true;
        }
        return QueueEnd(active->second);
    }

    bool EntityActionReplication::BeginOrRefreshCombatEngagement(
        const game::creature::combat::PlayerAttackEvent& event,
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        if (event.targetThingUid == 0 || localMap.empty() || mapEpoch == 0)
        {
            return true;
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(event.targetThingUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            !entity->creature || entity->mapName != localMap)
        {
            return true;
        }

        const auto indexed = combatActionIds_.find(event.targetThingUid);
        if (indexed != combatActionIds_.end())
        {
            const auto active = activeActions_.find(indexed->second);
            if (active != activeActions_.end() &&
                active->second.combatEngagement &&
                active->second.localOrigin &&
                active->second.ownerActorId == localActorId_ &&
                active->second.entityGeneration == entity->generation &&
                !active->second.endQueued)
            {
                active->second.lastActivityAt = event.observedAt != 0
                    ? event.observedAt
                    : GetTickCount64();
                if (active->second.actionEpoch != 0)
                {
                    return QueueUpdate(active->second);
                }

                protocol::EntityActionMessage retry = ToMessage(
                    active->second,
                    protocol::EntityActionPhase::Intent);
                if (role_ == PeerRole::Host)
                {
                    return HostAcceptIntent(
                        std::move(retry),
                        localActorId_);
                }
                return Queue(std::move(retry));
            }
            combatActionIds_.erase(indexed);
        }

        ActiveAction active;
        active.entityUid = event.targetThingUid;
        active.entityGeneration = entity->generation;
        active.actionId = NextActionId();
        active.ownerActorId = localActorId_;
        active.mapEpoch = mapEpoch;
        active.kind = protocol::EntityActionKind::Combat;
        active.flags = FlagsFor(active.kind);
        active.mapName = localMap;
        active.semanticName = "PlayerAttackEngagement";
        active.lastActivityAt = event.observedAt != 0
            ? event.observedAt
            : GetTickCount64();
        active.localOrigin = true;
        active.combatEngagement = true;
        const std::uint64_t actionId = active.actionId;
        activeActions_.emplace(actionId, active);
        combatActionIds_[active.entityUid] = actionId;

        protocol::EntityActionMessage intent = ToMessage(
            activeActions_.at(actionId),
            protocol::EntityActionPhase::Intent);
        if (role_ == PeerRole::Host)
        {
            return HostAcceptIntent(std::move(intent), localActorId_);
        }
        diagnostics_.Event(
            "MultiplayerCombatEngagementRequested",
            "local player attack requested primary-attacker authority for its selected NPC");
        return Queue(std::move(intent));
    }

    bool EntityActionReplication::QueueUpdate(ActiveAction& action)
    {
        if (!action.combatEngagement || action.actionEpoch == 0 ||
            action.endQueued)
        {
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            const authority::EntityAuthorityKey key{
                action.entityUid,
                action.entityGeneration,
            };
            if (!authority_->TouchActionLease(
                    key,
                    action.ownerActorId,
                    action.actionEpoch))
            {
                return true;
            }
        }
        return Queue(ToMessage(action, protocol::EntityActionPhase::Update));
    }

    bool EntityActionReplication::ExpireCombatEngagements(
        std::uint64_t now)
    {
        for (auto& entry : activeActions_)
        {
            ActiveAction& action = entry.second;
            if (!action.combatEngagement || action.endQueued ||
                action.lastActivityAt == 0 ||
                now - action.lastActivityAt < CombatIdleMilliseconds)
            {
                continue;
            }
            // The host is the final timeout authority. A guest only closes
            // the engagement it originated; observers wait for host End.
            if (role_ == PeerRole::Host || action.localOrigin)
            {
                action.finished = true;
                if (!QueueEnd(action))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void EntityActionReplication::ForgetCombatEngagement(
        const ActiveAction& action) noexcept
    {
        if (!action.combatEngagement)
        {
            return;
        }
        const auto indexed = combatActionIds_.find(action.entityUid);
        if (indexed != combatActionIds_.end() &&
            indexed->second == action.actionId)
        {
            combatActionIds_.erase(indexed);
        }
    }

    bool EntityActionReplication::HostAcceptIntent(
        protocol::EntityActionMessage intent,
        std::uint64_t sourceActorId)
    {
        const bool requestedCombatEngagement = intent.kind ==
                protocol::EntityActionKind::Combat &&
            intent.semanticName == "PlayerAttackEngagement";
        if (requestedCombatEngagement)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=received thing_uid=%016llX generation=%u action_id=%llu source_actor=%llu map=%s map_epoch=%u",
                static_cast<unsigned long long>(intent.entityUid),
                intent.entityGeneration,
                static_cast<unsigned long long>(intent.actionId),
                static_cast<unsigned long long>(sourceActorId),
                intent.mapName.c_str(),
                intent.mapEpoch);
            diagnostics_.Event("MultiplayerCombatEngagementHostTrace", detail);
        }
        const entities::WorldEntityRecord* const entity =
            lifecycle_->Directory().Find(intent.entityUid);
        if (entity == nullptr || !entity->live || !entity->available ||
            entity->mapName != intent.mapName ||
            (intent.entityGeneration != 0 &&
                intent.entityGeneration != entity->generation))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "action intent did not address the current live generation");
            return true;
        }
        if (activeActions_.find(intent.actionId) != activeActions_.end() &&
            activeActions_.at(intent.actionId).ownerActorId != sourceActorId)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "action identifier collided with another owner");
            return true;
        }

        intent.entityGeneration = entity->generation;
        intent.ownerActorId = sourceActorId;
        authority::ActionAuthorityLease granted;
        const authority::EntityAuthorityKey key{
            intent.entityUid, intent.entityGeneration};
        const authority::ActionAuthorityLease* const existingLease =
            authority_->FindActionLease(key);
        const bool combatEngagement = requestedCombatEngagement;
        const unsigned int requiredPriority = ActionPriority(intent);
        const bool borrowExistingLease = !combatEngagement &&
            existingLease != nullptr &&
            existingLease->actorId == sourceActorId &&
            existingLease->mapEpoch == intent.mapEpoch &&
            existingLease->mapName == intent.mapName &&
            LeasePriority(existingLease->kind) >= requiredPriority;
        const bool mapScoped = IsMapScopedAction(intent.kind) &&
            existingLease == nullptr &&
            authority_->IsMapPublisher(
                intent.mapName, sourceActorId, intent.mapEpoch);
        if (borrowExistingLease)
        {
            granted = *existingLease;
            authority_->TouchActionLease(
                key, sourceActorId, granted.actionEpoch);
        }
        else if (mapScoped)
        {
            granted.entity = key;
            granted.kind = protocol::ActionLeaseKind::Ambient;
            granted.actorId = sourceActorId;
            granted.mapEpoch = intent.mapEpoch;
            // Map-scoped actions are fenced by the existing map epoch. They
            // do not create another authority record or network grant.
            granted.actionEpoch = intent.mapEpoch;
            granted.mapName = intent.mapName;
            granted.localAuthority = sourceActorId == localActorId_;
        }
        else if (!authority_->RequestActionLease(
                intent, sourceActorId, granted))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "host denied the entity action lease");
            const auto local = activeActions_.find(intent.actionId);
            if (local != activeActions_.end() && local->second.localOrigin)
            {
                ForgetCombatEngagement(local->second);
                localActionIds_.erase(local->second.nativeAction);
                activeActions_.erase(local);
            }
            return true;
        }

        if (combatEngagement)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=lease-granted thing_uid=%016llX generation=%u source_actor=%llu action_epoch=%u lease_kind=%u",
                static_cast<unsigned long long>(intent.entityUid),
                intent.entityGeneration,
                static_cast<unsigned long long>(sourceActorId),
                granted.actionEpoch,
                static_cast<unsigned int>(granted.kind));
            diagnostics_.Event("MultiplayerCombatEngagementHostTrace", detail);
        }

        ActiveAction& active = activeActions_[intent.actionId];
        active.entityUid = intent.entityUid;
        active.entityGeneration = intent.entityGeneration;
        active.actionId = intent.actionId;
        active.ownerActorId = sourceActorId;
        active.mapEpoch = intent.mapEpoch;
        active.actionEpoch = granted.actionEpoch;
        active.kind = intent.kind;
        active.flags = intent.flags;
        active.animationId = intent.animationId;
        active.animationFlags = intent.animationFlags;
        active.mapName = intent.mapName;
        active.semanticName = intent.semanticName;
        active.combatEngagement = combatEngagement;
        active.ownsLease = !borrowExistingLease && !mapScoped;
        if (active.combatEngagement)
        {
            active.lastActivityAt = GetTickCount64();
            combatActionIds_[active.entityUid] = active.actionId;
        }
        protocol::EntityActionMessage begin = ToMessage(
            active,
            protocol::EntityActionPhase::Begin);
        if (active.combatEngagement)
        {
            diagnostics_.Event(
                "MultiplayerCombatEngagementHostTrace",
                "stage=begin-queued");
        }
        if (!Queue(std::move(begin)))
        {
            return false;
        }
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u action_id=%llu owner=%llu kind=%u semantic=%s local_owner=%s",
            static_cast<unsigned long long>(active.entityUid),
            active.entityGeneration,
            static_cast<unsigned long long>(active.actionId),
            static_cast<unsigned long long>(active.ownerActorId),
            static_cast<unsigned int>(active.kind),
            active.semanticName.c_str(),
            active.ownerActorId == localActorId_ ? "true" : "false");
        diagnostics_.Event("MultiplayerEntityActionBegan", detail);
        if (!ReplayAuthoritativeAnimation(active))
        {
            return false;
        }
        if (active.finished)
        {
            return QueueEnd(active);
        }
        return true;
    }

    bool EntityActionReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ || authority_ == nullptr || lifecycle_ == nullptr ||
            transportMessage.type != protocol::PacketType::EntityAction)
        {
            return false;
        }
        protocol::EntityActionMessage message;
        if (!protocol::DecodeEntityActionMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "invalid entity action payload");
            return true;
        }

        if (role_ == PeerRole::Host)
        {
            if (message.phase == protocol::EntityActionPhase::Intent)
            {
                if (message.ownerActorId != transportMessage.sourceActorId)
                {
                    return true;
                }
                return HostAcceptIntent(
                    std::move(message),
                    transportMessage.sourceActorId) && PublishPending();
            }
            if (message.phase == protocol::EntityActionPhase::Update)
            {
                const auto active = activeActions_.find(message.actionId);
                const authority::EntityAuthorityKey key{
                    message.entityUid,
                    message.entityGeneration,
                };
                const authority::ActionAuthorityLease* const lease =
                    authority_->FindActionLease(key);
                if (active == activeActions_.end() || lease == nullptr ||
                    !active->second.combatEngagement ||
                    active->second.ownerActorId !=
                        transportMessage.sourceActorId ||
                    lease->actorId != transportMessage.sourceActorId ||
                    lease->actionEpoch != message.actionEpoch)
                {
                    diagnostics_.Event(
                        "MultiplayerEntityActionStale",
                        "combat engagement refresh was fenced by the active lease");
                    return true;
                }
                if (!authority_->TouchActionLease(
                        key,
                        message.ownerActorId,
                        message.actionEpoch))
                {
                    return true;
                }
                active->second.lastActivityAt = GetTickCount64();
                return Queue(std::move(message)) && PublishPending();
            }
            if (message.phase != protocol::EntityActionPhase::End)
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionRejected",
                    "guest attempted to author a non-intent action phase");
                return true;
            }
            const auto active = activeActions_.find(message.actionId);
            const authority::EntityAuthorityKey key{
                message.entityUid,
                message.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            const bool mapScoped = active != activeActions_.end() &&
                !active->second.ownsLease &&
                IsMapScopedAction(active->second.kind) &&
                active->second.actionEpoch == active->second.mapEpoch &&
                authority_->IsMapPublisher(
                    active->second.mapName,
                    transportMessage.sourceActorId,
                    active->second.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == transportMessage.sourceActorId &&
                lease->actionEpoch == message.actionEpoch;
            if (active == activeActions_.end() ||
                active->second.ownerActorId !=
                    transportMessage.sourceActorId ||
                active->second.entityUid != message.entityUid ||
                active->second.entityGeneration !=
                    message.entityGeneration ||
                active->second.mapEpoch != message.mapEpoch ||
                active->second.actionEpoch != message.actionEpoch ||
                (!mapScoped && !leaseScoped))
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionStale",
                    "guest action end was fenced by the active lease");
                return true;
            }
            active->second.finished = true;
            active->second.endQueued = true;
            active->second.pendingRelease = true;
            if (!Queue(std::move(message)) || !PublishPending())
            {
                return false;
            }
            return FinalizeCompletedHostActions();
        }

        if (message.phase == protocol::EntityActionPhase::Intent)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionRejected",
                "guest ignored a non-host action intent");
            return true;
        }
        return AcceptAuthoritative(message);
    }

    bool EntityActionReplication::AcceptAuthoritative(
        const protocol::EntityActionMessage& message)
    {
        const authority::EntityAuthorityKey key{
            message.entityUid,
            message.entityGeneration,
        };
        const authority::ActionAuthorityLease* const lease =
            authority_->FindActionLease(key);
        if (message.phase == protocol::EntityActionPhase::Begin)
        {
            const bool mapScoped = IsMapScopedAction(message.kind) &&
                message.actionEpoch == message.mapEpoch &&
                authority_->IsMapPublisher(
                    message.mapName,
                    message.ownerActorId,
                    message.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == message.ownerActorId &&
                lease->mapEpoch == message.mapEpoch &&
                lease->actionEpoch == message.actionEpoch;
            if (!mapScoped && !leaseScoped)
            {
                diagnostics_.Event(
                    "MultiplayerEntityActionStale",
                    "action begin arrived without its ordered map or action authority fence");
                return true;
            }
            const auto existing = activeActions_.find(message.actionId);
            if (existing != activeActions_.end() &&
                existing->second.actionEpoch == message.actionEpoch &&
                existing->second.entityUid == message.entityUid &&
                existing->second.entityGeneration ==
                    message.entityGeneration &&
                existing->second.ownerActorId == message.ownerActorId &&
                existing->second.mapEpoch == message.mapEpoch &&
                existing->second.mapName == message.mapName &&
                existing->second.kind == message.kind &&
                existing->second.semanticName == message.semanticName)
            {
                // Peer-set baselines and reliable retransmission can repeat an
                // accepted Begin. Existing observers must not restart a
                // one-shot combat animation; a new observer has no entry and
                // still consumes this message normally.
                return true;
            }
            ActiveAction& active = activeActions_[message.actionId];
            const bool localOrigin = active.localOrigin;
            void* const nativeAction = active.nativeAction;
            const bool alreadyFinished = active.finished;
            const bool combatEngagement = active.combatEngagement ||
                (message.kind == protocol::EntityActionKind::Combat &&
                    message.semanticName == "PlayerAttackEngagement");
            const std::uint64_t lastActivityAt = active.lastActivityAt;
            active = {};
            active.entityUid = message.entityUid;
            active.entityGeneration = message.entityGeneration;
            active.actionId = message.actionId;
            active.ownerActorId = message.ownerActorId;
            active.mapEpoch = message.mapEpoch;
            active.actionEpoch = message.actionEpoch;
            active.kind = message.kind;
            active.flags = message.flags;
            active.animationId = message.animationId;
            active.animationFlags = message.animationFlags;
            active.mapName = message.mapName;
            active.semanticName = message.semanticName;
            active.localOrigin = localOrigin;
            active.nativeAction = nativeAction;
            active.finished = alreadyFinished;
            active.combatEngagement = combatEngagement;
            active.ownsLease = !mapScoped &&
                !IsMapScopedAction(message.kind) &&
                !(lease != nullptr &&
                    lease->kind ==
                        protocol::ActionLeaseKind::PrimaryAttacker &&
                    !combatEngagement);
            active.lastActivityAt = lastActivityAt != 0
                ? lastActivityAt
                : GetTickCount64();
            if (active.combatEngagement)
            {
                combatActionIds_[active.entityUid] = active.actionId;
            }

            char detail[384] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u action_id=%llu owner=%llu kind=%u semantic=%s local_owner=%s",
                static_cast<unsigned long long>(message.entityUid),
                message.entityGeneration,
                static_cast<unsigned long long>(message.actionId),
                static_cast<unsigned long long>(message.ownerActorId),
                static_cast<unsigned int>(message.kind),
                message.semanticName.c_str(),
                message.ownerActorId == localActorId_ ? "true" : "false");
            diagnostics_.Event("MultiplayerEntityActionBegan", detail);
            if (!ReplayAuthoritativeAnimation(active))
            {
                return false;
            }
            if (active.localOrigin && active.finished)
            {
                return QueueEnd(active) && PublishPending();
            }
            // Concrete verified codecs will replay native actions here. Until
            // then the authoritative semantic action is retained without
            // copying native action object memory.
            return true;
        }
        if (message.phase == protocol::EntityActionPhase::End)
        {
            const auto active = activeActions_.find(message.actionId);
            if (active != activeActions_.end())
            {
                ForgetCombatEngagement(active->second);
                if (active->second.nativeAction != nullptr)
                {
                    localActionIds_.erase(active->second.nativeAction);
                }
                activeActions_.erase(active);
            }
            diagnostics_.Event(
                "MultiplayerEntityActionEnded",
                message.semanticName.c_str());
            return true;
        }
        if (message.phase == protocol::EntityActionPhase::Update)
        {
            const auto active = activeActions_.find(message.actionId);
            if (active == activeActions_.end() ||
                !active->second.combatEngagement || lease == nullptr ||
                lease->actorId != message.ownerActorId ||
                lease->actionEpoch != message.actionEpoch)
            {
                return true;
            }
            active->second.lastActivityAt = GetTickCount64();
            return true;
        }
        return false;
    }

    bool EntityActionReplication::ReplayAuthoritativeAnimation(
        ActiveAction& action)
    {
        if (action.animationId == 0 || action.ownerActorId == localActorId_ ||
            action.animationReplayed || action.animationReplayFailed)
        {
            return true;
        }
        if (presence_ == nullptr || animation_ == nullptr ||
            lifecycle_ == nullptr)
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
            // Reliable action control may arrive just ahead of local native
            // materialization. Keep the active action bounded and retry while
            // its lifecycle/lease remains current.
            return true;
        }

        if (!animation_->PlayAuthoritative(
                live->thing,
                action.animationId,
                action.animationFlags))
        {
            action.animationReplayFailed = true;
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u action_id=%llu animation_id=%u semantic=%s",
                static_cast<unsigned long long>(action.entityUid),
                action.entityGeneration,
                static_cast<unsigned long long>(action.actionId),
                action.animationId,
                action.semanticName.c_str());
            diagnostics_.Event(
                "MultiplayerEntityAnimationRejected", detail);
            return true;
        }

        action.animationReplayed = true;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u action_id=%llu owner=%llu animation_id=%u semantic=%s",
            static_cast<unsigned long long>(action.entityUid),
            action.entityGeneration,
            static_cast<unsigned long long>(action.actionId),
            static_cast<unsigned long long>(action.ownerActorId),
            action.animationId,
            action.semanticName.c_str());
        diagnostics_.Event("MultiplayerEntityAnimationApplied", detail);
        return true;
    }

    bool EntityActionReplication::ReplayPendingAnimations()
    {
        for (auto& entry : activeActions_)
        {
            if (!ReplayAuthoritativeAnimation(entry.second))
            {
                return false;
            }
        }
        return true;
    }

    bool EntityActionReplication::PublishPeerBaseline()
    {
        if (role_ != PeerRole::Host || transport_ == nullptr)
        {
            return true;
        }
        const std::uint64_t peerRevision = transport_->PeerSetRevision();
        if (peerRevision == knownPeerRevision_)
        {
            return true;
        }
        for (const auto& [actionId, action] : activeActions_)
        {
            (void)actionId;
            if (action.actionEpoch == 0 || action.endQueued)
            {
                continue;
            }
            if (!Queue(ToMessage(
                    action,
                    protocol::EntityActionPhase::Begin)))
            {
                return false;
            }
        }
        if (!PublishPending())
        {
            return false;
        }
        knownPeerRevision_ = peerRevision;
        diagnostics_.Event(
            "MultiplayerEntityActionBaselinePublished",
            "current authoritative actions were replayed for the changed peer set");
        return true;
    }

    bool EntityActionReplication::QueueEnd(ActiveAction& action)
    {
        if (action.endQueued || action.actionEpoch == 0)
        {
            return true;
        }
        protocol::EntityActionMessage end = ToMessage(
            action,
            protocol::EntityActionPhase::End);
        end.outcome = protocol::EntityActionOutcome::Completed;
        if (!Queue(std::move(end)))
        {
            return false;
        }
        action.endQueued = true;
        action.pendingRelease = role_ == PeerRole::Host;
        return true;
    }

    bool EntityActionReplication::Queue(
        protocol::EntityActionMessage message)
    {
        if (pendingMessages_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionOverflow",
                "bounded semantic action publication queue is full");
            return false;
        }
        pendingMessages_.push_back(std::move(message));
        return true;
    }

    bool EntityActionReplication::PublishPending()
    {
        while (!pendingMessages_.empty())
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodeEntityActionMessage(
                    pendingMessages_.front(),
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    protocol::PacketType::EntityAction,
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
                        "MultiplayerEntityActionPublishDeferred",
                        "ordered baseline traffic is draining before queued action state");
                    publishBackpressured_ = true;
                }
                return true;
            }
            pendingMessages_.pop_front();
        }
        if (publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionPublishResumed",
                "queued action state has entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    bool EntityActionReplication::FinalizeCompletedHostActions()
    {
        if (role_ != PeerRole::Host || !pendingMessages_.empty())
        {
            return true;
        }
        std::vector<std::uint64_t> completed;
        for (const auto& entry : activeActions_)
        {
            if (entry.second.pendingRelease)
            {
                completed.push_back(entry.first);
            }
        }
        for (const std::uint64_t actionId : completed)
        {
            const auto active = activeActions_.find(actionId);
            if (active == activeActions_.end())
            {
                continue;
            }
            const authority::EntityAuthorityKey key{
                active->second.entityUid,
                active->second.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            if (lease == nullptr ||
                lease->actorId != active->second.ownerActorId ||
                lease->actionEpoch != active->second.actionEpoch)
            {
                if (active->second.nativeAction != nullptr)
                {
                    localActionIds_.erase(active->second.nativeAction);
                }
                ForgetCombatEngagement(active->second);
                activeActions_.erase(active);
                continue;
            }
            if (active->second.ownsLease &&
                !authority_->ReleaseActionLease(
                    key,
                    active->second.ownerActorId,
                    active->second.actionEpoch))
            {
                return false;
            }
            if (active->second.nativeAction != nullptr)
            {
                localActionIds_.erase(active->second.nativeAction);
            }
            ForgetCombatEngagement(active->second);
            activeActions_.erase(active);
        }
        return true;
    }

    void EntityActionReplication::PruneFencedActions()
    {
        if (authority_ == nullptr)
        {
            return;
        }
        std::vector<std::uint64_t> stale;
        stale.reserve(activeActions_.size());
        for (const auto& entry : activeActions_)
        {
            const ActiveAction& action = entry.second;
            if (action.actionEpoch == 0)
            {
                continue;
            }
            const authority::EntityAuthorityKey key{
                action.entityUid,
                action.entityGeneration,
            };
            const authority::ActionAuthorityLease* const lease =
                authority_->FindActionLease(key);
            const bool mapScoped = IsMapScopedAction(action.kind) &&
                action.actionEpoch == action.mapEpoch &&
                authority_->IsMapPublisher(
                    action.mapName,
                    action.ownerActorId,
                    action.mapEpoch);
            const bool leaseScoped = lease != nullptr &&
                lease->actorId == action.ownerActorId &&
                lease->mapEpoch == action.mapEpoch &&
                lease->actionEpoch == action.actionEpoch;
            if (!mapScoped && !leaseScoped)
            {
                stale.push_back(entry.first);
            }
        }
        for (const std::uint64_t actionId : stale)
        {
            const auto active = activeActions_.find(actionId);
            if (active == activeActions_.end())
            {
                continue;
            }
            if (active->second.nativeAction != nullptr)
            {
                localActionIds_.erase(active->second.nativeAction);
            }
            ForgetCombatEngagement(active->second);
            diagnostics_.Event(
                "MultiplayerEntityActionFenced",
                active->second.semanticName.c_str());
            activeActions_.erase(active);
        }
    }

    protocol::EntityActionMessage EntityActionReplication::ToMessage(
        const ActiveAction& action,
        protocol::EntityActionPhase phase) const
    {
        protocol::EntityActionMessage message;
        message.phase = phase;
        message.kind = action.kind;
        message.flags = action.flags;
        message.entityUid = action.entityUid;
        message.entityGeneration = action.entityGeneration;
        message.actionId = action.actionId;
        message.ownerActorId = action.ownerActorId;
        message.mapEpoch = action.mapEpoch;
        message.actionEpoch = phase == protocol::EntityActionPhase::Intent
            ? 0
            : action.actionEpoch;
        message.animationId = action.animationId;
        message.animationFlags = action.animationFlags;
        message.mapName = action.mapName;
        message.semanticName = action.semanticName;
        return message;
    }

    std::uint64_t EntityActionReplication::NextActionId() noexcept
    {
        ++nextActionId_;
        if (nextActionId_ == 0)
        {
            ++nextActionId_;
        }
        constexpr std::uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ull;
        const std::uint64_t mixed =
            (nextActionId_ * kGoldenRatio) ^ localActorId_;
        return mixed != 0 ? mixed : nextActionId_;
    }

    protocol::EntityActionKind EntityActionReplication::Classify(
        const std::string& actionType) noexcept
    {
        const auto contains = [&actionType](const char* value)
        {
            return actionType.find(value) != std::string::npos;
        };
        if (contains("ConversationAnimation"))
        {
            return protocol::EntityActionKind::ConversationAnimation;
        }
        if (contains("Talk") || contains("Conversation") ||
            contains("Speech"))
        {
            return protocol::EntityActionKind::Conversation;
        }
        if (contains("Shop") || contains("Trade") || contains("Buy") ||
            contains("Sell"))
        {
            return protocol::EntityActionKind::Trade;
        }
        if (contains("Attack") || contains("Combat") || contains("Block") ||
            contains("Shoot") || contains("Strike"))
        {
            return protocol::EntityActionKind::Combat;
        }
        if (contains("Move") || contains("Follow") || contains("Flee") ||
            contains("Wander") || contains("Patrol") ||
            contains("Navigate"))
        {
            return protocol::EntityActionKind::Movement;
        }
        if (contains("Quest") || contains("Cutscene"))
        {
            return protocol::EntityActionKind::QuestOrCutscene;
        }
        return protocol::EntityActionKind::Native;
    }

    std::uint8_t EntityActionReplication::FlagsFor(
        protocol::EntityActionKind kind) noexcept
    {
        switch (kind)
        {
        case protocol::EntityActionKind::Conversation:
        case protocol::EntityActionKind::ConversationAnimation:
        case protocol::EntityActionKind::Trade:
            return protocol::entity_action_flag::Exclusive |
                protocol::entity_action_flag::ObserverNoCamera;
        case protocol::EntityActionKind::Combat:
        case protocol::EntityActionKind::QuestOrCutscene:
            return protocol::entity_action_flag::Exclusive;
        default:
            return 0;
        }
    }

    void EntityActionReplication::CaptureEvent(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityActionReplication*>(context)->Enqueue(event);
        }
    }

    void EntityActionReplication::CapturePlayerAttack(
        void* context,
        const game::creature::combat::PlayerAttackEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityActionReplication*>(context)->
                EnqueuePlayerAttack(event);
        }
    }

    void EntityActionReplication::Enqueue(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pendingEventMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (pendingEvents_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        pendingEvents_.push_back(event);
    }

    void EntityActionReplication::EnqueuePlayerAttack(
        const game::creature::combat::PlayerAttackEvent& event) noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pendingEventMutex_);
        if (!acceptingEvents_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (pendingPlayerAttacks_.size() >= PendingEventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        pendingPlayerAttacks_.push_back(event);
    }

    void EntityActionReplication::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (combat_ != nullptr)
        {
            combat_->SetPlayerAttackSink(nullptr, nullptr);
        }
        if (observer_ != nullptr)
        {
            observer_->SetEventSink(nullptr, nullptr);
        }
        observer_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(pendingEventMutex_);
            pendingEvents_.clear();
            pendingPlayerAttacks_.clear();
        }
        pendingMessages_.clear();
        activeActions_.clear();
        localActionIds_.clear();
        combatActionIds_.clear();
        combat_ = nullptr;
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        animation_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextActionId_ = 0;
        knownPeerRevision_ = 0;
        droppedEvents_.store(0, std::memory_order_release);
        reportedDroppedEvents_ = 0;
        nonOwnerActionReported_ = false;
        publishBackpressured_ = false;
        initialized_ = false;
    }
}
