#pragma once
#include "CombatScenarioState.h"
namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunHeroWillPhase(PeerHarness &peers, CombatScenarioState &state);
}
