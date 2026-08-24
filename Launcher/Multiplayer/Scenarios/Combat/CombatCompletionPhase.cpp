#include "CombatCompletionPhase.h"

#include <iostream>

namespace fable::launcher::multiplayer::combat
{
int CompleteCombatScenario(PeerHarness &peers, CombatScenarioState &state, bool heroWill)
{
    Peer &host = *state.host;
    Peer &guest = *state.guest;
    const bool authorityReturned =
        heroWill || (peers.WaitEventDetail(host, "MultiplayerEntitySimulationCoverage", "fenced=0") &&
                     peers.WaitEventDetail(guest, "MultiplayerEntitySimulationCoverage", "local_simulation=0"));
    const bool survived = authorityReturned && peers.IsAlive(host) && peers.IsAlive(guest) &&
                          peers.IsResponsive(host) && peers.IsResponsive(guest) &&
                          !diagnostics::EventWasReported(diagnostics::ReadEventFile(host.events), "ClientFailed") &&
                          !diagnostics::EventWasReported(diagnostics::ReadEventFile(guest.events), "ClientFailed");
    if (state.interactive)
    {
        std::wcout
            << (survived
                    ? L"Interactive combat sequence completed; leaving both peers running for visual verification.\n"
                    : L"Interactive combat sequence did not satisfy every diagnostic assertion; leaving surviving "
                      L"peers running for visual verification.\n")
            << L"Host PID " << host.game.processId << L", guest PID " << guest.game.processId << L".\n"
            << L"State root: " << peers.context().sessionRoot.wstring() << L"\n";
        return survived ? 0 : 1;
    }
    const bool guestStopped = peers.Stop(guest);
    const bool hostStopped = peers.Stop(host);
    if (!survived || !guestStopped || !hostStopped)
    {
        std::wcerr
            << (heroWill
                    ? L"Multiplayer Hero Will acceptance failed before both peers completed the supported spell "
                      L"sequence.\n"
                    : L"Multiplayer combat authority acceptance failed during the primary-attacker lease handoff.\n");
        return 1;
    }
    std::wcout << (heroWill ? L"Multiplayer Hero Will acceptance passed in the Chamber of Fate: both Heroes submitted "
                              L"and replayed the complete supported retail Will ability sequence.\n"
                            : L"Multiplayer combat acceptance passed in the Chamber of Fate: both Heroes attacked the "
                              L"replicated enemy, exchanged PvP attacks, kept weapon transitions ordered, converged "
                              L"health, and replayed the complete retail Will ability sequence.\n")
               << L"State root: " << peers.context().sessionRoot.wstring() << L"\n";
    return 0;
}
} // namespace fable::launcher::multiplayer::combat
