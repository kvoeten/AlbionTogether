#include "SingleGameLaunch.h"

#include "../Automation/AutomationRunner.h"
#include "../Automation/WindowControl.h"
#include "../Configuration/LauncherConstants.h"
#include "../Runtime/GameProcess.h"

#include <iostream>

namespace fable::launcher::application
{
    int RunSingleGame(const LaunchPlan& plan)
    {
        runtime::GameLaunchSpec spec;
        spec.executable = plan.executable;
        spec.clientDll = plan.clientDll;
        spec.clientLog = plan.clientLog;
        spec.eventPath = plan.eventPath;
        spec.fixtureDocuments = plan.fixtureDocuments;
        spec.characterSnapshot = plan.characterSnapshot;
        spec.scriptData = plan.scriptData;
        spec.clientMode = plan.clientMode;
        spec.scenario = plan.options.automationScenario;
        spec.runId = plan.runId;
        spec.localSession = plan.localSession;
        spec.localInstance = plan.options.localInstance;
        spec.multiplayerRole = plan.options.multiplayerRole;
        spec.multiplayerAddress = plan.options.multiplayerAddress;
        spec.multiplayerPort = plan.options.multiplayerPort;
        spec.multiplayerPlayerId = plan.options.multiplayerPlayerId;
        spec.multiplayerAppearance = plan.options.multiplayerAppearance;
        spec.arguments = BuildGameArguments(plan);

        runtime::LaunchedGame launched;
        if (!runtime::SpawnGame(spec, launched))
        {
            return 1;
        }
        if (!plan.options.automationScenario.empty())
        {
            return automation::RunAutomation(
                launched,
                plan.eventPath,
                plan.options.automationScenario,
                plan.options.automationTimeoutSeconds,
                !plan.characterSnapshot.empty());
        }
        if (!plan.options.localInstance.empty())
        {
            const int x = plan.options.localInstance == L"host"
                ? 0
                : fable::launcher::kLocalTestWindowPitch;
            if (!automation::WaitForLocalInstanceReady(
                    launched,
                    plan.eventPath,
                    plan.options.localInstance.c_str(),
                    x,
                    plan.options.automationTimeoutSeconds))
            {
                runtime::CloseCreatedProcess(
                    launched.process.get(), launched.processId, nullptr);
                return 1;
            }
            std::wcout << L"Local " << plan.options.localInstance
                       << L" is ready in a compact test window; launcher is leaving PID "
                       << launched.processId << L" running.\n";
        }
        return 0;
    }
}
