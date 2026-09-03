#include "CombatAttackPhase.h"

namespace fable::launcher::multiplayer::combat
{
namespace
{
    bool SubmitAttack(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        const bool targeting = state.heroWill ||
            (peers.DriveFriendlyTargetedPvpAttacks(host, 2) &&
             peers.DriveFriendlyTargetedPvpAttacks(guest, 2));
        if (!targeting)
        {
            return false;
        }
        if (state.heroWill)
        {
            return true;
        }
        return peers.WaitEvent(guest, "MultiplayerCombatNativeUntargetedAttackSubmitted") &&
            peers.WaitEventDetail(guest, "MultiplayerLocalPlayerActionCaptured",
                "native_action=CCreatureAction_Interruptable") &&
            peers.WaitEventDetail(host, "MultiplayerRemoteNativeUntargetedAttackSubmitted",
                "source_action=CCreatureAction_Interruptable") &&
            peers.WaitEvent(guest, "MultiplayerCombatNativeAttackSubmitted");
    }

    bool CompleteMeleeHandoff(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEvent(guest, "MultiplayerCombatNativeMeleeReady") &&
            peers.WaitEventDetail(guest, "MultiplayerLocalPlayerActionCaptured",
                "native_action=CCreatureAction_Interruptable") &&
            peers.WaitEventDetail(host, "MultiplayerRemotePlayerAbilitySubmitted", "ability_id=1101") &&
            peers.WaitEventDetail(host, "MultiplayerRemoteNativeAttackSubmitted",
                "route=retail-ai-immediate-attack submitted=true") &&
            peers.WaitEventDetail(guest, "MultiplayerCombatNativeSustainedAttackSubmitted", "ordinal=6/6") &&
            peers.WaitEventDetailCount(host, "MultiplayerRemoteNativeUntargetedAttackSubmitted", "submitted=true", 7, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerRemotePlayerAbilitySubmitted", "ability_id=1101", 8, state.timeoutSeconds) &&
            peers.WaitEvent(guest, "MultiplayerCombatNativeMeleeStowed") &&
            peers.WaitEvent(guest, "MultiplayerCombatNativeMeleeRedrawReady");
    }

    bool CompleteWeaponTransitions(PeerHarness& peers, CombatScenarioState& state)
    {
        Peer& host = *state.host;
        Peer& guest = *state.guest;
        return peers.WaitEvent(guest,
                "MultiplayerCombatNativeUnarmedAttackSubmitted") &&
            peers.WaitEventDetail(guest,
                "MultiplayerLocalPlayerActionCaptured",
                "weapon=0 melee=") &&
            peers.WaitEventDetail(host,
                "MultiplayerRemoteNativeAttackSubmitted",
                "weapon_family=0") &&
            peers.WaitEventDetail(host,
                "MultiplayerRemoteNativeAttackSubmitted",
                "route=native-hero-auto-turn-action submitted=true") &&
            peers.WaitEventDetailCount(guest, "MultiplayerLocalWeaponTransitionCaptured",
                "native_action=CCreatureAction_", 3, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerRemoteWeaponTransitionSubmitted",
                "animation_id=", 3, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerRemoteWeaponTransitionAnimationStarted",
                "native_action=CCreatureAction_", 3, state.timeoutSeconds) &&
            peers.WaitEventDetailCount(host, "MultiplayerRemoteWeaponTransitionApplied",
                "attempts=", 3, state.timeoutSeconds);
    }
}

CombatPhaseResult RunCombatAttackPhase(PeerHarness& peers, CombatScenarioState& state)
{
    if (!SubmitAttack(peers, state))
    {
        return CombatPhaseResult::Failed;
    }
    if (state.heroWill)
    {
        return CombatPhaseResult::Ready;
    }
    return CompleteMeleeHandoff(peers, state) && CompleteWeaponTransitions(peers, state)
        ? CombatPhaseResult::Ready
        : CombatPhaseResult::Failed;
}
}
