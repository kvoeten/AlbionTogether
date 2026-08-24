#include "HeroWillPhase.h"

#include "../../../Configuration/LauncherConstants.h"

namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunHeroWillPhase(PeerHarness &peers, CombatScenarioState &state)
{
    if (!state.heroWill)
        return CombatPhaseResult::Ready;
    wchar_t value[8] = {};
    const DWORD length =
        GetEnvironmentVariableW(kHeroWillPillarOnlyEnvironment, value, static_cast<DWORD>(std::size(value)));
    const bool pillarOnly = length != 0 && length < std::size(value) && value[0] == L'1';
    const char *completion =
        pillarOnly ? "accepted=2 expected_unsupported=0 total=2" : "accepted=17 expected_unsupported=2 total=19";
    const std::size_t expected = pillarOnly ? 2 : 17;
    Peer &host = *state.host;
    Peer &guest = *state.guest;
    const bool complete = peers.WaitEventDetail(host, "MultiplayerHeroWillSequenceComplete", completion) &&
                          peers.WaitEventDetail(guest, "MultiplayerHeroWillSequenceComplete", completion) &&
                          peers.WaitEventDetailCount(host, "MultiplayerLocalHeroAbilityCaptured", "command=1", expected,
                                                     state.timeoutSeconds) &&
                          peers.WaitEventDetailCount(guest, "MultiplayerLocalHeroAbilityCaptured", "command=1",
                                                     expected, state.timeoutSeconds) &&
                          peers.WaitEventDetailCount(host, "MultiplayerRemoteHeroAbilityReplayed", "command=1",
                                                     expected, state.timeoutSeconds) &&
                          peers.WaitEventDetailCount(guest, "MultiplayerRemoteHeroAbilityReplayed", "command=1",
                                                     expected, state.timeoutSeconds);
    if (complete)
        Sleep(2'000);
    return complete ? CombatPhaseResult::Ready : CombatPhaseResult::Failed;
}
} // namespace fable::launcher::multiplayer::combat
