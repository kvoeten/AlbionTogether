#pragma once

#include "../Runtime/GameProcess.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::multiplayer
{
enum class MultiplayerScenario
{
    Basic,
    Roster,
    Manual,
    ManualCombat,
    Transition,
    Authority,
    Combat,
    HeroWill,
    ManualRoster
};

struct MultiplayerTestContext final
{
    std::filesystem::path executable;
    std::filesystem::path clientDll;
    std::filesystem::path fixtureDocumentsSource;
    std::filesystem::path sessionRoot;
    std::wstring sessionId;
    unsigned short port = 38171;
    unsigned int timeoutSeconds = 120;
    MultiplayerScenario scenario = MultiplayerScenario::Basic;
    std::vector<std::wstring> gameArguments;
};
}
