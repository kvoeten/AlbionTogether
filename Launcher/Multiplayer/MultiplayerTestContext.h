#pragma once

#include "../Runtime/GameProcess.h"

#include <cstdint>
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
    ManualRoster,
    MapStress,
    Save
};

struct MultiplayerTestContext final
{
    std::filesystem::path executable;
    std::filesystem::path clientDll;
    std::filesystem::path fixtureDocumentsSource;
    std::filesystem::path sessionRoot;
    std::wstring sessionId;
    std::wstring hostFixtureSave = L"AutoSave";
    std::wstring guestFixtureSave = L"AutoSave";
    unsigned short port = 38171;
    unsigned int timeoutSeconds = 120;
    std::uint32_t mapStressSeed = 0;
    unsigned int mapStressTransitions = 12;
    MultiplayerScenario scenario = MultiplayerScenario::Basic;
    std::vector<std::wstring> gameArguments;
};
}
