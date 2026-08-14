#include "RuntimeConfiguration.h"

#include <cwchar>

namespace
{
    constexpr wchar_t kClientModeEnvironment[] = L"FABLETOGETHER_CLIENT_MODE";
    constexpr wchar_t kScenarioEnvironment[] = L"FABLETOGETHER_SCENARIO";
    constexpr wchar_t kRunIdEnvironment[] = L"FABLETOGETHER_RUN_ID";
    constexpr wchar_t kEventPathEnvironment[] = L"FABLETOGETHER_EVENT_PATH";
    constexpr wchar_t kLogPathEnvironment[] = L"FABLETOGETHER_LOG_PATH";
    constexpr wchar_t kFixtureDocumentsEnvironment[] =
        L"FABLETOGETHER_FIXTURE_DOCUMENTS";
    constexpr wchar_t kCharacterSnapshotEnvironment[] =
        L"FABLETOGETHER_CHARACTER_SNAPSHOT";
    constexpr wchar_t kScriptDataEnvironment[] = L"FABLETOGETHER_SCRIPT_DATA";
    constexpr wchar_t kGameDefinitionsEnvironment[] =
        L"FABLETOGETHER_GAME_DEFINITIONS";
    constexpr wchar_t kLocalSessionEnvironment[] =
        L"FABLETOGETHER_LOCAL_SESSION";
    constexpr wchar_t kLocalInstanceEnvironment[] =
        L"FABLETOGETHER_LOCAL_INSTANCE";
    constexpr wchar_t kMultiplayerRoleEnvironment[] =
        L"FABLETOGETHER_MULTIPLAYER_ROLE";
    constexpr wchar_t kMultiplayerAddressEnvironment[] =
        L"FABLETOGETHER_MULTIPLAYER_ADDRESS";
    constexpr wchar_t kMultiplayerPortEnvironment[] =
        L"FABLETOGETHER_MULTIPLAYER_PORT";
    constexpr wchar_t kMultiplayerPlayerIdEnvironment[] =
        L"FABLETOGETHER_MULTIPLAYER_PLAYER_ID";
    constexpr wchar_t kMultiplayerAppearanceEnvironment[] =
        L"FABLETOGETHER_MULTIPLAYER_APPEARANCE";
    constexpr wchar_t kMorphSelfTestEnvironment[] =
        L"FABLETOGETHER_MORPH_SELF_TEST";
    constexpr wchar_t kShutdownEventPrefix[] = L"Local\\FableTogether.Shutdown.";

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
        fixtureDocumentsPath_ = ReadEnvironment(kFixtureDocumentsEnvironment);
        characterSnapshotPath_ = ReadEnvironment(kCharacterSnapshotEnvironment);
        scriptDataPath_ = ReadEnvironment(kScriptDataEnvironment);
        gameDefinitionsPath_ = ReadEnvironment(kGameDefinitionsEnvironment);
        localSessionId_ = ReadEnvironment(kLocalSessionEnvironment);
        localInstanceId_ = ReadEnvironment(kLocalInstanceEnvironment);
        multiplayerRole_ = ReadEnvironment(kMultiplayerRoleEnvironment);
        multiplayerAddress_ = ReadEnvironment(kMultiplayerAddressEnvironment);
        multiplayerPlayerId_ = ReadEnvironment(kMultiplayerPlayerIdEnvironment);
        multiplayerAppearance_ = ReadEnvironment(kMultiplayerAppearanceEnvironment);
        morphSelfTest_ = ReadEnvironment(kMorphSelfTestEnvironment) == L"1";
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

    const std::wstring& RuntimeConfiguration::GameDefinitionsPath() const noexcept
    {
        return gameDefinitionsPath_;
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

    bool RuntimeConfiguration::MorphSelfTest() const noexcept
    {
        return morphSelfTest_;
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
            ScenarioIs(L"multiplayer_host_transition") ||
            ScenarioIs(L"multiplayer_guest") ||
            ScenarioIs(L"multiplayer_guest_transition");
    }

    bool RuntimeConfiguration::LoadsFixture() const noexcept
    {
        return ScenarioIs(L"load_fixture") ||
            ScenarioIs(L"appearance_cycle") ||
            ScenarioIs(L"multiplayer_host") ||
            ScenarioIs(L"multiplayer_host_transition") ||
            ScenarioIs(L"multiplayer_guest") ||
            ScenarioIs(L"multiplayer_guest_transition");
    }
}
