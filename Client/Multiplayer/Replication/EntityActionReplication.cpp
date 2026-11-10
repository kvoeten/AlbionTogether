#include "EntityActionReplication.h"
#include "Multiplayer/Replication/EntityActions/EntityActionSemantics.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Combat/CombatActionLedger.h"
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
    fable::multiplayer::ReliableMessageSink* ResolveEntityActionSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.actions.entityActions;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkEntityAction,
    0x1003u,
    "entity-action",
    300u,
    "multiplayer-entity-action-dispatch",
    ResolveEntityActionSink);

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
        combat::PlayerCombatantDirectory& combatants,
        combat::CombatActionLedger& combatLedger,
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
        combatants_ = &combatants;
        combatLedger_ = &combatLedger;
        diagnostics_ = diagnostics;
        publication_.Initialize(transport, diagnostics);
        replay_.Initialize(
            localActorId,
            lifecycle,
            presence,
            combatants,
            combat,
            diagnostics);
        if (!capture_.Initialize(combat))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-entity-attack-observer");
            Shutdown();
            return;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerEntityActionReady",
            "native creature actions use host-issued ordered action leases");
    }

    bool EntityActionReplication::Attach(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_)
        {
            return false;
        }
        if (!capture_.Attach(observer))
        {
            return false;
        }
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
            identities_ == nullptr || presence_ == nullptr)
        {
            return false;
        }
        if (!publication_.Publish() || !FinalizeCompletedHostActions())
        {
            return false;
        }
        PruneFencedActions();

        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            pending;
        std::deque<game::creature::combat::CreatureAbilityEvent> abilities;
        capture_.Drain(pending, abilities);
        for (auto& event : pending)
        {
            event.thingUid = identities_->CanonicalizeLocalObservation(
                event.thingUid);
            event.targetThingUid =
                identities_->CanonicalizeLocalObservation(
                    event.targetThingUid);
        }
        for (auto& ability : abilities)
        {
            ability.sourceThingUid =
                identities_->CanonicalizeLocalObservation(
                    ability.sourceThingUid);
            ability.targetThingUid =
                identities_->CanonicalizeLocalObservation(
                    ability.targetThingUid);
        }

        const authority::MapAuthorityLease* const map =
            authority_->FindMapLease(localMap);
        const std::uint32_t currentMapEpoch = map != nullptr
            ? map->epoch
            : 0;
        if (ownerRosterReady && map != nullptr && map->epoch != 0)
        {
            for (const auto& ability : abilities)
            {
                if (ability.attackCommand &&
                    !BeginOrRefreshCombatEngagement(
                        ability, localMap, map->epoch))
                {
                    return false;
                }
                const entities::WorldEntityRecord* const source =
                    lifecycle_->Directory().Find(ability.sourceThingUid);
                const bool canPublishAbility = source != nullptr &&
                    source->live && source->available && source->creature &&
                    source->mapName == localMap && source->mapEpoch != 0 &&
                    authority_->IsEntityActionPublisher(
                        {source->thingUid, source->generation},
                        source->mapName,
                        localActorId_,
                        source->mapEpoch);
                if (canPublishAbility &&
                    !BeginLocalAbility(
                        ability, localMap, source->mapEpoch))
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
                authority_->IsEntityActionPublisher(
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

        if (!ReplayPendingAbilities())
        {
            return false;
        }
        if (!ReplayPendingNativeActions())
        {
            return false;
        }
        if (!PublishPeerBaseline())
        {
            return false;
        }

        const unsigned int dropped = capture_.DroppedCount();
        if (dropped != reportedDroppedEvents_)
        {
            reportedDroppedEvents_ = dropped;
            diagnostics_.Event(
                "MultiplayerEntityActionOverflow",
                "native action events exceeded the bounded queue");
            return false;
        }
        return ExpireCombatEngagements(GetTickCount64()) &&
            publication_.Publish() && FinalizeCompletedHostActions();
    }

    void EntityActionReplication::Shutdown() noexcept
    {
        capture_.Shutdown();
        publication_.Clear();
        replay_.Shutdown();
        activeActions_.clear();
        localActionIds_.clear();
        combatActionIds_.clear();
        pendingAbilityCorrelations_.clear();
        pendingLegacyAbilities_.clear();
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        combatants_ = nullptr;
        combatLedger_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextActionId_ = 0;
        knownPeerRevision_ = 0;
        reportedDroppedEvents_ = 0;
        nonOwnerActionReported_ = false;
        initialized_ = false;
    }
}
