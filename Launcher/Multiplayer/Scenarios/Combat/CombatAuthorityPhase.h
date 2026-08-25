#pragma once
#include "CombatScenarioState.h"
namespace fable::launcher::multiplayer::combat
{
CombatPhaseResult RunCombatAuthorityPhase(PeerHarness &peers, CombatScenarioState &state);
}
