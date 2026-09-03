#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"
#include "../../Diagnostics/EventLog.h"
#include "../../Runtime/GameProcess.h"

#include <iostream>
#include <string>

namespace fable::launcher::multiplayer
{
    int RunMapStressScenario(MultiplayerTestSession& session)
    {
        PeerHarness& peers = session.peers();
        const auto& context = session.context();
        std::wcout
            << L"Map stress: seed=" << context.mapStressSeed
            << L" transitions=" << context.mapStressTransitions
            << L". The route is deterministic and can be replayed with "
               L"--map-stress-seed.\n";

        const bool hostCompleted = peers.WaitEvent(
            peers.host(), "MultiplayerMapStressCompleted");
        const bool guestCompleted = hostCompleted && peers.WaitEvent(
            peers.guest(), "MultiplayerMapStressCompleted");
        const bool hostPresentationCompleted = hostCompleted &&
            peers.WaitEvent(
                peers.host(),
                "MultiplayerOwnerScopedPresentationStabilityComplete");
        const bool guestPresentationCompleted = guestCompleted &&
            peers.WaitEvent(
                peers.guest(),
                "MultiplayerOwnerScopedPresentationStabilityComplete");
        const std::string hostEvents = diagnostics::ReadEventFile(
            peers.host().events);
        const std::string guestEvents = diagnostics::ReadEventFile(
            peers.guest().events);
        const std::string hostActorMarker = "actor_id=" +
            std::to_string(diagnostics::StablePlayerActorId(
                peers.host().role, peers.host().player));
        const std::string guestActorMarker = "actor_id=" +
            std::to_string(diagnostics::StablePlayerActorId(
                peers.guest().role, peers.guest().player));
        const bool allRounds = guestCompleted &&
            diagnostics::EventCount(
                hostEvents,
                "MultiplayerMapStressTransitionCompleted") >=
                context.mapStressTransitions &&
            diagnostics::EventCount(
                guestEvents,
                "MultiplayerMapStressTransitionCompleted") >=
                context.mapStressTransitions;
        const bool presentationStable = hostPresentationCompleted &&
            guestPresentationCompleted &&
            diagnostics::EventDetailContains(
                hostEvents,
                "MultiplayerOwnerScopedRemotePresentationVerified",
                (guestActorMarker + " clothing_changed=false").c_str()) &&
            diagnostics::EventDetailContains(
                guestEvents,
                "MultiplayerOwnerScopedRemotePresentationVerified",
                (hostActorMarker +
                    " clothing_changed=true equipment_changed=true local_owner_unchanged=true").c_str()) &&
            diagnostics::EventDetailContains(
                hostEvents,
                "MultiplayerRemoteEquipmentApplied",
                guestActorMarker.c_str()) &&
            diagnostics::EventDetailContains(
                guestEvents,
                "MultiplayerRemoteEquipmentApplied",
                hostActorMarker.c_str()) &&
            diagnostics::EventDetailContains(
                hostEvents,
                "MultiplayerRemotePresentationStateReceived",
                guestActorMarker.c_str()) &&
            diagnostics::EventDetailContains(
                guestEvents,
                "MultiplayerRemotePresentationStateReceived",
                hostActorMarker.c_str()) &&
            diagnostics::EventCount(
                hostEvents,
                "MultiplayerRemoteClothingApplied") != 0 &&
            diagnostics::EventCount(
                guestEvents,
                "MultiplayerRemoteClothingApplied") != 0 &&
            diagnostics::EventCount(
                hostEvents,
                "MultiplayerOwnerScopedPresentationCheckpoint") >= 2 &&
            diagnostics::EventCount(
                guestEvents,
                "MultiplayerOwnerScopedPresentationCheckpoint") >= 2 &&
            diagnostics::EventCount(
                hostEvents,
                "MultiplayerOwnerScopedPresentationFailed") == 0 &&
            diagnostics::EventCount(
                guestEvents,
                "MultiplayerOwnerScopedPresentationFailed") == 0;
        const bool alive = peers.IsAlive(peers.host()) &&
            peers.IsAlive(peers.guest());
        const bool responsive = alive &&
            peers.IsResponsive(peers.host()) &&
            peers.IsResponsive(peers.guest());

        const bool guestStopped = peers.Stop(peers.guest());
        const bool hostStopped = peers.Stop(peers.host());
        session.LeaveRunning();
        if (!allRounds || !presentationStable || !responsive ||
            !guestStopped || !hostStopped)
        {
            std::wcerr
                << L"Multiplayer map stress failed. Re-run with seed "
                << context.mapStressSeed
                << L" and inspect: " << context.sessionRoot.wstring()
                << L"\n";
            return 1;
        }

        std::wcout
            << L"Multiplayer map stress passed: both peers survived "
            << context.mapStressTransitions
            << L" deterministic split, reunion, and shared map transitions; "
               L"owner-scoped clothing/equipment remained stable across "
               L"same-map rebuild checkpoints.\n"
            << L"State root: " << context.sessionRoot.wstring() << L"\n";
        return 0;
    }
}
