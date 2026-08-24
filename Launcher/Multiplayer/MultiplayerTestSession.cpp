#include "MultiplayerTestSession.h"

#include "../Configuration/LauncherConstants.h"

#include <iostream>
#include <array>

namespace
{
    using fable::launcher::multiplayer::MultiplayerTestSession;
    using fable::launcher::multiplayer::Peer;
    using fable::launcher::multiplayer::PeerHarness;

    bool WaitSixPeerRosterMatrix(MultiplayerTestSession& session)
    {
        PeerHarness& peers = session.peers();
        const std::array<Peer*, 6> views = {
            &peers.host(),
            &peers.guest(),
            &peers.guest2(),
            &peers.showcaseGuest(0),
            &peers.showcaseGuest(1),
            &peers.showcaseGuest(2)};
        for (std::size_t observer = 0; observer < views.size(); ++observer)
        {
            for (std::size_t subject = 0; subject < views.size(); ++subject)
            {
                if (observer == subject)
                {
                    continue;
                }
                const wchar_t* role = subject == 0 ? L"host" : L"guest";
                const std::string actor = std::to_string(
                    fable::launcher::diagnostics::StablePlayerActorId(
                        role, views[subject]->player));
                if (!peers.WaitEventDetail(
                        *views[observer],
                        "MultiplayerRemoteDefinitionCreated",
                        "actor_id=" + actor))
                {
                    return false;
                }
            }
        }
        return true;
    }
}

namespace fable::launcher::multiplayer
{
MultiplayerTestSession::MultiplayerTestSession(const MultiplayerTestContext& context)
    : context_(context),
      peers_(context),
      manualPlaytestEnvironment_(
          kManualPlaytestEnvironment,
          context.scenario == MultiplayerScenario::Manual ||
                  context.scenario == MultiplayerScenario::ManualRoster ||
                  context.scenario == MultiplayerScenario::ManualCombat
              ? L"1"
              : L"")
{
}

MultiplayerTestSession::~MultiplayerTestSession()
{
    if (!leaveRunning_)
    {
        peers_.StopAll();
    }
}

bool MultiplayerTestSession::Prepare()
{
    if (!manualPlaytestEnvironment_.applied())
    {
        std::wcerr << L"Could not configure manual multiplayer input ownership.\n";
        return false;
    }
    if (!peers_.PreparePeer(peers_.host()) ||
        !peers_.PreparePeer(peers_.guest()))
    {
        std::wcerr << L"Could not prepare multiplayer fixture documents.\n";
        return false;
    }
    if (context_.scenario == MultiplayerScenario::Roster ||
        context_.scenario == MultiplayerScenario::ManualRoster)
    {
        if (!peers_.PreparePeer(peers_.guest2()))
        {
            std::wcerr << L"Could not prepare multiplayer fixture documents.\n";
            return false;
        }
    }
    if (context_.scenario == MultiplayerScenario::ManualRoster)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (!peers_.PreparePeer(peers_.showcaseGuest(i)))
            {
                std::wcerr << L"Could not prepare multiplayer fixture documents.\n";
                return false;
            }
        }
    }
    return true;
}

bool MultiplayerTestSession::StartCorePeers()
{
    if (!peers_.SpawnPeer(peers_.host(), L"")) return false;
    if (!peers_.WaitReady(peers_.host(), 0)) return false;
    if (!peers_.WaitEvent(peers_.host(), "MultiplayerLocalHeroReady")) return false;
    if (!peers_.SpawnPeer(peers_.guest(), L"127.0.0.1")) return false;
    if (!peers_.WaitReady(peers_.guest(), kLocalTestWindowPitch)) return false;
    if (!peers_.WaitEvent(peers_.guest(), "MultiplayerLocalHeroReady")) return false;
    const bool automatedBasicOrRoster =
        context_.scenario == MultiplayerScenario::Basic ||
        context_.scenario == MultiplayerScenario::Roster;
    if (automatedBasicOrRoster && !peers_.Focus(peers_.host()))
    {
        return false;
    }
    if (!peers_.WaitEvent(peers_.host(), "MultiplayerRemoteDefinitionCreated")) return false;
    if (automatedBasicOrRoster && !peers_.Focus(peers_.guest()))
    {
        return false;
    }
    return peers_.WaitEvent(peers_.guest(), "MultiplayerRemoteDefinitionCreated");
}

