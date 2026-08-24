#pragma once

#include <Windows.h>

namespace fable::launcher {
inline constexpr wchar_t kGameExecutableName[] = L"Fable Anniversary.exe";
inline constexpr wchar_t kClientDllName[] = L"FableTogether.Client.dll";
inline constexpr wchar_t kClientModeEnvironment[] =
    L"FABLETOGETHER_CLIENT_MODE";
inline constexpr wchar_t kScenarioEnvironment[] = L"FABLETOGETHER_SCENARIO";
inline constexpr wchar_t kRunIdEnvironment[] = L"FABLETOGETHER_RUN_ID";
inline constexpr wchar_t kEventPathEnvironment[] = L"FABLETOGETHER_EVENT_PATH";
inline constexpr wchar_t kLogPathEnvironment[] = L"FABLETOGETHER_LOG_PATH";
inline constexpr wchar_t kFixtureDocumentsEnvironment[] =
    L"FABLETOGETHER_FIXTURE_DOCUMENTS";
inline constexpr wchar_t kCharacterSnapshotEnvironment[] =
    L"FABLETOGETHER_CHARACTER_SNAPSHOT";
inline constexpr wchar_t kScriptDataEnvironment[] =
    L"FABLETOGETHER_SCRIPT_DATA";
inline constexpr wchar_t kLocalSessionEnvironment[] =
    L"FABLETOGETHER_LOCAL_SESSION";
inline constexpr wchar_t kLocalInstanceEnvironment[] =
    L"FABLETOGETHER_LOCAL_INSTANCE";
inline constexpr wchar_t kMultiplayerRoleEnvironment[] =
    L"FABLETOGETHER_MULTIPLAYER_ROLE";
inline constexpr wchar_t kMultiplayerAddressEnvironment[] =
    L"FABLETOGETHER_MULTIPLAYER_ADDRESS";
inline constexpr wchar_t kMultiplayerPortEnvironment[] =
    L"FABLETOGETHER_MULTIPLAYER_PORT";
inline constexpr wchar_t kMultiplayerPlayerIdEnvironment[] =
    L"FABLETOGETHER_MULTIPLAYER_PLAYER_ID";
inline constexpr wchar_t kMultiplayerAppearanceEnvironment[] =
    L"FABLETOGETHER_MULTIPLAYER_APPEARANCE";
inline constexpr wchar_t kGameDefinitionsEnvironment[] =
    L"FABLETOGETHER_GAME_DEFINITIONS";
inline constexpr wchar_t kManualPlaytestEnvironment[] =
    L"FABLETOGETHER_MANUAL_PLAYTEST";
inline constexpr wchar_t kHeroWillPillarOnlyEnvironment[] =
    L"FABLE_TOGETHER_HERO_WILL_PILLAR_ONLY";
inline constexpr wchar_t kRemoteHeroDefinition[] =
    L"CREATURE_HERO_RIVAL_GOOD_01";
inline constexpr wchar_t kShutdownEventPrefix[] =
    L"Local\\FableTogether.Shutdown.";
inline constexpr wchar_t kDevelopmentGameRoot[] =
    L"D:\\SteamLibrary\\steamapps\\common\\Fable Anniversary";
inline constexpr wchar_t kFableSteamAppId[] = L"288470";
inline constexpr DWORD kInjectionTimeoutMilliseconds = 15'000;
inline constexpr DWORD kRuntimeReadyTimeoutMilliseconds = 90'000;
inline constexpr DWORD kClientPreResumeReady = 0x0000F101;
inline constexpr DWORD kClientRuntimeReady = 0x0000F102;
inline constexpr int kLocalTestWindowWidth = 830;
inline constexpr int kLocalTestWindowHeight = 620;
inline constexpr int kLocalTestWindowPitch = 850;
} // namespace fable::launcher
