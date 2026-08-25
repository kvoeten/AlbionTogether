#pragma once
#include "CombatScenarioState.h"
namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunCombatVisualPhase(PeerHarness &peers, CombatScenarioState &state);
}
