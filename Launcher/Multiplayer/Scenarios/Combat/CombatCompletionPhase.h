#pragma once
#include "CombatScenarioState.h"
namespace fable::launcher::multiplayer::combat
{
int CompleteCombatScenario(PeerHarness &peers, CombatScenarioState &state, bool heroWill);
}
