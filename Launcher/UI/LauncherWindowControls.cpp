#include "LauncherWindow.h"

#include "LauncherControlIds.h"
#include "LauncherLayout.h"
#include "LauncherTheme.h"

#include <CommCtrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwchar>

#pragma comment(lib, "uxtheme.lib")

namespace fable::launcher::ui
{
    namespace
    {
        constexpr wchar_t ButtonHotProperty[] =
            L"AlbionTogether.ButtonHot";

        std::wstring Trim(std::wstring value)
        {
            const auto visible = [](const wchar_t character)
            {
                return iswspace(character) == 0;
            };
            const auto first = std::find_if(value.begin(), value.end(), visible);
            const auto last = std::find_if(
                value.rbegin(), value.rend(), visible).base();
            return first >= last ? std::wstring() : std::wstring(first, last);
        }

        void SetCue(HWND control, const wchar_t* text)
        {
            SendMessageW(
                control, EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(text));
        }

        LRESULT CALLBACK EditSubclassProcedure(
            HWND control,
            const UINT message,
            const WPARAM wparam,
            const LPARAM lparam,
            UINT_PTR,
            DWORD_PTR)
        {
            if (message == WM_NCDESTROY)
            {
                RemoveWindowSubclass(control, EditSubclassProcedure, 1);
            }

            const LRESULT result = DefSubclassProc(
                control, message, wparam, lparam);
            if (message == WM_PAINT || message == WM_SETFOCUS ||
                message == WM_KILLFOCUS)
            {
                HDC context = GetDC(control);
                if (context != nullptr)
                {
                    RECT rectangle = {};
                    GetClientRect(control, &rectangle);
                    HPEN pen = CreatePen(
                        PS_SOLID,
                        1,
                        GetFocus() == control ? theme::Border : theme::Divider);
                    HPEN previousPen = static_cast<HPEN>(
                        SelectObject(context, pen));
                    HBRUSH previousBrush = static_cast<HBRUSH>(
                        SelectObject(context, GetStockObject(NULL_BRUSH)));
                    Rectangle(
                        context,
                        rectangle.left,
                        rectangle.top,
                        rectangle.right,
                        rectangle.bottom);
                    SelectObject(context, previousBrush);
                    SelectObject(context, previousPen);
                    DeleteObject(pen);
                    ReleaseDC(control, context);
                }
            }
            return result;
        }

        LRESULT CALLBACK ButtonSubclassProcedure(
            HWND control,
            const UINT message,
            const WPARAM wparam,
            const LPARAM lparam,
            UINT_PTR,
            DWORD_PTR)
        {
            if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (message == WM_MOUSEMOVE &&
                GetPropW(control, ButtonHotProperty) == nullptr)
            {
                SetPropW(
                    control,
                    ButtonHotProperty,
                    reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT tracking = {
                    sizeof(tracking), TME_LEAVE, control, 0};
                TrackMouseEvent(&tracking);
                RedrawWindow(
                    control, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW);
            }
            else if (message == WM_MOUSELEAVE)
            {
                RemovePropW(control, ButtonHotProperty);
                RedrawWindow(
                    control, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW);
            }

            const LRESULT result = DefSubclassProc(
                control, message, wparam, lparam);
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
                message == WM_CAPTURECHANGED || message == WM_KEYDOWN ||
                message == WM_KEYUP)
            {
                RedrawWindow(
                    control, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_UPDATENOW);
            }
            if (message == WM_NCDESTROY)
            {
                RemovePropW(control, ButtonHotProperty);
                RemoveWindowSubclass(control, ButtonSubclassProcedure, 2);
            }
            return result;
        }
    }

