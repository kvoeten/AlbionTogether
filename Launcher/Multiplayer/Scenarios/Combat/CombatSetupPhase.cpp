#include "CombatSetupPhase.h"

#include "../../../Diagnostics/EventLog.h"

#include <iostream>
#include <windows.h>

namespace fable::launcher::multiplayer::combat
{
namespace
{
const std::string kCombatTargetScript = "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";

} // namespace

CombatPhaseResult RunCombatSetupPhase(PeerHarness &peers, CombatScenarioState &state, bool interactive, bool heroWill,
                                      unsigned int timeoutSeconds)
{
    state.host = &peers.host();
    state.guest = &peers.guest();
    state.interactive = interactive;
    state.heroWill = heroWill;
    state.timeoutSeconds = timeoutSeconds;

    if (!interactive && !peers.Move(peers.host(), 500, false))
        return CombatPhaseResult::Failed;

    Peer &host = peers.host();
    Peer &guest = peers.guest();
    const bool staged = peers.WaitEvent(host, "MultiplayerCombatPeerStaged") &&
                        peers.WaitEvent(guest, "MultiplayerCombatPeerStaged") &&
                        peers.WaitEvent(host, "MultiplayerCombatArenaConverged") &&
                        peers.WaitEvent(guest, "MultiplayerCombatArenaConverged");
    const bool targetReady = staged && peers.WaitEvent(host, "MultiplayerCombatTargetSpawned") &&
                             peers.WaitEventDetail(guest, "MultiplayerEntityMaterialized", kCombatTargetScript) &&
                             peers.WaitEvent(guest, "MultiplayerCombatTargetArmed");
    if (!targetReady)
        return CombatPhaseResult::Failed;

    if (interactive)
    {
        std::wcout << L"Manual Chamber combat playtest is ready. Host PID " << host.game.processId << L", guest PID "
                   << guest.game.processId << L". Both processes are being left running.\n"
                   << L"The Hobbe is armed, but no synthetic movement, targeting, "
                      L"equipment, health, or combat input will be submitted.\n"
                   << L"State root: " << peers.context().sessionRoot.wstring() << L"\n";
        return CombatPhaseResult::ManualLeaveRunning;
    }

    state.guestActorId = diagnostics::StablePlayerActorId(L"guest", L"Guest");
    state.hostActorId = diagnostics::StablePlayerActorId(L"host", L"Host");
    state.guestActor = std::to_string(state.guestActorId);
    state.guestCombatOwner = "kind=5 authority_actor_id=" + state.guestActor + " map=FrescoDome";
    state.guestVitalsOwner = "owner=" + state.guestActor;
    state.guestPlayerVitals = "subject=player actor=" + state.guestActor;
    state.guestRemoteVitals = "actor=" + state.guestActor;
    state.guestRemoteCompanion = "actor_id=" + state.guestActor;
    state.guestPlayerVitalsBeforeAttack = diagnostics::EventDetailCount(
        diagnostics::ReadEventFile(guest.events), "MultiplayerEntityVitalsPublished", state.guestPlayerVitals.c_str());
    state.hostRemoteVitalsBeforeAttack =
        diagnostics::EventDetailCount(diagnostics::ReadEventFile(host.events), "MultiplayerRemotePlayerVitalsApplied",
                                      state.guestRemoteVitals.c_str());
    return CombatPhaseResult::Ready;
}
} // namespace fable::launcher::multiplayer::combat
