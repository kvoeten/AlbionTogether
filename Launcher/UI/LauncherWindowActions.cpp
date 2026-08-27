#include "LauncherWindow.h"

#include "LauncherControlIds.h"
#include "LauncherTheme.h"
#include "../Configuration/LauncherConstants.h"
#include "../Platform/FolderPicker.h"
#include "../Runtime/GameProcess.h"

#include <Shellapi.h>

#include <memory>

#pragma comment(lib, "shell32.lib")

namespace fable::launcher::ui
{
    namespace
    {
        constexpr UINT DiagnosticsCompleteMessage = WM_APP + 1;
    }

    void LauncherWindow::HandleCommand(
        const int identifier,
        const int notification)
    {
        if (notification == EN_CHANGE)
        {
            if (identifier == control_id::HostName ||
                identifier == control_id::JoinName)
            {
                HWND source = identifier == control_id::HostName
                    ? hostName_ : joinName_;
                HWND target = identifier == control_id::HostName
                    ? joinName_ : hostName_;
                const std::wstring text = ControlText(source);
                if (ControlText(target) != text) SetControlText(target, text);
            }
            else if (identifier == control_id::JoinAddress ||
                identifier == control_id::NetworkAddress)
            {
                HWND source = identifier == control_id::JoinAddress
                    ? joinAddress_ : networkAddress_;
                HWND target = identifier == control_id::JoinAddress
                    ? networkAddress_ : joinAddress_;
                const std::wstring text = ControlText(source);
                if (ControlText(target) != text) SetControlText(target, text);
            }
            else if (identifier == control_id::HostPort ||
                identifier == control_id::JoinPort ||
                identifier == control_id::NetworkPort)
            {
                HWND source = identifier == control_id::HostPort
                    ? hostPort_ : identifier == control_id::JoinPort
                        ? joinPort_ : networkPort_;
                const std::wstring text = ControlText(source);
                HWND targets[] = {hostPort_, joinPort_, networkPort_};
                for (HWND target : targets)
                {
                    if (target != source && ControlText(target) != text)
                    {
                        SetControlText(target, text);
                    }
                }
            }
            return;
        }
        if (notification != BN_CLICKED)
        {
            return;
        }

        switch (identifier)
        {
        case control_id::NavPlay: ShowPage(Page::Play); break;
        case control_id::NavNetwork: ShowPage(Page::Network); break;
        case control_id::NavSettings: ShowPage(Page::Settings); break;
        case control_id::ShowConsole:
            showConsoleChecked_ = !showConsoleChecked_;
            InvalidateRect(showConsole_, nullptr, TRUE);
            break;
        case control_id::GenerateLogs:
            generateLogsChecked_ = !generateLogsChecked_;
            InvalidateRect(generateLogs_, nullptr, TRUE);
            break;
        case control_id::HostGame: Launch(true); break;
        case control_id::JoinGame: Launch(false); break;
        case control_id::RunTests:
        case control_id::NetworkTest: BeginDiagnostics(true); break;
        case control_id::RepairFirewall: RepairFirewall(); break;
        case control_id::BrowseGame: BrowseForGame(); break;
        case control_id::SaveSettings: SaveSettings(); break;
        case control_id::OpenDiscord: OpenDiscord(); break;
        case control_id::OpenDiagnostics: OpenLatestDiagnostics(); break;
        default: break;
        }
    }

    void LauncherWindow::BeginDiagnostics(const bool offerRepair)
    {
        if (diagnosticsTask_.IsRunning())
        {
            return;
        }
        LauncherSettings current;
        std::wstring error;
        if (!ReadSettingsFromControls(current, error))
        {
            SetStatus(error, theme::Error);
            return;
        }
        offerRepairAfterDiagnostics_ = offerRepair;
        EnableWindow(runTests_, FALSE);
        EnableWindow(networkTest_, FALSE);
        SetStatus(
            L"Running compatibility and connection tests...", theme::Muted);
        if (!diagnosticsTask_.Start(
                window_,
                DiagnosticsCompleteMessage,
                ResolveGameExecutable(),
                current.hostAddress,
                current.port))
        {
            EnableWindow(runTests_, TRUE);
            EnableWindow(networkTest_, TRUE);
            SetStatus(
                L"Connection tests are already running.", theme::Warning);
        }
    }

    void LauncherWindow::CompleteDiagnostics(
        diagnostics::LauncherDiagnosticsReport* rawReport)
    {
        std::unique_ptr<diagnostics::LauncherDiagnosticsReport> report(rawReport);
        diagnosticsTask_.Join();
        EnableWindow(runTests_, TRUE);
        EnableWindow(networkTest_, TRUE);
        if (report == nullptr)
        {
            SetStatus(L"Connection tests failed unexpectedly.", theme::Error);
            return;
        }
        diagnostics_ = std::move(*report);
        const bool ready = diagnostics_.game.IsCompatible() &&
            diagnostics_.firewall.AllowsTraffic();
        SetStatus(
            ready ? L"Ready to play" : L"Setup needs attention",
            ready ? theme::Green : theme::Warning);
        EnableWindow(
            repairFirewall_,
            diagnostics_.firewall.state ==
                diagnostics::FirewallState::RuleMissing);
        InvalidateRect(window_, nullptr, TRUE);
        if (offerRepairAfterDiagnostics_)
        {
            offerRepairAfterDiagnostics_ = false;
            OfferFirewallRepair();
        }
    }

