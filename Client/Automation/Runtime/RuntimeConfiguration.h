#pragma once

#include <Windows.h>

#include <string>

namespace fable::automation::runtime
{
    enum class ClientMode
    {
        Observe,
        AppearanceCycle,
        TransformProbe,
    };

    class RuntimeConfiguration final
    {
    public:
        void LoadFromEnvironment();

        [[nodiscard]] ClientMode Mode() const noexcept;
        [[nodiscard]] const std::wstring& RunId() const noexcept;
        [[nodiscard]] const std::wstring& Scenario() const noexcept;
        [[nodiscard]] const std::wstring& EventPath() const noexcept;
        [[nodiscard]] const std::wstring& FixtureDocumentsPath() const noexcept;
        [[nodiscard]] const std::wstring& CharacterSnapshotPath() const noexcept;
        [[nodiscard]] HANDLE ShutdownEvent() const noexcept;

        [[nodiscard]] bool ScenarioIs(const wchar_t* value) const noexcept;
        [[nodiscard]] bool UsesFrontEndStartAutomation() const noexcept;
        [[nodiscard]] bool LoadsFixture() const noexcept;

    private:
        ClientMode mode_ = ClientMode::Observe;
        std::wstring runId_;
        std::wstring scenario_;
        std::wstring eventPath_;
        std::wstring fixtureDocumentsPath_;
        std::wstring characterSnapshotPath_;
        HANDLE shutdownEvent_ = nullptr;
    };
}
