#include "RuntimeConfiguration.h"

#include <cwchar>

namespace
{
    constexpr wchar_t kClientModeEnvironment[] = L"FABLETOGETHER_CLIENT_MODE";
    constexpr wchar_t kScenarioEnvironment[] = L"FABLETOGETHER_SCENARIO";
    constexpr wchar_t kRunIdEnvironment[] = L"FABLETOGETHER_RUN_ID";
    constexpr wchar_t kEventPathEnvironment[] = L"FABLETOGETHER_EVENT_PATH";
    constexpr wchar_t kFixtureDocumentsEnvironment[] =
        L"FABLETOGETHER_FIXTURE_DOCUMENTS";
    constexpr wchar_t kCharacterSnapshotEnvironment[] =
        L"FABLETOGETHER_CHARACTER_SNAPSHOT";
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
        fixtureDocumentsPath_ = ReadEnvironment(kFixtureDocumentsEnvironment);
        characterSnapshotPath_ = ReadEnvironment(kCharacterSnapshotEnvironment);

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

    const std::wstring& RuntimeConfiguration::FixtureDocumentsPath() const noexcept
    {
        return fixtureDocumentsPath_;
    }

    const std::wstring& RuntimeConfiguration::CharacterSnapshotPath() const noexcept
    {
        return characterSnapshotPath_;
    }

    HANDLE RuntimeConfiguration::ShutdownEvent() const noexcept
    {
        return shutdownEvent_;
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
            ScenarioIs(L"appearance_cycle");
    }

    bool RuntimeConfiguration::LoadsFixture() const noexcept
    {
        return ScenarioIs(L"load_fixture") || ScenarioIs(L"appearance_cycle");
    }
}
