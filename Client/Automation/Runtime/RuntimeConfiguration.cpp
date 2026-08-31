#include "RuntimeConfiguration.h"

#include <cwchar>

namespace
{
    constexpr wchar_t kClientModeEnvironment[] = L"ALBIONTOGETHER_CLIENT_MODE";
    constexpr wchar_t kScenarioEnvironment[] = L"ALBIONTOGETHER_SCENARIO";
    constexpr wchar_t kRunIdEnvironment[] = L"ALBIONTOGETHER_RUN_ID";
    constexpr wchar_t kEventPathEnvironment[] = L"ALBIONTOGETHER_EVENT_PATH";
    constexpr wchar_t kLogPathEnvironment[] = L"ALBIONTOGETHER_LOG_PATH";
    constexpr wchar_t kConsoleEnabledEnvironment[] =
        L"ALBIONTOGETHER_CONSOLE_ENABLED";
    constexpr wchar_t kLogFilesEnabledEnvironment[] =
        L"ALBIONTOGETHER_LOG_FILES_ENABLED";
    constexpr wchar_t kFixtureDocumentsEnvironment[] =
        L"ALBIONTOGETHER_FIXTURE_DOCUMENTS";
    constexpr wchar_t kCharacterSnapshotEnvironment[] =
        L"ALBIONTOGETHER_CHARACTER_SNAPSHOT";
    constexpr wchar_t kScriptDataEnvironment[] = L"ALBIONTOGETHER_SCRIPT_DATA";
    constexpr wchar_t kLocalSessionEnvironment[] =
        L"ALBIONTOGETHER_LOCAL_SESSION";
    constexpr wchar_t kLocalInstanceEnvironment[] =
        L"ALBIONTOGETHER_LOCAL_INSTANCE";
    constexpr wchar_t kMultiplayerRoleEnvironment[] =
        L"ALBIONTOGETHER_MULTIPLAYER_ROLE";
    constexpr wchar_t kMultiplayerAddressEnvironment[] =
        L"ALBIONTOGETHER_MULTIPLAYER_ADDRESS";
    constexpr wchar_t kMultiplayerPortEnvironment[] =
        L"ALBIONTOGETHER_MULTIPLAYER_PORT";
    constexpr wchar_t kMultiplayerPlayerIdEnvironment[] =
        L"ALBIONTOGETHER_MULTIPLAYER_PLAYER_ID";
    constexpr wchar_t kMultiplayerAppearanceEnvironment[] =
        L"ALBIONTOGETHER_MULTIPLAYER_APPEARANCE";
    constexpr wchar_t kMorphSelfTestEnvironment[] =
        L"ALBIONTOGETHER_MORPH_SELF_TEST";
    constexpr wchar_t kManualPlaytestEnvironment[] =
        L"ALBIONTOGETHER_MANUAL_PLAYTEST";
    constexpr wchar_t kMapStressSeedEnvironment[] =
        L"ALBIONTOGETHER_MAP_STRESS_SEED";
    constexpr wchar_t kMapStressTransitionsEnvironment[] =
        L"ALBIONTOGETHER_MAP_STRESS_TRANSITIONS";
    constexpr wchar_t kShutdownEventPrefix[] = L"Local\\AlbionTogether.Shutdown.";

    std::wstring ReadEnvironment(const wchar_t* name)
    {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0)
        {
            return {};
        }

        std::wstring value(static_cast<std::size_t>(required), L'\0');
        const DWORD copied = GetEnvironmentVariableW(
            name,
            value.data(),
            required);
        if (copied == 0 || copied >= required)
        {
            return {};
        }
        value.resize(static_cast<std::size_t>(copied));
        return value;
    }

    unsigned long ReadUnsignedEnvironment(const wchar_t* name)
    {
        const std::wstring value = ReadEnvironment(name);
        if (value.empty())
        {
            return 0;
        }
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
        return end != value.c_str() && *end == L'\0' ? parsed : 0;
    }
}

