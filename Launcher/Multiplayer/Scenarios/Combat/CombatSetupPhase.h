#pragma once

#include "CombatScenarioState.h"

namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunCombatSetupPhase(PeerHarness &peers, CombatScenarioState &state, bool interactive, bool heroWill,
                                      unsigned int timeoutSeconds);
}
