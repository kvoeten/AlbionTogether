#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "LauncherConstants.h"

namespace fable::launcher
{
namespace fs = std::filesystem;

struct Options
{
    fs::path executable;
    fs::path gameDirectory;
    fs::path clientDll;
    fs::path fixtureDocuments;
    fs::path characterSnapshot;
    std::vector<std::wstring> gameArguments;
    std::wstring automationScenario;
    std::wstring localSession;
    std::wstring localInstance;
    std::wstring multiplayerRole;
    std::wstring multiplayerAddress;
    std::wstring multiplayerPlayerId;
    std::wstring multiplayerAppearance;
    unsigned short multiplayerPort = 38171;
    unsigned int automationTimeoutSeconds = 120;
    unsigned int dualInstanceHoldSeconds = 10;
    bool transformationProbe = false;
    bool dualInstanceTest = false;
    bool multiplayerTest = false;
    bool multiplayerRosterTest = false;
    bool multiplayerTransitionTest = false;
    bool multiplayerAuthorityTest = false;
    bool multiplayerCombatTest = false;
    bool multiplayerHeroWillTest = false;
    bool multiplayerPlaytest = false;
    bool showConsole = true;
    bool generateLogs = true;
    bool dryRun = false;
    bool showHelp = false;
};

bool ParseOptions(int argc, wchar_t** argv, Options& options, std::wstring& error);
void PrintUsage();
}