    void LauncherWindow::CreateFonts()
    {
        titleFont_ = CreateFontW(
            -Scale(29), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        headingFont_ = CreateFontW(
            -Scale(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        bodyFont_ = CreateFontW(
            -Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont_ = CreateFontW(
            -Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }

    void LauncherWindow::CreateControls()
    {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(window_, GWLP_HINSTANCE));
        const auto button = [&](const wchar_t* text, const int id)
        {
            HWND control = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(id),
                instance, nullptr);
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            SetWindowSubclass(control, ButtonSubclassProcedure, 2, 0);
            return control;
        };
        const auto edit = [&](const int id, const wchar_t* cue)
        {
            HWND control = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(id),
                instance, nullptr);
            SetWindowTheme(control, L"", L"");
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            SendMessageW(
                control,
                EM_SETMARGINS,
                EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(Scale(10), Scale(10)));
            SetWindowSubclass(control, EditSubclassProcedure, 1, 0);
            SetCue(control, cue);
            return control;
        };
        const auto check = [&](const wchar_t* text, const int id)
        {
            HWND control = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(id),
                instance, nullptr);
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            return control;
        };

        navPlay_ = button(L"Play", control_id::NavPlay);
        navNetwork_ = button(L"Network", control_id::NavNetwork);
        navSettings_ = button(L"Settings", control_id::NavSettings);
        ShowWindow(navPlay_, SW_SHOW);
        ShowWindow(navNetwork_, SW_SHOW);
        ShowWindow(navSettings_, SW_SHOW);

        hostName_ = edit(control_id::HostName, L"Display name");
        hostPort_ = edit(control_id::HostPort, L"38171");
        hostButton_ = button(L"Host Game", control_id::HostGame);
        joinName_ = edit(control_id::JoinName, L"Display name");
        joinAddress_ = edit(control_id::JoinAddress, L"Host IP address");
        joinPort_ = edit(control_id::JoinPort, L"38171");
        joinButton_ = button(L"Join Game", control_id::JoinGame);
        runTests_ = button(L"Run tests", control_id::RunTests);
        playControls_ = {
            hostName_, hostPort_, hostButton_, joinName_, joinAddress_,
            joinPort_, joinButton_, runTests_};

        networkAddress_ = edit(
            control_id::NetworkAddress, L"Host IP address");
        networkPort_ = edit(control_id::NetworkPort, L"38171");
        networkTest_ = button(
            L"Run connection tests", control_id::NetworkTest);
        repairFirewall_ = button(
            L"Fix Windows Firewall", control_id::RepairFirewall);
        networkControls_ = {
            networkAddress_, networkPort_, networkTest_, repairFirewall_};

        gamePath_ = edit(
            control_id::GamePath,
            L"Fable Anniversary folder");
        browseGame_ = button(L"Browse...", control_id::BrowseGame);
        showConsole_ = check(
            L"Open diagnostics console when the game starts",
            control_id::ShowConsole);
        generateLogs_ = check(
            L"Generate diagnostic log files", control_id::GenerateLogs);
        saveSettings_ = button(
            L"Save settings", control_id::SaveSettings);
        settingsControls_ = {
            gamePath_, browseGame_, showConsole_, generateLogs_, saveSettings_};

        discord_ = button(L"Discord", control_id::OpenDiscord);
        openDiagnostics_ = button(
            L"Diagnostics", control_id::OpenDiagnostics);
        SendMessageW(discord_, WM_SETFONT,
            reinterpret_cast<WPARAM>(smallFont_), TRUE);
        SendMessageW(openDiagnostics_, WM_SETFONT,
            reinterpret_cast<WPARAM>(smallFont_), TRUE);
        ShowWindow(discord_, SW_SHOW);
        ShowWindow(openDiagnostics_, SW_SHOW);
    }

    void LauncherWindow::LayoutControls()
    {
        if (window_ == nullptr || navPlay_ == nullptr)
        {
            return;
        }
        RECT client = {};
        GetClientRect(window_, &client);
        const LauncherLayout layout = LauncherLayout::Calculate(client, dpi_);
        const int navigationX = Scale(18);
        const int navigationWidth = layout.navigation.right - Scale(36);
        const int navigationTop = layout.navigation.top + Scale(34);
        const int navigationHeight = Scale(50);
        MoveWindow(navPlay_, navigationX, navigationTop,
            navigationWidth, navigationHeight, TRUE);
        MoveWindow(navNetwork_, navigationX,
            navigationTop + Scale(58), navigationWidth, navigationHeight, TRUE);
        MoveWindow(navSettings_, navigationX,
            navigationTop + Scale(116), navigationWidth, navigationHeight, TRUE);

        const int hostX = layout.hostPanel.left + Scale(28);
        const int hostWidth = layout.hostPanel.right -
            layout.hostPanel.left - Scale(56);
        const int hostTop = layout.hostPanel.top + Scale(100);
        MoveWindow(hostName_, hostX, hostTop, hostWidth, Scale(42), TRUE);
        MoveWindow(hostPort_, hostX, hostTop + Scale(66),
            hostWidth, Scale(42), TRUE);
        MoveWindow(hostButton_, hostX, layout.hostPanel.bottom - Scale(68),
            hostWidth, Scale(44), TRUE);

        const int joinX = layout.joinPanel.left + Scale(28);
        const int joinWidth = layout.joinPanel.right -
            layout.joinPanel.left - Scale(56);
        const int joinTop = layout.joinPanel.top + Scale(100);
        MoveWindow(joinName_, joinX, joinTop, joinWidth, Scale(42), TRUE);
        MoveWindow(joinAddress_, joinX, joinTop + Scale(66),
            joinWidth, Scale(42), TRUE);
        MoveWindow(joinPort_, joinX, joinTop + Scale(132),
            joinWidth, Scale(42), TRUE);
        MoveWindow(joinButton_, joinX, layout.joinPanel.bottom - Scale(68),
            joinWidth, Scale(44), TRUE);
        MoveWindow(
            runTests_,
            layout.diagnosticsPanel.left + Scale(24),
            layout.diagnosticsPanel.bottom - Scale(68),
            layout.diagnosticsPanel.right - layout.diagnosticsPanel.left - Scale(48),
            Scale(44), TRUE);

        const int networkX = layout.networkPanel.left + Scale(32);
        const int networkTop = layout.networkPanel.top + Scale(116);
        const int networkWidth = Scale(330);
        MoveWindow(networkAddress_, networkX, networkTop,
            networkWidth, Scale(42), TRUE);
        MoveWindow(networkPort_, networkX + networkWidth + Scale(16),
            networkTop, Scale(110), Scale(42), TRUE);
        MoveWindow(networkTest_, networkX, networkTop + Scale(52),
            Scale(220), Scale(44), TRUE);
        MoveWindow(repairFirewall_, networkX + Scale(236),
            networkTop + Scale(52), Scale(220), Scale(44), TRUE);

        const int settingsX = layout.settingsPanel.left + Scale(32);
        const int settingsTop = layout.settingsPanel.top + Scale(84);
        const int browseWidth = Scale(118);
        const int settingsWidth = layout.settingsPanel.right -
            layout.settingsPanel.left - Scale(64);
        MoveWindow(gamePath_, settingsX, settingsTop,
            settingsWidth - browseWidth - Scale(12), Scale(42), TRUE);
        MoveWindow(browseGame_,
            settingsX + settingsWidth - browseWidth, settingsTop,
            browseWidth, Scale(42), TRUE);
        MoveWindow(showConsole_, settingsX,
            layout.settingsPanel.top + Scale(194),
            settingsWidth, Scale(40), TRUE);
        MoveWindow(generateLogs_, settingsX,
            layout.settingsPanel.top + Scale(242),
            settingsWidth, Scale(40), TRUE);
        MoveWindow(saveSettings_, settingsX,
            layout.settingsPanel.bottom - Scale(64),
            Scale(180), Scale(44), TRUE);

        const int footerTop = layout.footer.top + Scale(5);
        const int diagnosticsWidth = Scale(100);
        const int discordWidth = Scale(76);
        const int footerRight = client.right - Scale(22);
        MoveWindow(openDiagnostics_,
            footerRight - diagnosticsWidth,
            footerTop,
            diagnosticsWidth,
            Scale(40),
            TRUE);
        MoveWindow(discord_,
            footerRight - diagnosticsWidth - Scale(8) - discordWidth,
            footerTop,
            discordWidth,
            Scale(40),
            TRUE);
    }

    void LauncherWindow::ShowPage(const Page page)
    {
        page_ = page;
        const auto show = [](const std::vector<HWND>& controls, const bool visible)
        {
            for (HWND control : controls)
            {
                ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
            }
        };
        show(playControls_, page == Page::Play);
        show(networkControls_, page == Page::Network);
        show(settingsControls_, page == Page::Settings);
        for (HWND navigation : {navPlay_, navNetwork_, navSettings_})
        {
            RedrawWindow(
                navigation,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW);
        }
        InvalidateRect(window_, nullptr, TRUE);
    }

    void LauncherWindow::LoadControlsFromSettings()
    {
        SetControlText(hostName_, settings_.playerName);
        SetControlText(joinName_, settings_.playerName);
        SetControlText(joinAddress_, settings_.hostAddress);
        SetControlText(networkAddress_, settings_.hostAddress);
        const std::wstring port = std::to_wstring(settings_.port);
        SetControlText(hostPort_, port);
        SetControlText(joinPort_, port);
        SetControlText(networkPort_, port);
        SetControlText(gamePath_, settings_.gameDirectory.wstring());
        showConsoleChecked_ = settings_.showConsole;
        generateLogsChecked_ = settings_.generateLogs;
        InvalidateRect(showConsole_, nullptr, TRUE);
        InvalidateRect(generateLogs_, nullptr, TRUE);
    }

    bool LauncherWindow::ReadSettingsFromControls(
        LauncherSettings& settings,
        std::wstring& error) const
    {
        error.clear();
        settings.gameDirectory = Trim(ControlText(gamePath_));
        settings.playerName = Trim(ControlText(hostName_));
        settings.hostAddress = Trim(ControlText(joinAddress_));
        const std::wstring portText = Trim(ControlText(hostPort_));
        wchar_t* end = nullptr;
        const unsigned long port = std::wcstoul(portText.c_str(), &end, 10);
        if (portText.empty() || end == portText.c_str() || *end != L'\0' ||
            port == 0 || port > 65'535)
        {
            error = L"The port must be between 1 and 65535.";
            return false;
        }
        if (settings.playerName.empty() || settings.playerName.size() > 32)
        {
            error = L"Display names must contain 1 to 32 characters.";
            return false;
        }
        settings.port = static_cast<unsigned short>(port);
        settings.showConsole = showConsoleChecked_;
        settings.generateLogs = generateLogsChecked_;
        return true;
    }

    std::filesystem::path LauncherWindow::ResolveGameExecutable() const
    {
        LauncherSettings current = settings_;
        current.gameDirectory = Trim(ControlText(gamePath_));
        return workflow_.ResolveGameExecutable(current);
    }

    std::wstring LauncherWindow::ControlText(HWND control) const
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(control, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(length));
        return value;
    }

    void LauncherWindow::SetControlText(
        HWND control,
        const std::wstring& value)
    {
        SetWindowTextW(control, value.c_str());
    }

    int LauncherWindow::Scale(const int value) const noexcept
    {
        return MulDiv(value, static_cast<int>(dpi_), 96);
    }
}
