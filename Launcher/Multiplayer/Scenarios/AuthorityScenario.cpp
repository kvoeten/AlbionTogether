#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"
#include "../../Diagnostics/EventLog.h"
#include "../../Runtime/GameProcess.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
namespace
{
    struct AuthorityExpectations final
    {
        std::string grant;
        std::string owner;
        std::string recovery;
    };

    AuthorityExpectations MakeExpectations()
    {
        const std::uint64_t host = diagnostics::StablePlayerActorId(L"host", L"Host");
        const std::uint64_t guest = diagnostics::StablePlayerActorId(L"guest", L"Guest");
        return {
            "map=BowerstonePosh authority_actor_id=" + std::to_string(guest) + " epoch=2",
            "owner_actor_id=" + std::to_string(guest) + " map=BowerstonePosh epoch=2",
            "map=BowerstonePosh authority_actor_id=" + std::to_string(host) + " epoch=3"};
    }

    bool WaitHandoff(MultiplayerTestSession& session, const AuthorityExpectations& e)
    {
        PeerHarness& p = session.peers();
        return p.WaitEvent(p.host(), "MultiplayerTransitionAcceptanceStarted") &&
            p.WaitEvent(p.host(), "MultiplayerWorldTransitionCompleted") &&
            p.WaitEventDetail(p.host(), "MultiplayerMapAuthorityChanged", e.grant) &&
            p.WaitEventDetail(p.guest(), "MultiplayerMapAuthorityChanged", e.grant) &&
            p.WaitEventDetail(p.guest(), "MultiplayerEntitySimulationCoverage", "local_simulation=22 fenced=0") &&
            p.WaitEventDetail(p.guest(), "MultiplayerEntityMovementPublishedMoving", e.owner) &&
            p.WaitEventDetail(p.host(), "MultiplayerEntityMovementAcceptedMoving", e.owner);
    }

    bool VerifyStickyLease(const Peer& host, const Peer& guest, const std::string& grant)
    {
        const std::string h = diagnostics::ReadEventFile(host.events);
        const std::string g = diagnostics::ReadEventFile(guest.events);
        const bool grants = diagnostics::EventDetailCount(h, "MultiplayerMapAuthorityChanged", "operation=grant map=BowerstonePosh") == 2 &&
            diagnostics::EventDetailCount(g, "MultiplayerMapAuthorityChanged", "operation=grant map=BowerstonePosh") == 2;
        const bool detail = diagnostics::EventDetailContains(h, "MultiplayerMapAuthorityChanged", grant.c_str()) &&
            diagnostics::EventDetailContains(g, "MultiplayerMapAuthorityChanged", grant.c_str());
        return grants && detail;
    }

    bool VerifyLive(const Peer& host, const Peer& guest)
    {
        const std::string h = diagnostics::ReadEventFile(host.events);
        const std::string g = diagnostics::ReadEventFile(guest.events);
        return WaitForSingleObject(host.game.process.get(), 0) == WAIT_TIMEOUT &&
            WaitForSingleObject(guest.game.process.get(), 0) == WAIT_TIMEOUT &&
            host.game.window != nullptr && guest.game.window != nullptr &&
            automation::WindowIsResponsive(host.game.window) &&
            automation::WindowIsResponsive(guest.game.window) &&
            !diagnostics::EventWasReported(h, "ClientFailed") &&
            !diagnostics::EventWasReported(g, "ClientFailed");
    }

    bool VerifyHostLive(const Peer& host)
    {
        const std::string events = diagnostics::ReadEventFile(host.events);
        return WaitForSingleObject(host.game.process.get(), 0) == WAIT_TIMEOUT &&
            host.game.window != nullptr && automation::WindowIsResponsive(host.game.window) &&
            !diagnostics::EventWasReported(events, "ClientFailed");
    }
}

int RunAuthorityScenario(MultiplayerTestSession& session)
{
    PeerHarness& p = session.peers();
    const AuthorityExpectations e = MakeExpectations();
    const bool handoff = WaitHandoff(session, e) &&
        p.WaitEvent(p.host(), "MultiplayerTransitionAcceptanceReturned") &&
        p.WaitEventCount(p.host(), "MultiplayerRemoteDefinitionCreated", 2) &&
        p.WaitEventDetail(p.guest(), "MultiplayerRemoteAvatarResumed", "player=Host map=BowerstonePosh action=resumed");
    if (handoff)
    {
        Sleep(2'000);
    }
    p.host().game.window = automation::FindMainWindow(p.host().game.processId);
    p.guest().game.window = automation::FindMainWindow(p.guest().game.processId);
    const bool sticky = handoff && VerifyStickyLease(p.host(), p.guest(), e.grant) && VerifyLive(p.host(), p.guest());
    const bool guestStopped = sticky && runtime::CloseCreatedProcess(p.guest().game.process.get(), p.guest().game.processId, p.guest().game.shutdownEvent.get());
    const bool recovered = guestStopped && p.WaitEventDetail(p.host(), "MultiplayerMapAuthorityChanged", e.recovery) &&
        p.WaitEventDetail(p.host(), "MultiplayerEntitySimulationCoverage", "local_simulation=22 fenced=0");
    if (recovered)
    {
        Sleep(2'000);
    }
    p.host().game.window = automation::FindMainWindow(p.host().game.processId);
    const bool hostSurvived = recovered && VerifyHostLive(p.host());
    const bool hostStopped = runtime::CloseCreatedProcess(p.host().game.process.get(), p.host().game.processId, p.host().game.shutdownEvent.get());
    session.LeaveRunning();
    if (!hostSurvived || !hostStopped)
    {
        std::wcerr << L"Multiplayer authority acceptance failed during split-map handoff, sticky host re-entry, or owner-disconnect recovery.\n";
        return 1;
    }
    std::wcout << L"Multiplayer authority acceptance passed: the host left Bowerstone Posh, atomically granted epoch 2 to the remaining guest, returned without stealing that sticky lease, then recovered all 22 NPC simulations at epoch 3 after the owning guest disconnected.\n"
        << L"State root: " << session.context().sessionRoot.wstring() << L"\n";
    return 0;
}
}
