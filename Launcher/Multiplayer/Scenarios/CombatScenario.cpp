#include "ScenarioRunners.h"

#include "Combat/CombatAttackPhase.h"
#include "Combat/CombatAuthorityPhase.h"
#include "Combat/CombatCompletionPhase.h"
#include "Combat/CombatScenarioState.h"
#include "Combat/CombatSetupPhase.h"
#include "Combat/CombatVisualPhase.h"
#include "Combat/HeroWillPhase.h"

namespace fable::launcher::multiplayer
{
namespace
{
    using combat::CombatPhaseResult;
    using combat::CombatScenarioState;

    bool IsReady(CombatPhaseResult result)
    {
        return result == CombatPhaseResult::Ready;
    }

    bool IsManual(CombatPhaseResult result)
    {
        return result == CombatPhaseResult::ManualLeaveRunning;
    }

    int RunPhases(MultiplayerTestSession& session, bool heroWill)
    {
        PeerHarness& peers = session.peers();
        CombatScenarioState state;
        const bool interactive = session.context().scenario == MultiplayerScenario::ManualCombat;
        CombatPhaseResult result = combat::RunCombatSetupPhase(
            peers,
            state,
            interactive,
            heroWill,
            session.context().timeoutSeconds);
        if (IsManual(result))
        {
            session.LeaveRunning();
            return 0;
        }
        if (!IsReady(result))
        {
            return 1;
        }
        result = combat::RunCombatAttackPhase(peers, state);
        if (!IsReady(result))
        {
            return 1;
        }
        result = combat::RunCombatAuthorityPhase(peers, state);
        if (!IsReady(result))
        {
            return 1;
        }
        result = combat::RunCombatVisualPhase(peers, state);
        if (!IsReady(result)) return 1;
        result = combat::RunHeroWillPhase(peers, state);
        if (!IsReady(result)) return 1;
        return combat::CompleteCombatScenario(peers, state, heroWill);
    }
}

int RunCombatScenario(MultiplayerTestSession& session)
{
    return RunPhases(session, false);
}

int RunHeroWillScenario(MultiplayerTestSession& session)
{
    return RunPhases(session, true);
}
}
