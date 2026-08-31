#pragma once

#include <Windows.h>

namespace fable::launcher {
inline constexpr wchar_t kGameExecutableName[] = L"Fable Anniversary.exe";
inline constexpr wchar_t kClientDllName[] = L"AlbionTogether.Client.dll";
inline constexpr wchar_t kLauncherVersion[] = L"v0.1.0-alpha.13";
inline constexpr wchar_t kDiscordUrl[] = L"https://discord.gg/5JSKmjKd85";
inline constexpr wchar_t kClientModeEnvironment[] =
    L"ALBIONTOGETHER_CLIENT_MODE";
inline constexpr wchar_t kScenarioEnvironment[] = L"ALBIONTOGETHER_SCENARIO";
inline constexpr wchar_t kRunIdEnvironment[] = L"ALBIONTOGETHER_RUN_ID";
inline constexpr wchar_t kEventPathEnvironment[] = L"ALBIONTOGETHER_EVENT_PATH";
inline constexpr wchar_t kLogPathEnvironment[] = L"ALBIONTOGETHER_LOG_PATH";
inline constexpr wchar_t kConsoleEnabledEnvironment[] =
    L"ALBIONTOGETHER_CONSOLE_ENABLED";
inline constexpr wchar_t kLogFilesEnabledEnvironment[] =
    L"ALBIONTOGETHER_LOG_FILES_ENABLED";
inline constexpr wchar_t kFixtureDocumentsEnvironment[] =
    L"ALBIONTOGETHER_FIXTURE_DOCUMENTS";
inline constexpr wchar_t kCharacterSnapshotEnvironment[] =
    L"ALBIONTOGETHER_CHARACTER_SNAPSHOT";
inline constexpr wchar_t kScriptDataEnvironment[] =
    L"ALBIONTOGETHER_SCRIPT_DATA";
inline constexpr wchar_t kLocalSessionEnvironment[] =
    L"ALBIONTOGETHER_LOCAL_SESSION";
inline constexpr wchar_t kLocalInstanceEnvironment[] =
    L"ALBIONTOGETHER_LOCAL_INSTANCE";
inline constexpr wchar_t kMultiplayerRoleEnvironment[] =
    L"ALBIONTOGETHER_MULTIPLAYER_ROLE";
inline constexpr wchar_t kMultiplayerAddressEnvironment[] =
    L"ALBIONTOGETHER_MULTIPLAYER_ADDRESS";
inline constexpr wchar_t kMultiplayerPortEnvironment[] =
    L"ALBIONTOGETHER_MULTIPLAYER_PORT";
inline constexpr wchar_t kMultiplayerPlayerIdEnvironment[] =
    L"ALBIONTOGETHER_MULTIPLAYER_PLAYER_ID";
inline constexpr wchar_t kMultiplayerAppearanceEnvironment[] =
    L"ALBIONTOGETHER_MULTIPLAYER_APPEARANCE";
inline constexpr wchar_t kMapStressSeedEnvironment[] =
    L"ALBIONTOGETHER_MAP_STRESS_SEED";
inline constexpr wchar_t kMapStressTransitionsEnvironment[] =
    L"ALBIONTOGETHER_MAP_STRESS_TRANSITIONS";
inline constexpr wchar_t kManualPlaytestEnvironment[] =
    L"ALBIONTOGETHER_MANUAL_PLAYTEST";
inline constexpr wchar_t kHeroWillPillarOnlyEnvironment[] =
    L"ALBION_TOGETHER_HERO_WILL_PILLAR_ONLY";
inline constexpr wchar_t kRemoteHeroDefinition[] =
    L"CREATURE_HERO_RIVAL_GOOD_01";
inline constexpr wchar_t kShutdownEventPrefix[] =
    L"Local\\AlbionTogether.Shutdown.";
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
