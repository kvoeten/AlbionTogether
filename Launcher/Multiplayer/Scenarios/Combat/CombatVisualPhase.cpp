#include "CombatVisualPhase.h"

#include <cstdio>

namespace fable::launcher::multiplayer::combat
{
namespace
{
    std::string PvpReactionDetail(std::uint64_t source, std::uint64_t target)
    {
        char detail[192] = {};
        std::snprintf(detail, sizeof(detail),
            "source_kind=1 source=%016llX target_kind=1 target=%016llX reaction_route=observer-replay",
            static_cast<unsigned long long>(source),
            static_cast<unsigned long long>(target));
        return detail;
    }

    bool WaitHeroWillVisual(PeerHarness& peers, CombatScenarioState& state)
    {
        return peers.WaitEvent(*state.host, "MultiplayerHeroWillSequenceArmed") &&
            peers.WaitEvent(*state.guest, "MultiplayerHeroWillSequenceArmed");
    }

    bool WaitCombatVisual(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        const std::string hostActor = std::to_string(state.hostActorId);
        return peers.WaitEventDetail(host, "MultiplayerCombatHeroAttackSubmitted",
                "source=host-local-hero target=enemy ordinal=2") &&
            peers.WaitEventDetail(guest, "MultiplayerCombatHeroAttackSubmitted",
                "source=guest-local-hero target=enemy ordinal=2") &&
            peers.WaitEventDetailCount(host, "MultiplayerCombatEnemyCounterattackSubmitted",
                "target=host-local-hero", 2, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerCombatEnemyCounterattackSubmitted",
                "target=guest-remote-hero", 2, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(guest, "MultiplayerEntityNativeActionSubmitted",
                "target_player=" + hostActor, 2, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(guest, "MultiplayerEntityNativeActionSubmitted",
                "target_player=" + state.guestActor, 2, state.timeoutSeconds) &&
            peers.WaitEventDetail(host, "MultiplayerLocalPlayerActionCaptured",
                "target_player=" + state.guestActor) &&
            peers.WaitEventDetail(guest, "MultiplayerLocalPlayerActionCaptured",
                "target_player=" + hostActor) &&
            peers.WaitEventDetail(guest, "MultiplayerRemotePlayerAbilitySubmitted",
                "actor_id=" + hostActor) &&
            peers.WaitEventDetail(host, "MultiplayerRemotePlayerAbilitySubmitted",
                "actor_id=" + state.guestActor) &&
            peers.WaitEventCount(host, "CreatureHitResolved", 1, state.timeoutSeconds) &&
            peers.WaitEventCount(guest, "CreatureHitResolved", 1, state.timeoutSeconds);
    }

    bool WaitCombatHits(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEventDetail(host, "MultiplayerCombatHitApplied", "target_kind=1") &&
            peers.WaitEventDetail(guest, "MultiplayerCombatHitApplied", "target_kind=1") &&
            peers.WaitEventDetail(host, "MultiplayerCombatHitApplied", "target_kind=2") &&
            peers.WaitEventDetail(guest, "MultiplayerCombatHitApplied", "target_kind=2") &&
            peers.WaitEventDetail(host, "MultiplayerCombatHitApplied",
                PvpReactionDetail(state.hostActorId, state.guestActorId)) &&
            peers.WaitEventDetail(guest, "MultiplayerCombatHitApplied",
                PvpReactionDetail(state.guestActorId, state.hostActorId)) &&
            peers.WaitEvent(host, "MultiplayerCombatVisualExchangeComplete") &&
            peers.WaitEvent(guest, "MultiplayerCombatVisualExchangeComplete");
    }

    bool WaitTerminalTarget(PeerHarness& peers, CombatScenarioState& state)
    {
        const std::string target = "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";
        return peers.WaitEventDetail(*state.host, "MultiplayerCombatTargetKillStarted", target) &&
            peers.WaitEventDetail(*state.host, "MultiplayerCombatTargetTerminalObserved",
                target + " health=0.000 maximum=60.000 dead=true") &&
            peers.WaitEventDetail(*state.guest, "MultiplayerCombatTargetTerminalObserved",
                target + " health=0.000 maximum=60.000 dead=true");
    }
}

CombatPhaseResult RunCombatVisualPhase(PeerHarness& peers, CombatScenarioState& state)
{
    const bool visual = state.heroWill
        ? WaitHeroWillVisual(peers, state)
        : WaitCombatVisual(peers, state) && WaitCombatHits(peers, state);
    if (!visual || !WaitTerminalTarget(peers, state))
    {
        return CombatPhaseResult::Failed;
    }
    return CombatPhaseResult::Ready;
}
}
