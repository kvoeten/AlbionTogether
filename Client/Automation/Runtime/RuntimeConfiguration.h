#pragma once

#include <Windows.h>

#include <cstdint>
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
        [[nodiscard]] const std::wstring& LogPath() const noexcept;
        [[nodiscard]] const std::wstring& FixtureDocumentsPath() const noexcept;
        [[nodiscard]] const std::wstring& CharacterSnapshotPath() const noexcept;
        [[nodiscard]] const std::wstring& ScriptDataPath() const noexcept;
        [[nodiscard]] const std::wstring& LocalSessionId() const noexcept;
        [[nodiscard]] const std::wstring& LocalInstanceId() const noexcept;
        [[nodiscard]] const std::wstring& MultiplayerRole() const noexcept;
        [[nodiscard]] const std::wstring& MultiplayerAddress() const noexcept;
        [[nodiscard]] const std::wstring& MultiplayerPlayerId() const noexcept;
        [[nodiscard]] const std::wstring& MultiplayerAppearance() const noexcept;
        [[nodiscard]] unsigned short MultiplayerPort() const noexcept;
        [[nodiscard]] std::uint32_t MapStressSeed() const noexcept;
        [[nodiscard]] unsigned int MapStressTransitions() const noexcept;
        [[nodiscard]] bool MorphSelfTest() const noexcept;
        [[nodiscard]] bool ManualPlaytest() const noexcept;
        [[nodiscard]] bool ShowConsole() const noexcept;
        [[nodiscard]] bool GenerateLogFiles() const noexcept;
        [[nodiscard]] HANDLE ShutdownEvent() const noexcept;

        [[nodiscard]] bool IsLocalInstance() const noexcept;
        [[nodiscard]] bool MultiplayerEnabled() const noexcept;
        [[nodiscard]] bool ScenarioIs(const wchar_t* value) const noexcept;
        [[nodiscard]] bool UsesFrontEndStartAutomation() const noexcept;
        [[nodiscard]] bool LoadsFixture() const noexcept;

    private:
        ClientMode mode_ = ClientMode::Observe;
        std::wstring runId_;
        std::wstring scenario_;
        std::wstring eventPath_;
        std::wstring logPath_;
        std::wstring fixtureDocumentsPath_;
        std::wstring characterSnapshotPath_;
        std::wstring scriptDataPath_;
        std::wstring localSessionId_;
        std::wstring localInstanceId_;
        std::wstring multiplayerRole_;
        std::wstring multiplayerAddress_;
        std::wstring multiplayerPlayerId_;
        std::wstring multiplayerAppearance_;
        unsigned short multiplayerPort_ = 0;
        std::uint32_t mapStressSeed_ = 0;
        unsigned int mapStressTransitions_ = 0;
        bool morphSelfTest_ = false;
        bool manualPlaytest_ = false;
        bool showConsole_ = true;
        bool generateLogFiles_ = true;
        HANDLE shutdownEvent_ = nullptr;
    };
}
