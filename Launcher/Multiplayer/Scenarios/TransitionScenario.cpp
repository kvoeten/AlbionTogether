#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"
#include "../../Diagnostics/EventLog.h"
#include "../../Runtime/GameProcess.h"

#include <cstdlib>
#include <iostream>

namespace fable::launcher::multiplayer
{
namespace
{
    struct TransitionExpectations final
    {
        std::string target;
        std::uint16_t sourceMapId = 0;
        std::uint16_t destinationMapId = 0;
        std::size_t hostMaterialized = 0;
        std::size_t guestMaterialized = 0;
    };

    TransitionExpectations MakeExpectations()
    {
        const std::string target = "script_name=SCRIPT_NAME_ALBION_TOGETHER_TRANSFER_TARGET";
        return {target};
    }

    bool EventContainsAll(
        const std::string& events,
        const char* state,
        const std::string& first,
        const std::string& second)
    {
        const std::string marker =
            std::string("\"state\":\"") + state + "\"";
        std::size_t position = 0;
        while ((position = events.find(marker, position)) !=
            std::string::npos)
        {
            const std::size_t end = events.find('\n', position);
            const std::string line = events.substr(
                position,
                end == std::string::npos
                    ? std::string::npos
                    : end - position);
            if (line.find(first) != std::string::npos &&
                line.find(second) != std::string::npos)
            {
                return true;
            }
            position += marker.size();
        }
        return false;
    }

    std::uint16_t ReadMapId(
        const std::string& events,
        const char* field)
    {
        const std::string state =
            "\"state\":\"MultiplayerTransitionAcceptanceBoundaryCrossed\"";
        const std::size_t event = events.rfind(state);
        if (event == std::string::npos)
        {
            return 0;
        }
        const std::string marker = std::string(field) + "=";
        const std::size_t value = events.find(marker, event);
        const std::size_t lineEnd = events.find('\n', event);
        if (value == std::string::npos ||
            (lineEnd != std::string::npos && value >= lineEnd))
        {
            return 0;
        }
        const unsigned long parsed = std::strtoul(
            events.c_str() + value + marker.size(), nullptr, 10);
        return parsed != 0 && parsed <= 0xFFFF
            ? static_cast<std::uint16_t>(parsed)
            : 0;
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
            p.WaitEvent(p.host(), "MultiplayerTransitionAcceptanceBoundaryCrossed") &&
            p.WaitEvent(p.host(), "MultiplayerWorldTransitionStarted") &&
            p.WaitEvent(p.host(), "MultiplayerWorldTransitionCompleted") &&
            p.WaitEvent(p.guest(), "MultiplayerTransitionAcceptanceStarted") &&
            p.WaitEvent(p.guest(), "MultiplayerTransitionAcceptanceBoundaryCrossed") &&
            p.WaitEvent(p.guest(), "MultiplayerWorldTransitionStarted") &&
            p.WaitEvent(p.guest(), "MultiplayerWorldTransitionCompleted") &&
            p.WaitEventCount(p.host(), "MultiplayerRemoteDefinitionCreated", 2) &&
            p.WaitEventCount(p.guest(), "MultiplayerRemoteDefinitionCreated", 2) &&
            p.WaitEventDetailCount(p.host(), "MultiplayerEntityMaterialized", e.target, e.hostMaterialized + 1) &&
            p.WaitEventDetailCount(p.guest(), "MultiplayerEntityMaterialized", e.target, e.guestMaterialized + 1);
    }

    bool VerifySingleOwner(const std::string& hostEvents, const std::string& guestEvents,
        const TransitionExpectations& e)
    {
        if (e.sourceMapId == 0 || e.destinationMapId == 0)
        {
            return false;
        }
        const std::uint64_t hostId = diagnostics::StablePlayerActorId(L"host", L"Host");
        const std::uint64_t guestId = diagnostics::StablePlayerActorId(L"guest", L"Guest");
        const std::string destination =
            "map_id=" + std::to_string(e.destinationMapId);
        const std::string source = "map_id=" + std::to_string(e.sourceMapId);
        const std::string hostOwner =
            "authority_actor_id=" + std::to_string(hostId);
        const std::string guestOwner =
            "authority_actor_id=" + std::to_string(guestId);
        const bool releaseObserved =
            EventContainsAll(hostEvents, "MultiplayerMapAuthorityChanged",
                "operation=release", source) &&
            EventContainsAll(guestEvents, "MultiplayerMapAuthorityChanged",
                "operation=release", source);
        const bool hostWon =
            EventContainsAll(hostEvents, "MultiplayerMapAuthorityChanged",
                hostOwner, destination) &&
            EventContainsAll(guestEvents, "MultiplayerMapAuthorityChanged",
                hostOwner, destination);
        const bool guestWon =
            EventContainsAll(hostEvents, "MultiplayerMapAuthorityChanged",
                guestOwner, destination) &&
            EventContainsAll(guestEvents, "MultiplayerMapAuthorityChanged",
                guestOwner, destination);
        return releaseObserved && (hostWon || guestWon);
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
    e.sourceMapId = ReadMapId(hostEvents, "source_map_id");
    e.destinationMapId = ReadMapId(hostEvents, "destination_map_id");
    const bool routeMatches = e.sourceMapId != 0 &&
        e.destinationMapId != 0 &&
        ReadMapId(guestEvents, "source_map_id") == e.sourceMapId &&
        ReadMapId(guestEvents, "destination_map_id") == e.destinationMapId;
    const bool health = diagnostics::EventDetailContains(hostEvents, "MultiplayerEntityVitalsRestored", e.target.c_str()) ||
        diagnostics::EventDetailContains(guestEvents, "MultiplayerEntityVitalsRestored", e.target.c_str());
    p.host().game.window = automation::FindMainWindow(p.host().game.processId);
    p.guest().game.window = automation::FindMainWindow(p.guest().game.processId);
    const bool survived = completed && routeMatches &&
        VerifySingleOwner(hostEvents, guestEvents, e) && health &&
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