bool MultiplayerTestSession::StartRosterPeers()
{
    if (!peers_.WaitEvent(peers_.host(), "MultiplayerSavedEntityMapBaselinePublished")) return false;
    if (!peers_.WaitEvent(peers_.guest(), "MultiplayerSavedEntityMapBaselineAccepted")) return false;
    if (!peers_.SpawnPeer(peers_.guest2(), L"127.0.0.1")) return false;
    if (!peers_.WaitReady(peers_.guest2(), kLocalTestWindowPitch * 2)) return false;
    if (!peers_.WaitEvent(peers_.guest2(), "MultiplayerLocalHeroReady")) return false;
    const std::uint64_t guest2Actor = diagnostics::StablePlayerActorId(L"guest", L"Guest Two");
    if (!peers_.WaitEventDetail(peers_.host(), "MultiplayerRemoteDefinitionCreated", "actor_id=" + std::to_string(guest2Actor))) return false;
    if (!peers_.WaitEventDetail(peers_.guest(), "MultiplayerRemoteDefinitionCreated", "actor_id=" + std::to_string(guest2Actor))) return false;
    const std::uint64_t guestActor =
        diagnostics::StablePlayerActorId(L"guest", L"Guest");
    if (!peers_.WaitEventDetail(
            peers_.guest2(),
            "MultiplayerRemoteDefinitionCreated",
            "actor_id=" + std::to_string(guestActor)))
    {
        return false;
    }
    if (context_.scenario != MultiplayerScenario::ManualRoster) return true;
    for (std::size_t index = 0; index < 3; ++index)
    {
        Peer& showcase = peers_.showcaseGuest(index);
        if (!peers_.SpawnPeer(showcase, L"127.0.0.1")) return false;
        if (!peers_.WaitReady(showcase, 850 * static_cast<int>(index + 3))) return false;
        if (!peers_.WaitEvent(showcase, "MultiplayerLocalHeroReady")) return false;
    }
    return context_.scenario != MultiplayerScenario::ManualRoster ||
        WaitSixPeerRosterMatrix(*this);
}

bool MultiplayerTestSession::PositionWindows()
{
    if (!peers_.Position(peers_.host(), 0) ||
        !peers_.Position(peers_.guest(),
            context_.scenario == MultiplayerScenario::ManualRoster
                ? kLocalTestWindowWidth
                : kLocalTestWindowPitch))
    {
        return false;
    }
    if (context_.scenario != MultiplayerScenario::Roster &&
        context_.scenario != MultiplayerScenario::ManualRoster)
    {
        return true;
    }
    if (!peers_.Position(
            peers_.guest2(),
            context_.scenario == MultiplayerScenario::ManualRoster
                ? kLocalTestWindowWidth * 2
                : kLocalTestWindowPitch * 2))
    {
        return false;
    }
    if (context_.scenario != MultiplayerScenario::ManualRoster)
    {
        return true;
    }
    return peers_.Position(peers_.showcaseGuest(0), 0, kLocalTestWindowHeight) &&
        peers_.Position(
            peers_.showcaseGuest(1),
            kLocalTestWindowWidth,
            kLocalTestWindowHeight) &&
        peers_.Position(
            peers_.showcaseGuest(2),
            kLocalTestWindowWidth * 2,
            kLocalTestWindowHeight);
}

void MultiplayerTestSession::LeaveRunning()
{
    leaveRunning_ = true;
}

bool MultiplayerTestSession::Shutdown()
{
    leaveRunning_ = true;
    bool stopped = true;
    if (context_.scenario == MultiplayerScenario::ManualRoster)
    {
        for (std::size_t index = 0; index < 3; ++index)
        {
            stopped = peers_.Stop(peers_.showcaseGuest(index)) && stopped;
        }
    }
    if (context_.scenario == MultiplayerScenario::Roster ||
        context_.scenario == MultiplayerScenario::ManualRoster)
    {
        stopped = peers_.Stop(peers_.guest2()) && stopped;
    }
    stopped = peers_.Stop(peers_.guest()) && stopped;
    stopped = peers_.Stop(peers_.host()) && stopped;
    return stopped;
}
}
