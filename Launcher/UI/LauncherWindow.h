#pragma once

#include "../Configuration/LauncherSettings.h"
#include "../Diagnostics/LauncherDiagnostics.h"
#include "../Application/LauncherWorkflow.h"
#include "LauncherDiagnosticsTask.h"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::ui
{
    class LauncherWindow final
    {
    public:
        LauncherWindow();
        ~LauncherWindow();

        LauncherWindow(const LauncherWindow&) = delete;
        LauncherWindow& operator=(const LauncherWindow&) = delete;

        int Run(HINSTANCE instance);

    private:
        enum class Page
        {
            Play,
            Network,
            Settings,
        };

        static LRESULT CALLBACK WindowProcedure(
            HWND window,
            UINT message,
            WPARAM wparam,
            LPARAM lparam);
        LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

        bool Create(HINSTANCE instance);
        void CreateControls();
        void CreateFonts();
        void LayoutControls();
        void ShowPage(Page page);
        void Paint();
        void DrawButton(const DRAWITEMSTRUCT& item);
        void HandleCommand(int identifier, int notification);

        void BeginDiagnostics(bool offerRepair = false);
        void CompleteDiagnostics(
            diagnostics::LauncherDiagnosticsReport* report);
        void OfferFirewallRepair();
        void RepairFirewall();
        void Launch(bool host);
        void BrowseForGame();
        void SaveSettings();
        void OpenDiscord();
        void OpenLatestDiagnostics();
        void UpdateGameRunningState();
        void LoadControlsFromSettings();
        [[nodiscard]] bool ReadSettingsFromControls(
            LauncherSettings& settings,
            std::wstring& error) const;
        [[nodiscard]] std::filesystem::path ResolveGameExecutable() const;
        [[nodiscard]] std::wstring ControlText(HWND control) const;
        void SetControlText(HWND control, const std::wstring& value);
        void SetStatus(const std::wstring& value, COLORREF color);
        [[nodiscard]] int Scale(int value) const noexcept;

        HWND window_ = nullptr;
        HWND navPlay_ = nullptr;
        HWND navNetwork_ = nullptr;
        HWND navSettings_ = nullptr;

        HWND hostName_ = nullptr;
        HWND hostPort_ = nullptr;
        HWND hostButton_ = nullptr;
        HWND joinName_ = nullptr;
        HWND joinAddress_ = nullptr;
        HWND joinPort_ = nullptr;
        HWND joinButton_ = nullptr;
        HWND runTests_ = nullptr;

        HWND networkAddress_ = nullptr;
        HWND networkPort_ = nullptr;
        HWND networkTest_ = nullptr;
        HWND repairFirewall_ = nullptr;

        HWND gamePath_ = nullptr;
        HWND browseGame_ = nullptr;
        HWND showConsole_ = nullptr;
        HWND generateLogs_ = nullptr;
        HWND saveSettings_ = nullptr;
        HWND discord_ = nullptr;
        HWND openDiagnostics_ = nullptr;

        std::vector<HWND> playControls_;
        std::vector<HWND> networkControls_;
        std::vector<HWND> settingsControls_;

        HFONT titleFont_ = nullptr;
        HFONT headingFont_ = nullptr;
        HFONT bodyFont_ = nullptr;
        HFONT smallFont_ = nullptr;
        HBRUSH windowBrush_ = nullptr;
        HBRUSH panelBrush_ = nullptr;
        HBRUSH editBrush_ = nullptr;

        application::LauncherWorkflow workflow_;
        LauncherSettings settings_;
        diagnostics::LauncherDiagnosticsReport diagnostics_;
        std::wstring statusText_ = L"Checking your setup...";
        COLORREF statusColor_ = RGB(169, 172, 163);
        Page page_ = Page::Play;
        unsigned int dpi_ = 96;
        bool showConsoleChecked_ = true;
        bool generateLogsChecked_ = true;
        bool offerRepairAfterDiagnostics_ = false;
        bool gameRunning_ = false;
        LauncherDiagnosticsTask diagnosticsTask_;
    };

    int RunLauncherUi(HINSTANCE instance);
}
