#include "SavedEntityConstructionGate.h"

#include "Game/Entity/Persistence/Hooks/SavedEntityMapBlobObserver.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <cstdio>
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
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        role_ = role;
        transport_ = &transport;
        reliableMessages_ = &reliableMessages;
        remotePlayers_ = &remotePlayers;
        playerActions_ = &playerActions;
        authority_ = &authority;
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
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        waiting_.store(false, std::memory_order_release);
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
                "MultiplayerSavedEntityConstructionGateBypassed",
                "the selected save used an unsupported text saved-entity collection");
            return;
        }
        try
        {
            gate->AwaitAuthoritativeMap();
        }
        catch (...)
        {
            gate->waiting_.store(false, std::memory_order_release);
            gate->diagnostics_.Event(
                "MultiplayerSavedEntityConstructionGateAborted",
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
                break;
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
                break;
            }
            if (!PumpControlLane())
            {
                Sleep(1);
                continue;
            }

            std::string hostMap;
            std::uint16_t hostMapId = 0;
            if (!ResolveHostMap(hostMap, hostMapId))
            {
                if (!heldReported)
                {
                    Report(
                        "MultiplayerSavedEntityConstructionHeld",
                        {},
                        0,
                        "waiting for the host's current native map identity");
                    heldReported = true;
                }
                Sleep(1);
                continue;
            }
            if (requestedMap != hostMap || requestedMapId != hostMapId)
            {
                requestedMap = std::move(hostMap);
                requestedMapId = hostMapId;
                heldReported = false;
            }
            if (!heldReported)
            {
                Report(
                    "MultiplayerSavedEntityConstructionHeld",
                    requestedMap,
                    requestedMapId,
                    "retail Thing construction is waiting for the host baseline");
                heldReported = true;
            }
            if (!authority_->RequestMapPreparation(
                    requestedMap,
                    requestedMapId))
            {
                Sleep(1);
                continue;
            }
            if (!PumpControlLane() ||
                !authority_->IsMapPreparationReady(
                    requestedMap,
                    requestedMapId))
            {
                Sleep(1);
                continue;
            }

            Report(
                "MultiplayerSavedEntityConstructionReleased",
                requestedMap,
                requestedMapId,
                "host baseline is installed before retail Thing construction");
            break;
        }
        waiting_.store(false, std::memory_order_release);
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
        return authority_->Reconcile(nullptr, snapshots) &&
            reliableMessages_->Pump() && authority_->ProcessControl();
    }

    bool SavedEntityConstructionGate::ResolveHostMap(
        std::string& mapName,
        std::uint16_t& mapId) const
    {
        mapName.clear();
        mapId = 0;
        if (remotePlayers_ == nullptr)
        {
            return false;
        }
        const std::vector<replication::RemotePlayerSnapshot> snapshots =
            remotePlayers_->Snapshots();
        const PlayerState* selected = nullptr;
        for (const auto& snapshot : snapshots)
        {
            const PlayerState& candidate = snapshot.state;
            if (candidate.role != PeerRole::Host || candidate.actorId == 0 ||
                candidate.mapId == 0 || candidate.mapName.empty())
            {
                continue;
            }
            if (selected == nullptr || candidate.actorId < selected->actorId)
            {
                selected = &candidate;
            }
        }
        if (selected == nullptr)
        {
            return false;
        }
        mapName = selected->mapName;
        mapId = selected->mapId;
        return true;
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
