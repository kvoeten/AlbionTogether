#pragma once

#include "CombatScenarioState.h"

namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunCombatAttackPhase(PeerHarness &peers, CombatScenarioState &state);
}
