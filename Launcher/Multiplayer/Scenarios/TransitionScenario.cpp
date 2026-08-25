#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"
#include "../../Diagnostics/EventLog.h"
#include "../../Runtime/GameProcess.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
namespace
{
    struct TransitionExpectations final
    {
        std::string target;
        std::string destination;
        std::string release;
        std::size_t hostMaterialized = 0;
        std::size_t guestMaterialized = 0;
    };

    TransitionExpectations MakeExpectations()
    {
        const std::string target = "script_name=SCRIPT_NAME_FABLE_TOGETHER_TRANSFER_TARGET";
        return {
            target,
            "operation=grant map=BowerstoneJail",
            "operation=release map=BowerstonePosh authority_actor_id=0", 0, 0};
    }

    bool WaitNativeTransfer(MultiplayerTestSession& session)
    {
        PeerHarness& p = session.peers();
        return p.WaitEvent(p.host(), "MultiplayerNpcTransferTargetSpawned") &&
            p.WaitEvent(p.host(), "MultiplayerNpcTransferSourceTeardownRequested") &&
            p.WaitEvent(p.host(), "MultiplayerEntityTransferred");
    }

    void CaptureMaterializationBaselines(PeerHarness& p, TransitionExpectations& e)
    {
        e.hostMaterialized = diagnostics::EventDetailCount(
            diagnostics::ReadEventFile(p.host().events),
            "MultiplayerEntityMaterialized",
            e.target.c_str());
        e.guestMaterialized = diagnostics::EventDetailCount(
            diagnostics::ReadEventFile(p.guest().events),
            "MultiplayerEntityMaterialized",
            e.target.c_str());
    }

    bool WaitTransitionLifecycle(MultiplayerTestSession& session, const TransitionExpectations& e)
    {
        PeerHarness& p = session.peers();
        return
            p.WaitEvent(p.host(), "MultiplayerTransitionAcceptanceStarted") &&
            p.WaitEvent(p.host(), "MultiplayerWorldTransitionStarted") &&
            p.WaitEvent(p.host(), "MultiplayerWorldTransitionCompleted") &&
            p.WaitEvent(p.host(), "MultiplayerRemoteWorldPresentationQuarantined") &&
            p.WaitEvent(p.guest(), "MultiplayerTransitionAcceptanceStarted") &&
            p.WaitEvent(p.guest(), "MultiplayerWorldTransitionStarted") &&
            p.WaitEvent(p.guest(), "MultiplayerWorldTransitionCompleted") &&
            p.WaitEvent(p.guest(), "MultiplayerRemoteWorldPresentationQuarantined") &&
            p.WaitEventCount(p.host(), "MultiplayerRemoteDefinitionCreated", 2) &&
            p.WaitEventCount(p.guest(), "MultiplayerRemoteDefinitionCreated", 2) &&
            p.WaitEventDetail(p.host(), "MultiplayerMapAuthorityChanged", e.destination) &&
            p.WaitEventDetail(p.guest(), "MultiplayerMapAuthorityChanged", e.destination) &&
            p.WaitEventDetail(p.host(), "MultiplayerMapAuthorityChanged", e.release) &&
            p.WaitEventDetail(p.guest(), "MultiplayerMapAuthorityChanged", e.release) &&
            p.WaitEventDetailCount(p.host(), "MultiplayerEntityMaterialized", e.target, e.hostMaterialized + 1) &&
            p.WaitEventDetailCount(p.guest(), "MultiplayerEntityMaterialized", e.target, e.guestMaterialized + 1);
    }

