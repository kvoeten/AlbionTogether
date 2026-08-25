#include "CombatAuthorityPhase.h"

namespace fable::launcher::multiplayer::combat
{
namespace
{
    bool WaitAuthorityLease(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEvent(guest, "MultiplayerCombatEngagementRequested") &&
            peers.WaitEventDetail(host, "MultiplayerActionAuthorityChanged", state.guestCombatOwner) &&
            peers.WaitEventDetail(guest, "MultiplayerActionAuthorityChanged", state.guestCombatOwner) &&
            peers.WaitEventDetail(host, "MultiplayerEntitySimulationCoverage", "fenced=1") &&
            peers.WaitEventDetail(guest, "MultiplayerEntitySimulationCoverage", "local_simulation=1") &&
            peers.WaitEventDetail(host, "MultiplayerEntityActionBegan", state.guestVitalsOwner) &&
            peers.WaitEventDetail(host, "MultiplayerRemoteCompanionRegistered", state.guestRemoteCompanion);
    }

    bool WaitVitals(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEventDetailCount(guest, "MultiplayerEntityVitalsPublished",
                state.guestVitalsOwner, 2, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerEntityVitalsAccepted",
                state.guestVitalsOwner, 2, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerEntityVitalsApplied",
                state.guestVitalsOwner, 1, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(guest, "MultiplayerCombatTargetHealthMutationApplied",
                "source=native-creature-health-setter", 2, state.timeoutSeconds);
    }

    bool WaitPlayerHealth(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEventDetailCount(guest, "MultiplayerEntityVitalsPublished",
                state.guestPlayerVitals, state.guestPlayerVitalsBeforeAttack + 1, state.timeoutSeconds) &&
            peers.WaitEvent(guest, "MultiplayerCombatGuestHealthMutationApplied") &&
            peers.WaitEventDetailCount(host, "MultiplayerRemotePlayerVitalsApplied",
                state.guestRemoteVitals, state.hostRemoteVitalsBeforeAttack + 1, state.timeoutSeconds) &&
            peers.WaitEventDetail(guest, "MultiplayerEntityActionEnded", "PlayerAttackEngagement");
    }
}

CombatPhaseResult RunCombatAuthorityPhase(PeerHarness& peers, CombatScenarioState& state)
{
    if (!WaitAuthorityLease(peers, state) || !WaitVitals(peers, state) ||
        !WaitPlayerHealth(peers, state))
    {
        return CombatPhaseResult::Failed;
    }
    return CombatPhaseResult::Ready;
}
}