namespace fable::automation::runtime
{
    void RuntimeConfiguration::LoadFromEnvironment()
    {
        const std::wstring mode = ReadEnvironment(kClientModeEnvironment);
        if (mode == L"transform_probe")
        {
            mode_ = ClientMode::TransformProbe;
        }
        else if (mode == L"appearance_cycle")
        {
            mode_ = ClientMode::AppearanceCycle;
        }
        else
        {
            mode_ = ClientMode::Observe;
        }

        runId_ = ReadEnvironment(kRunIdEnvironment);
        scenario_ = ReadEnvironment(kScenarioEnvironment);
        eventPath_ = ReadEnvironment(kEventPathEnvironment);
        logPath_ = ReadEnvironment(kLogPathEnvironment);
        showConsole_ = ReadEnvironment(kConsoleEnabledEnvironment) != L"0";
        generateLogFiles_ =
            ReadEnvironment(kLogFilesEnabledEnvironment) != L"0";
        fixtureDocumentsPath_ = ReadEnvironment(kFixtureDocumentsEnvironment);
        characterSnapshotPath_ = ReadEnvironment(kCharacterSnapshotEnvironment);
        scriptDataPath_ = ReadEnvironment(kScriptDataEnvironment);
        localSessionId_ = ReadEnvironment(kLocalSessionEnvironment);
        localInstanceId_ = ReadEnvironment(kLocalInstanceEnvironment);
        multiplayerRole_ = ReadEnvironment(kMultiplayerRoleEnvironment);
        multiplayerAddress_ = ReadEnvironment(kMultiplayerAddressEnvironment);
        multiplayerPlayerId_ = ReadEnvironment(kMultiplayerPlayerIdEnvironment);
        multiplayerAppearance_ = ReadEnvironment(kMultiplayerAppearanceEnvironment);
        mapStressSeed_ = static_cast<std::uint32_t>(
            ReadUnsignedEnvironment(kMapStressSeedEnvironment));
        mapStressTransitions_ = static_cast<unsigned int>(
            ReadUnsignedEnvironment(kMapStressTransitionsEnvironment));
        morphSelfTest_ = ReadEnvironment(kMorphSelfTestEnvironment) == L"1";
        manualPlaytest_ =
            ReadEnvironment(kManualPlaytestEnvironment) == L"1";
        multiplayerPort_ = 0;
        const std::wstring port = ReadEnvironment(kMultiplayerPortEnvironment);
        if (!port.empty())
        {
            wchar_t* end = nullptr;
            const unsigned long value = std::wcstoul(port.c_str(), &end, 10);
            if (end != port.c_str() && *end == L'\0' && value <= 65'535)
            {
                multiplayerPort_ = static_cast<unsigned short>(value);
            }
        }

        if (shutdownEvent_ != nullptr)
        {
            CloseHandle(shutdownEvent_);
            shutdownEvent_ = nullptr;
        }
        if (!scenario_.empty() && !runId_.empty())
        {
            const std::wstring eventName = kShutdownEventPrefix + runId_;
            shutdownEvent_ = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
        }
    }

    ClientMode RuntimeConfiguration::Mode() const noexcept
    {
        return mode_;
    }

    const std::wstring& RuntimeConfiguration::RunId() const noexcept
    {
        return runId_;
    }

    const std::wstring& RuntimeConfiguration::Scenario() const noexcept
    {
        return scenario_;
    }

    const std::wstring& RuntimeConfiguration::EventPath() const noexcept
    {
        return eventPath_;
    }

    const std::wstring& RuntimeConfiguration::LogPath() const noexcept
    {
        return logPath_;
    }

    const std::wstring& RuntimeConfiguration::FixtureDocumentsPath() const noexcept
    {
        return fixtureDocumentsPath_;
    }

    const std::wstring& RuntimeConfiguration::CharacterSnapshotPath() const noexcept
    {
        return characterSnapshotPath_;
    }

    const std::wstring& RuntimeConfiguration::ScriptDataPath() const noexcept
    {
        return scriptDataPath_;
    }

    const std::wstring& RuntimeConfiguration::LocalSessionId() const noexcept
    {
        return localSessionId_;
    }

    const std::wstring& RuntimeConfiguration::LocalInstanceId() const noexcept
    {
        return localInstanceId_;
    }

    const std::wstring& RuntimeConfiguration::MultiplayerRole() const noexcept
    {
        return multiplayerRole_;
    }

    const std::wstring& RuntimeConfiguration::MultiplayerAddress() const noexcept
    {
        return multiplayerAddress_;
    }

    const std::wstring& RuntimeConfiguration::MultiplayerPlayerId() const noexcept
    {
        return multiplayerPlayerId_;
    }

    const std::wstring& RuntimeConfiguration::MultiplayerAppearance() const noexcept
    {
        return multiplayerAppearance_;
    }

    unsigned short RuntimeConfiguration::MultiplayerPort() const noexcept
    {
        return multiplayerPort_;
    }

    std::uint32_t RuntimeConfiguration::MapStressSeed() const noexcept
    {
        return mapStressSeed_;
    }

    unsigned int RuntimeConfiguration::MapStressTransitions() const noexcept
    {
        return mapStressTransitions_;
    }

    bool RuntimeConfiguration::MorphSelfTest() const noexcept
    {
        return morphSelfTest_;
    }

    bool RuntimeConfiguration::ManualPlaytest() const noexcept
    {
        return manualPlaytest_;
    }

    bool RuntimeConfiguration::ShowConsole() const noexcept
    {
        return showConsole_;
    }

    bool RuntimeConfiguration::GenerateLogFiles() const noexcept
    {
        return generateLogFiles_;
    }

    HANDLE RuntimeConfiguration::ShutdownEvent() const noexcept
    {
        return shutdownEvent_;
    }

    bool RuntimeConfiguration::IsLocalInstance() const noexcept
    {
        return !localSessionId_.empty() && !localInstanceId_.empty();
    }

    bool RuntimeConfiguration::MultiplayerEnabled() const noexcept
    {
        return (multiplayerRole_ == L"host" || multiplayerRole_ == L"guest") &&
            multiplayerPort_ != 0 && !multiplayerPlayerId_.empty() &&
            !multiplayerAppearance_.empty();
    }

    bool RuntimeConfiguration::ScenarioIs(const wchar_t* value) const noexcept
    {
        return value != nullptr && scenario_ == value;
    }

    bool RuntimeConfiguration::UsesFrontEndStartAutomation() const noexcept
    {
        return ScenarioIs(L"observe_frontend") ||
            ScenarioIs(L"observe_save_list") ||
            ScenarioIs(L"bootstrap_fixture_probe") ||
            ScenarioIs(L"load_fixture") ||
            ScenarioIs(L"appearance_cycle") ||
            ScenarioIs(L"multiplayer_host") ||
            ScenarioIs(L"multiplayer_host_combat") ||
            ScenarioIs(L"multiplayer_host_hero_will") ||
            ScenarioIs(L"multiplayer_host_authority") ||
            ScenarioIs(L"multiplayer_host_transition") ||
            ScenarioIs(L"multiplayer_host_map_stress") ||
            ScenarioIs(L"multiplayer_host_save") ||
            ScenarioIs(L"multiplayer_guest") ||
            ScenarioIs(L"multiplayer_guest_combat") ||
            ScenarioIs(L"multiplayer_guest_hero_will") ||
            ScenarioIs(L"multiplayer_guest_authority") ||
            ScenarioIs(L"multiplayer_guest_transition") ||
            ScenarioIs(L"multiplayer_guest_map_stress") ||
            ScenarioIs(L"multiplayer_guest_save");
    }

    bool RuntimeConfiguration::LoadsFixture() const noexcept
    {
        return ScenarioIs(L"load_fixture") ||
            ScenarioIs(L"appearance_cycle") ||
            ScenarioIs(L"multiplayer_host") ||
            ScenarioIs(L"multiplayer_host_combat") ||
            ScenarioIs(L"multiplayer_host_hero_will") ||
            ScenarioIs(L"multiplayer_host_authority") ||
            ScenarioIs(L"multiplayer_host_transition") ||
            ScenarioIs(L"multiplayer_host_map_stress") ||
            ScenarioIs(L"multiplayer_host_save") ||
            ScenarioIs(L"multiplayer_guest") ||
            ScenarioIs(L"multiplayer_guest_combat") ||
            ScenarioIs(L"multiplayer_guest_hero_will") ||
            ScenarioIs(L"multiplayer_guest_authority") ||
            ScenarioIs(L"multiplayer_guest_transition") ||
            ScenarioIs(L"multiplayer_guest_map_stress") ||
            ScenarioIs(L"multiplayer_guest_save");
    }
}