    bool VerifySingleOwner(const std::string& hostEvents, const std::string& guestEvents,
        const TransitionExpectations& e)
    {
        const std::uint64_t hostId = diagnostics::StablePlayerActorId(L"host", L"Host");
        const std::uint64_t guestId = diagnostics::StablePlayerActorId(L"guest", L"Guest");
        const std::string hostOwner = e.destination + " authority_actor_id=" + std::to_string(hostId);
        const std::string guestOwner = e.destination + " authority_actor_id=" + std::to_string(guestId);
        const bool oneEach = diagnostics::EventDetailCount(hostEvents, "MultiplayerMapAuthorityChanged", e.destination.c_str()) == 1 &&
            diagnostics::EventDetailCount(guestEvents, "MultiplayerMapAuthorityChanged", e.destination.c_str()) == 1;
        const bool hostWon = diagnostics::EventDetailContains(hostEvents, "MultiplayerMapAuthorityChanged", hostOwner.c_str()) &&
            diagnostics::EventDetailContains(guestEvents, "MultiplayerMapAuthorityChanged", hostOwner.c_str());
        const bool guestWon = diagnostics::EventDetailContains(hostEvents, "MultiplayerMapAuthorityChanged", guestOwner.c_str()) &&
            diagnostics::EventDetailContains(guestEvents, "MultiplayerMapAuthorityChanged", guestOwner.c_str());
        return oneEach && (hostWon || guestWon);
    }

    bool VerifyLiveness(const Peer& host, const Peer& guest,
        const std::string& hostEvents, const std::string& guestEvents)
    {
        const bool alive = WaitForSingleObject(host.game.process.get(), 0) == WAIT_TIMEOUT &&
            WaitForSingleObject(guest.game.process.get(), 0) == WAIT_TIMEOUT;
        const bool responsive = host.game.window != nullptr && guest.game.window != nullptr &&
            automation::WindowIsResponsive(host.game.window) && automation::WindowIsResponsive(guest.game.window);
        return alive && responsive && !diagnostics::EventWasReported(hostEvents, "ClientFailed") &&
            !diagnostics::EventWasReported(guestEvents, "ClientFailed");
    }
}

int RunTransitionScenario(MultiplayerTestSession& session)
{
    PeerHarness& p = session.peers();
    TransitionExpectations e = MakeExpectations();
    bool completed = WaitNativeTransfer(session);
    if (completed)
    {
        CaptureMaterializationBaselines(p, e);
        completed = WaitTransitionLifecycle(session, e);
    }
    if (completed)
    {
        Sleep(8'000);
    }
    const std::string hostEvents = diagnostics::ReadEventFile(p.host().events);
    const std::string guestEvents = diagnostics::ReadEventFile(p.guest().events);
    const bool health = diagnostics::EventDetailContains(hostEvents, "MultiplayerEntityVitalsRestored", e.target.c_str()) ||
        diagnostics::EventDetailContains(guestEvents, "MultiplayerEntityVitalsRestored", e.target.c_str());
    p.host().game.window = automation::FindMainWindow(p.host().game.processId);
    p.guest().game.window = automation::FindMainWindow(p.guest().game.processId);
    const bool survived = completed && VerifySingleOwner(hostEvents, guestEvents, e) && health &&
        VerifyLiveness(p.host(), p.guest(), hostEvents, guestEvents);
    const bool guestStopped = runtime::CloseCreatedProcess(p.guest().game.process.get(), p.guest().game.processId, p.guest().game.shutdownEvent.get());
    const bool hostStopped = runtime::CloseCreatedProcess(p.host().game.process.get(), p.host().game.processId, p.host().game.shutdownEvent.get());
    session.LeaveRunning();
    if (!survived || !guestStopped || !hostStopped)
    {
        std::wcerr << L"Multiplayer transition acceptance failed during destination bind, graphics-queue grace, or shutdown.\n";
        return 1;
    }
    std::wcout << L"Multiplayer transition acceptance passed: simultaneous boundary requests resolved to one destination owner, one canonical guard retained health while crossing source high-sim through host low-sim, materialized for both destination observers, and both processes remained responsive beyond the former crash windows.\n"
        << L"State root: " << session.context().sessionRoot.wstring() << L"\n";
    return 0;
}
}