    void LauncherWindow::OfferFirewallRepair()
    {
        if (diagnostics_.firewall.state !=
            diagnostics::FirewallState::RuleMissing)
        {
            return;
        }
        if (MessageBoxW(
                window_,
                L"AlbionTogether is not allowed through Windows Firewall on the selected UDP port. Fix it now?",
                L"Windows Firewall",
                MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            RepairFirewall();
        }
    }

    void LauncherWindow::RepairFirewall()
    {
        LauncherSettings current;
        std::wstring error;
        if (!ReadSettingsFromControls(current, error))
        {
            SetStatus(error, theme::Error);
            return;
        }
        SetStatus(
            L"Waiting for Windows Firewall permission...", theme::Muted);
        if (!workflow_.RepairFirewall(current.port, error))
        {
            MessageBoxW(
                window_, error.c_str(), L"Windows Firewall",
                MB_OK | MB_ICONERROR);
            SetStatus(L"Firewall setup was not changed.", theme::Error);
            return;
        }
        SetStatus(L"Firewall rule added. Rechecking...", theme::Green);
        BeginDiagnostics(false);
    }

    void LauncherWindow::Launch(const bool host)
    {
        UpdateGameRunningState();
        if (host && gameRunning_)
        {
            SetStatus(
                L"A game is already running on this PC.", theme::Warning);
            return;
        }

        LauncherSettings current;
        std::wstring result;
        if (!ReadSettingsFromControls(current, result))
        {
            MessageBoxW(
                window_, result.c_str(), L"AlbionTogether",
                MB_OK | MB_ICONWARNING);
            return;
        }
        const auto compatibility = workflow_.CheckGame(current);
        if (!compatibility.IsCompatible())
        {
            SetStatus(compatibility.detail, theme::Error);
            MessageBoxW(
                window_, compatibility.detail.c_str(), L"Unsupported game",
                MB_OK | MB_ICONERROR);
            ShowPage(Page::Settings);
            return;
        }
        if (host)
        {
            const auto firewall = workflow_.CheckFirewall(current.port);
            if (firewall.state == diagnostics::FirewallState::RuleMissing &&
                MessageBoxW(
                    window_,
                    L"Windows Firewall is not configured for this port. Fix it before hosting?",
                    L"Windows Firewall",
                    MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                settings_ = current;
                RepairFirewall();
                return;
            }
        }

        settings_ = current;
        const bool launched = workflow_.Launch(
            host ? application::InteractiveRole::Host
                 : application::InteractiveRole::Guest,
            current,
            result);
        UpdateGameRunningState();
        SetStatus(result, launched ? theme::Green : theme::Error);
        if (!launched)
        {
            MessageBoxW(
                window_, result.c_str(), L"AlbionTogether",
                MB_OK | MB_ICONERROR);
        }
    }

    void LauncherWindow::UpdateGameRunningState()
    {
        const bool running = runtime::IsGameProcessRunning();
        if (gameRunning_ == running)
        {
            return;
        }
        gameRunning_ = running;
        EnableWindow(hostButton_, running ? FALSE : TRUE);
        RedrawWindow(
            hostButton_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW);
    }

    void LauncherWindow::BrowseForGame()
    {
        std::filesystem::path selected;
        const std::filesystem::path current = ControlText(gamePath_);
        if (platform::PickFolder(window_, current, selected))
        {
            SetControlText(gamePath_, selected.wstring());
            SaveSettings();
            BeginDiagnostics(false);
        }
    }

    void LauncherWindow::SaveSettings()
    {
        LauncherSettings current;
        std::wstring error;
        if (!ReadSettingsFromControls(current, error))
        {
            SetStatus(error, theme::Error);
            return;
        }
        if (!workflow_.SaveSettings(current))
        {
            SetStatus(L"Settings could not be saved.", theme::Error);
            return;
        }
        settings_ = current;
        SetStatus(L"Settings saved", theme::Green);
    }

    void LauncherWindow::OpenDiscord()
    {
        const HINSTANCE opened = ShellExecuteW(
            window_, L"open", kDiscordUrl, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32)
        {
            SetStatus(L"Discord could not be opened.", theme::Error);
        }
    }

    void LauncherWindow::OpenLatestDiagnostics()
    {
        const std::filesystem::path log = workflow_.LatestDiagnosticLog();
        if (log.empty())
        {
            MessageBoxW(
                window_,
                L"No diagnostic log has been generated yet.",
                L"AlbionTogether diagnostics",
                MB_OK | MB_ICONINFORMATION);
            return;
        }
        const HINSTANCE opened = ShellExecuteW(
            window_, L"open", log.c_str(), nullptr,
            log.parent_path().c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32)
        {
            MessageBoxW(
                window_,
                L"The latest diagnostic log could not be opened.",
                L"AlbionTogether diagnostics",
                MB_OK | MB_ICONERROR);
        }
    }

    void LauncherWindow::SetStatus(
        const std::wstring& value,
        const COLORREF color)
    {
        statusText_ = value;
        statusColor_ = color;
        InvalidateRect(window_, nullptr, FALSE);
    }
}
