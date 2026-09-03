#include "SavedEntityConstructionGate.h"

#include "Game/Entity/Persistence/Hooks/SavedEntityMapBlobObserver.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"
#include "Multiplayer/Persistence/QuestStateAuthorityService.h"
#include "Multiplayer/Persistence/SavedEntityMapBaselineService.h"
#include "Multiplayer/Persistence/WorldSectionAuthorityService.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace fable::multiplayer::persistence
{
    void SavedEntityConstructionGate::Initialize(
        PeerRole role,
        UdpPeer& transport,
        ReliableMessageDispatcher& reliableMessages,
        replication::RemotePlayerChannels& remotePlayers,
        replication::PlayerActionReplication& playerActions,
        authority::AuthorityReplication& authority,
        const core::Diagnostics& diagnostics,
        QuestStateAuthorityService* const questState,
        WorldSectionAuthorityService* const worldSections,
        SavedEntityMapBaselineService* const mapBaseline) noexcept
    {
        Shutdown();
        role_ = role;
        transport_ = &transport;
        reliableMessages_ = &reliableMessages;
        remotePlayers_ = &remotePlayers;
        playerActions_ = &playerActions;
        authority_ = &authority;
        questState_ = questState;
        worldSections_ = worldSections;
        mapBaseline_ = mapBaseline;
        diagnostics_ = diagnostics;
        stopping_.store(false, std::memory_order_release);
    }

    bool SavedEntityConstructionGate::Attach(
        game::entity::persistence::SavedEntityMapBlobObserver& observer)
        noexcept
    {
        if (transport_ == nullptr || reliableMessages_ == nullptr ||
            remotePlayers_ == nullptr || playerActions_ == nullptr ||
            authority_ == nullptr ||
            !observer.IsInstalled())
        {
            return false;
        }
        observer_ = &observer;
        observer_->SetPostLoadBarrierSink(
            &SavedEntityConstructionGate::AwaitPostLoad,
            this);
        diagnostics_.Event(
            "MultiplayerSavedEntityConstructionGateReady",
            role_ == PeerRole::Host
                ? "host captures its native saved simulation without a construction hold"
                : "guest blocks retail Thing construction until the host map record is installed");
        return true;
    }

    bool SavedEntityConstructionGate::AttachThingLoadFilterHook(
        game::entity::persistence::ThingSaveProjectionHook& hook) noexcept
    {
        if (role_ != PeerRole::Guest)
        {
            return true;
        }
        (void)hook;
        // The native Thing load detour exposes neither the save reader nor the
        // collection record identity. A SCRIPT_NAME_HERO-only predicate would
        // be unsafe because it could reject the guest's true selected-save
        // Hero. Leave this hook detached until a source-scoped boundary is
        // available.
        diagnostics_.Event(
            "MultiplayerSavedEntityHeroLoadFilterUnavailable",
            "preserving the guest SCRIPT_NAME_HERO because the native detour cannot distinguish host-record source");
        return true;
    }

    void SavedEntityConstructionGate::Shutdown() noexcept
    {
        stopping_.store(true, std::memory_order_release);
        if (observer_ != nullptr)
        {
            observer_->SetPostLoadBarrierSink(nullptr, nullptr);
        }
        observer_ = nullptr;
        transport_ = nullptr;
        reliableMessages_ = nullptr;
        remotePlayers_ = nullptr;
        playerActions_ = nullptr;
        authority_ = nullptr;
        questState_ = nullptr;
        worldSections_ = nullptr;
        mapBaseline_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        waiting_.store(false, std::memory_order_release);
    }

    void SavedEntityConstructionGate::OnWorldReady() noexcept
    {
        if (role_ != PeerRole::Guest || mapBaseline_ == nullptr)
        {
            return;
        }
        // WorldReady only proves that the native Hero object exists. Fable
        // continues settling its component graph afterwards; retiring and
        // reinstalling the source record here can remove the serialized
        // inventory/ability payload before the local Hero is actually usable.
        // PresentationLifecycleCoordinator keeps it pinned for the initial
        // Hero incarnation and retires it only when that Hero departs the map.
        // This also preserves data consumed lazily by inventory components.
        diagnostics_.Event(
            "MultiplayerGuestHeroInitialRecordRetirementDeferred",
            "selected-save Hero record remains pinned through its initial map incarnation");
    }

    void SavedEntityConstructionGate::AwaitPostLoad(
        void* context,
        const game::entity::persistence::SavedEntityMapCollectionEvent& event)
        noexcept
    {
        auto* const gate = static_cast<SavedEntityConstructionGate*>(context);
        if (gate == nullptr || gate->role_ != PeerRole::Guest ||
            event.phase != game::entity::persistence::
                SavedEntityMapCollectionPhase::Complete)
        {
            return;
        }
        if (event.format != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            gate->diagnostics_.Event(
                "MultiplayerSavedEntityConstructionGateBlocked",
                "text saved-entity collections have no validated host-record installer");
            gate->AbortLoad(
                "unsupported text save cannot satisfy host world authority");
            return;
        }
        try
        {
            gate->AwaitAuthoritativeMap();
        }
        catch (...)
        {
            gate->AbortLoad(
                "the pre-construction control pump raised an exception");
        }
    }

    void SavedEntityConstructionGate::AwaitAuthoritativeMap()
    {
        if (waiting_.exchange(true, std::memory_order_acq_rel))
        {
            diagnostics_.Event(
                "MultiplayerSavedEntityConstructionGateReentered",
                "a nested CSavedEntities load was not held twice");
            return;
        }

        std::string requestedMap;
        std::uint16_t requestedMapId = 0;
        bool heldReported = false;
        const std::uint64_t startedAt = GetTickCount64();
        for (;;)
        {
            if (GetTickCount64() - startedAt >= MaximumHoldMilliseconds)
            {
                Report(
                    "MultiplayerSavedEntityConstructionGateTimedOut",
                    requestedMap,
                    requestedMapId,
                    "host map preparation did not complete within 30 seconds; local construction is being released instead of hanging forever");
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-saved-entity-construction-timeout");
                AbortLoad(
                    "host world authority timed out before construction");
                return;
            }
            if (stopping_.load(std::memory_order_acquire))
            {
                Report(
                    "MultiplayerSavedEntityConstructionGateAborted",
                    requestedMap,
                    requestedMapId,
                    "multiplayer session stopped while the save was loading");
                break;
            }
            if (transport_ == nullptr || reliableMessages_ == nullptr ||
                remotePlayers_ == nullptr || authority_ == nullptr ||
                transport_->HasFailed())
            {
                Report(
                    "MultiplayerSavedEntityConstructionGateAborted",
                    requestedMap,
                    requestedMapId,
                    "multiplayer control transport failed while the save was loading");
                AbortLoad(
                    "transport failed before host world authority was applied");
                return;
            }
            if (!PumpControlLane())
            {
                Sleep(1);
                continue;
            }

            if (mapBaseline_ == nullptr ||
                !mapBaseline_->IsGuestCollectionReady())
            {
                if (!heldReported)
                {
                    Report(
                        "MultiplayerSavedEntityConstructionHeld",
                        {},
                        0,
                        "waiting for the host saved-entity collection commit and native apply");
                    heldReported = true;
                }
                Sleep(1);
                continue;
            }

            // Global manager sections load after this ENTITIES boundary.
            // Hold until their reliable host snapshots are immutable, then
            // their exact native load hooks substitute only QUESTS, REGIONS,
            // and FACTIONS as retail continues the same bundle.
            if (questState_ != nullptr &&
                !questState_->IsReadyForGuestWorldLoad())
            {
                if (!heldReported)
                {
                    Report(
                        "MultiplayerSavedEntityConstructionHeld",
                        requestedMap,
                        requestedMapId,
                        "waiting for the complete host quest snapshot before native section completion");
                    heldReported = true;
                }
                Sleep(1);
                continue;
            }

            if (worldSections_ != nullptr &&
                !worldSections_->IsGuestReady())
            {
                if (!heldReported)
                {
                    Report(
                        "MultiplayerSavedEntityConstructionHeld",
                        requestedMap,
                        requestedMapId,
                        "waiting for complete host REGIONS and FACTIONS snapshots before native section load");
                    heldReported = true;
                }
                Sleep(1);
                continue;
            }

            Report(
                "MultiplayerSavedEntityConstructionReleased",
                requestedMap,
                requestedMapId,
                "host world and exact selected-save guest Hero were installed before retail Thing construction");
            break;
        }
        waiting_.store(false, std::memory_order_release);
    }

    void SavedEntityConstructionGate::AbortLoad(
        const char* const reason) noexcept
    {
        stopping_.store(true, std::memory_order_release);
        waiting_.store(false, std::memory_order_release);
        diagnostics_.Event(
            "MultiplayerSavedEntityConstructionAborted",
            reason != nullptr ? reason : "host world authority unavailable");
        // The observer boundary has no native failure return. Exit the game
        // thread instead of ever exposing the guest's stale world as a valid
        // multiplayer session.
        PostQuitMessage(EXIT_FAILURE);
    }

    bool SavedEntityConstructionGate::PumpControlLane()
    {
        if (transport_ == nullptr || reliableMessages_ == nullptr ||
            remotePlayers_ == nullptr || authority_ == nullptr)
        {
            return false;
        }

        const std::uint64_t now = GetTickCount64();
        PlayerState inbound;
        while (transport_->TryConsume(inbound))
        {
            if ((inbound.changedProperties & player_property::Retired) != 0)
            {
                // Remove the channel first so the existing invalidation API
                // observes the retired incarnation rather than treating it
                // as the current owner and preserving its pending actions.
                remotePlayers_->Remove(inbound.actorId);
                if (playerActions_ != nullptr)
                {
                    playerActions_->InvalidateActor(inbound.actorId);
                }
                continue;
            }
            remotePlayers_->Apply(inbound, now);
        }
        const std::vector<replication::RemotePlayerSnapshot> snapshots =
            remotePlayers_->Snapshots();
        if (questState_ != nullptr)
        {
            for (const auto& snapshot : snapshots)
            {
                if (snapshot.state.role == PeerRole::Host &&
                    snapshot.state.actorId != 0)
                {
                    questState_->SetExpectedHostActor(
                        snapshot.state.actorId);
                    if (worldSections_ != nullptr)
                    {
                        worldSections_->SetExpectedHostActor(
                            snapshot.state.actorId);
                    }
                    break;
                }
            }
        }
        return authority_->Reconcile(nullptr, snapshots) &&
            reliableMessages_->Pump() && authority_->ProcessControl();
    }

    void SavedEntityConstructionGate::Report(
        const char* event,
        const std::string& mapName,
        std::uint16_t mapId,
        const char* reason) const noexcept
    {
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map=%s map_id=%u reason=%s",
            mapName.empty() ? "<pending>" : mapName.c_str(),
            static_cast<unsigned int>(mapId),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event(
            event != nullptr
                ? event
                : "MultiplayerSavedEntityConstructionGate",
            detail);
    }
}
