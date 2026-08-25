#include "ScenarioRunners.h"

#include "../../Automation/WindowControl.h"

#include <iostream>

namespace fable::launcher::multiplayer
{
int RunMultiplayerTest(const MultiplayerTestContext& context)
{
    MultiplayerTestContext normalizedContext = context;
    normalizedContext.gameArguments =
        fable::launcher::automation::LocalWindowArguments(
            normalizedContext.gameArguments);
    MultiplayerTestSession session(normalizedContext);
    if (!session.Prepare() || !session.StartCorePeers()) return 1;
    if (context.scenario == MultiplayerScenario::Roster ||
        context.scenario == MultiplayerScenario::ManualRoster)
    {
        if (!session.StartRosterPeers()) return 1;
    }
    if (!session.PositionWindows())
    {
        std::wcerr
            << L"Could not reapply the compact side-by-side layout after world load; continuing with the layout established at startup.\n";
    }
    switch (context.scenario)
    {
    case MultiplayerScenario::Basic: return RunBasicScenario(session);
    case MultiplayerScenario::Roster: return RunRosterScenario(session);
    case MultiplayerScenario::Manual: return RunManualScenario(session);
    case MultiplayerScenario::ManualCombat: return RunCombatScenario(session);
    case MultiplayerScenario::ManualRoster: return RunManualScenario(session);
    case MultiplayerScenario::Transition: return RunTransitionScenario(session);
    case MultiplayerScenario::Authority: return RunAuthorityScenario(session);
    case MultiplayerScenario::Combat: return RunCombatScenario(session);
    case MultiplayerScenario::HeroWill: return RunHeroWillScenario(session);
    }
    return 1;
}
}
