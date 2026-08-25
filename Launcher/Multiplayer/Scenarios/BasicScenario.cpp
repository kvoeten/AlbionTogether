#include "ScenarioRunners.h"

#include "../../Diagnostics/EventLog.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
int RunBasicScenario(MultiplayerTestSession& session)
{
    PeerHarness& peers = session.peers();
    const std::uint64_t hostActor =
        diagnostics::StablePlayerActorId(L"host", L"Host");
    const std::uint64_t guestActor =
        diagnostics::StablePlayerActorId(L"guest", L"Guest");
    const bool hostMoved = peers.Move(peers.host()) &&
        peers.WaitBackgroundMovement(peers.guest(), hostActor);
    const bool guestMoved = hostMoved && peers.Move(peers.guest()) &&
        peers.WaitBackgroundMovement(peers.host(), guestActor);
    const bool nativeAnimation = guestMoved &&
        (diagnostics::EventWasReported(
             diagnostics::ReadEventFile(peers.host().events),
             "MultiplayerRemoteAvatarWalking") ||
            diagnostics::EventWasReported(
                diagnostics::ReadEventFile(peers.guest().events),
                "MultiplayerRemoteAvatarWalking"));
    const bool appearancesApplied = nativeAnimation &&
        peers.WaitEvent(peers.host(), "MultiplayerRemoteAppearanceModifiersApplied") &&
        peers.WaitEvent(peers.guest(), "MultiplayerRemoteAppearanceModifiersApplied") &&
        peers.WaitEvent(peers.host(), "MultiplayerRemoteBoneScalesApplied") &&
        peers.WaitEvent(peers.guest(), "MultiplayerRemoteBoneScalesApplied");
    const bool guestStopped = session.Shutdown();
    if (!nativeAnimation || !appearancesApplied || !guestStopped)
    {
        std::wcerr << L"Multiplayer acceptance failed during movement or shutdown.\n";
        return 1;
    }
    std::wcout
        << L"Multiplayer structural acceptance passed: both remote definitions were created from the safe dispatch context, each unfocused peer applied non-zero remote physics movement without focus handoff, the shared focused native-animation path remained live, and each reconciled selected-save appearance. This command does not claim pixel-level visibility.\n"
        << L"State root: " << session.context().sessionRoot.wstring() << L"\n";
    return 0;
}
}
