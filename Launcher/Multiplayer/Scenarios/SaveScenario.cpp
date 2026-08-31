#include "ScenarioRunners.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
    int RunSaveScenario(MultiplayerTestSession& session)
    {
        PeerHarness& peers = session.peers();
        Peer& host = peers.host();
        Peer& guest = peers.guest();

        if (!peers.WaitEvent(host, "MultiplayerRemoteAvatarReady") ||
            !peers.WaitEvent(guest, "MultiplayerRemoteAvatarReady") ||
            !peers.Stop(guest) ||
            !peers.WaitEvent(host, "MultiplayerAutoSaveInvoked") ||
            !peers.WaitForAutoSaveWrite(host) ||
            !peers.Stop(host))
        {
            return 1;
        }

        std::wcout
            << L"Host native save completed. Reconnecting so the guest can save its own Hero.\n";
        if (!peers.PrepareRelaunch(host, L"multiplayer_host") ||
            !peers.PrepareRelaunch(guest, L"multiplayer_guest_save") ||
            !session.StartCorePeers())
        {
            return 1;
        }

        if (!peers.WaitEvent(host, "MultiplayerRemoteAvatarReady") ||
            !peers.WaitEvent(guest, "MultiplayerRemoteAvatarReady") ||
            !peers.Stop(host) ||
            !peers.WaitEvent(guest, "MultiplayerAutoSaveInvoked") ||
            !peers.WaitForAutoSaveWrite(guest) ||
            !peers.Stop(guest))
        {
            return 1;
        }

        std::wcout
            << L"Both native player saves completed. Restarting the same isolated profiles against the same host world.\n";
        if (!peers.PrepareRelaunch(host, L"multiplayer_host") ||
            !peers.PrepareRelaunch(guest, L"multiplayer_guest") ||
            !session.StartCorePeers() || !session.PositionWindows())
        {
            std::wcerr
                << L"Could not restart both saved multiplayer Heroes.\n";
            return 1;
        }
        if (!peers.WaitEvent(host, "MultiplayerRemoteAvatarReady") ||
            !peers.WaitEvent(guest, "MultiplayerRemoteAvatarReady") ||
            !peers.WaitEvent(host,
                "MultiplayerSavedEntityMapBaselinePublished") ||
            !peers.WaitEvent(guest,
                "MultiplayerSavedEntityMapBaselineAccepted") ||
            !peers.WaitEventDetail(
                host, "MultiplayerEntityVitalsPublished", "health=129.000") ||
            !peers.WaitEventDetail(
                guest, "MultiplayerEntityVitalsPublished", "health=129.000") ||
            !peers.IsResponsive(host) || !peers.IsResponsive(guest))
        {
            return 1;
        }

        if (!session.Shutdown())
        {
            return 1;
        }
        std::wcout
            << L"Multiplayer save acceptance passed: host and guest each saved their own Hero through Fable's native save system, restarted from those files, reconstructed the same host-authoritative world, and saw each other again.\n";
        return 0;
    }
}
