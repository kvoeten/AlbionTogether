#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"
#include "../../Diagnostics/EventLog.h"
#include "../../Runtime/GameProcess.h"

#include <iostream>

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
        const std::string hostEvents = diagnostics::ReadEventFile(
            peers.host().events);
        const std::string guestEvents = diagnostics::ReadEventFile(
            peers.guest().events);
        const bool allRounds = guestCompleted &&
            diagnostics::EventCount(
                hostEvents,
                "MultiplayerMapStressTransitionCompleted") >=
                context.mapStressTransitions &&
            diagnostics::EventCount(
                guestEvents,
                "MultiplayerMapStressTransitionCompleted") >=
                context.mapStressTransitions;
        const bool alive = peers.IsAlive(peers.host()) &&
            peers.IsAlive(peers.guest());
        const bool responsive = alive &&
            peers.IsResponsive(peers.host()) &&
            peers.IsResponsive(peers.guest());

        const bool guestStopped = peers.Stop(peers.guest());
        const bool hostStopped = peers.Stop(peers.host());
        session.LeaveRunning();
        if (!allRounds || !responsive || !guestStopped || !hostStopped)
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
            << L" deterministic split, reunion, and shared map transitions.\n"
            << L"State root: " << context.sessionRoot.wstring() << L"\n";
        return 0;
    }
}
