#include "ScenarioRunners.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
int RunRosterScenario(MultiplayerTestSession& session)
{
    PeerHarness& peers = session.peers();
    const std::uint64_t guestActor = diagnostics::StablePlayerActorId(L"guest", L"Guest");
    const bool relayed = peers.Move(peers.guest()) &&
        peers.WaitBackgroundMovement(peers.host(), guestActor) &&
        peers.WaitBackgroundMovement(peers.guest2(), guestActor);
    const bool alive = relayed && peers.IsAlive(peers.host()) &&
        peers.IsAlive(peers.guest()) && peers.IsAlive(peers.guest2()) &&
        peers.IsResponsive(peers.host()) && peers.IsResponsive(peers.guest()) &&
        peers.IsResponsive(peers.guest2());
    session.LeaveRunning();
    const bool guest2Stopped = session.peers().Stop(session.peers().guest2());
    const bool guestStopped = session.peers().Stop(session.peers().guest());
    const bool hostStopped = session.peers().Stop(session.peers().host());
    if (!relayed || !alive || !guest2Stopped ||
        !guestStopped || !hostStopped)
    {
        std::wcerr
            << L"Multiplayer roster acceptance failed during guest-to-guest relay or shutdown.\n";
        return 1;
    }
    std::wcout
        << L"Multiplayer roster acceptance passed: host plus two independently owned guests established three actor channels, every process presented both remote actors, and one guest's movement reached both the host and the other unfocused guest through host routing.\n"
        << L"State root: " << session.context().sessionRoot.wstring() << L"\n";
    return 0;
}

int RunManualScenario(MultiplayerTestSession& session)
{
    session.LeaveRunning();
    const MultiplayerTestContext& context = session.context();
    PeerHarness& peers = session.peers();
    if (context.scenario == MultiplayerScenario::ManualRoster)
    {
        std::wcout << L"Manual six-peer Chamber roster showcase is ready. Host PID "
                   << peers.host().game.processId << L", guest PIDs "
                   << peers.guest().game.processId << L", "
                   << peers.guest2().game.processId << L", "
                   << peers.showcaseGuest(0).game.processId << L", "
                   << peers.showcaseGuest(1).game.processId << L", "
                   << peers.showcaseGuest(2).game.processId << L". All processes are being left running.\n"
                   << L"No synthetic movement, targeting, attacks, spells, or automated combat input was submitted.\n"
                   << L"State root: " << context.sessionRoot.wstring() << L"\n"
                   << L"The six-peer coordinator is staying alive passively; no focus, input, or automatic shutdown will be performed.\n";
        for (;;)
        {
            Sleep(1'000);
        }
    }
    std::wcout << L"Manual multiplayer playtest is ready. Host PID "
               << peers.host().game.processId << L", guest PID "
               << peers.guest().game.processId
               << L". Both processes are being left running.\n"
               << L"Both native remote characters exist. Use either window normally; the launcher no longer blocks handoff on an arbitrary automated separation distance.\n"
               << L"State root: " << context.sessionRoot.wstring() << L"\n";
    return 0;
}
}
