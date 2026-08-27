#include "LauncherWindow.h"

#include "../Configuration/LauncherConstants.h"
#include "LauncherLayout.h"
#include "LauncherTheme.h"

#include <algorithm>
#include <iterator>

namespace fable::launcher::ui
{
    namespace
    {
        void Fill(HDC context, const RECT& rectangle, const COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(context, &rectangle, brush);
            DeleteObject(brush);
        }

        void Text(
            HDC context,
            HFONT font,
            const COLORREF color,
            const std::wstring& value,
            RECT rectangle,
            const UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            HFONT previous = static_cast<HFONT>(SelectObject(context, font));
            SetTextColor(context, color);
            SetBkMode(context, TRANSPARENT);
            DrawTextW(context, value.c_str(), -1, &rectangle, format);
            SelectObject(context, previous);
        }

        void Panel(HDC context, const RECT& rectangle, const int radius)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, theme::Border);
            HBRUSH brush = CreateSolidBrush(theme::Panel);
            HPEN previousPen = static_cast<HPEN>(SelectObject(context, pen));
            HBRUSH previousBrush = static_cast<HBRUSH>(SelectObject(context, brush));
            RoundRect(
                context,
                rectangle.left,
                rectangle.top,
                rectangle.right,
                rectangle.bottom,
                radius,
                radius);
            SelectObject(context, previousBrush);
            SelectObject(context, previousPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }

        void StatusLine(
            HDC context,
            HFONT font,
            const RECT& panel,
            const int y,
            const int scale,
            const std::wstring& label,
            const std::wstring& value,
            const COLORREF color)
        {
            HBRUSH dot = CreateSolidBrush(color);
            RECT dotRectangle = {
                panel.left + scale * 24 / 10,
                y + scale * 5 / 10,
                panel.left + scale * 24 / 10 + scale,
                y + scale * 15 / 10};
            FillRect(context, &dotRectangle, dot);
            DeleteObject(dot);
            Text(
                context,
                font,
                theme::Ivory,
                label,
                {panel.left + scale * 44 / 10, y,
                 panel.right - scale * 16 / 10, y + scale * 24 / 10});
            Text(
                context,
                font,
                color,
                value,
                {panel.left + scale * 44 / 10, y + scale * 22 / 10,
                 panel.right - scale * 16 / 10, y + scale * 44 / 10});
        }

        std::wstring GameStatus(
            const diagnostics::GameCompatibilityResult& game)
        {
            switch (game.state)
            {
            case diagnostics::GameCompatibilityState::Compatible:
                return L"Compatible";
            case diagnostics::GameCompatibilityState::Missing:
                return L"Not found";
            case diagnostics::GameCompatibilityState::Unsupported:
                return L"Unsupported build";
            default:
                return L"Check failed";
            }
        }

        COLORREF GameColor(
            const diagnostics::GameCompatibilityResult& game)
        {
            return game.IsCompatible() ? theme::Green : theme::Error;
        }

        std::wstring HeaderGameStatus(
            const diagnostics::GameCompatibilityResult& game)
        {
            switch (game.state)
            {
            case diagnostics::GameCompatibilityState::Compatible:
                return L"Game detected";
            case diagnostics::GameCompatibilityState::Missing:
                return L"Game not found";
            case diagnostics::GameCompatibilityState::Unsupported:
                return L"Unsupported game";
            default:
                return L"Game check failed";
            }
        }

        std::wstring FirewallStatus(const diagnostics::FirewallResult& firewall)
        {
            switch (firewall.state)
            {
            case diagnostics::FirewallState::Allowed: return L"Allowed";
            case diagnostics::FirewallState::RuleMissing: return L"Needs setup";
            case diagnostics::FirewallState::FirewallDisabled: return L"Disabled";
            default: return L"Check failed";
            }
        }

        COLORREF FirewallColor(const diagnostics::FirewallResult& firewall)
        {
            return firewall.AllowsTraffic() ? theme::Green :
                firewall.state == diagnostics::FirewallState::RuleMissing
                    ? theme::Warning : theme::Error;
        }

        std::wstring HostStatus(
            const diagnostics::HostReachabilityResult& host)
        {
            using diagnostics::HostReachabilityState;
            switch (host.state)
            {
            case HostReachabilityState::AlbionTogetherDetected:
                return L"Host detected";
            case HostReachabilityState::AddressReachable:
                return L"Address reachable";
            case HostReachabilityState::Unreachable:
                return L"No response";
            case HostReachabilityState::InvalidAddress:
                return L"Invalid address";
            case HostReachabilityState::Error:
                return L"Check failed";
            default:
                return L"Not tested";
            }
        }

        COLORREF HostColor(const diagnostics::HostReachabilityResult& host)
        {
            using diagnostics::HostReachabilityState;
            switch (host.state)
            {
            case HostReachabilityState::AlbionTogetherDetected:
                return theme::Green;
            case HostReachabilityState::AddressReachable:
                return theme::Green;
            case HostReachabilityState::NotTested:
                return theme::Warning;
            default:
                return theme::Error;
            }
        }
    }

    void LauncherWindow::Paint()
    {
        PAINTSTRUCT paint = {};
        HDC target = BeginPaint(window_, &paint);
        RECT client = {};
        GetClientRect(window_, &client);
        HDC context = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(
            target,
            (std::max)(1L, client.right),
            (std::max)(1L, client.bottom));
        HBITMAP previousBitmap = static_cast<HBITMAP>(
            SelectObject(context, bitmap));

        const LauncherLayout layout = LauncherLayout::Calculate(client, dpi_);
        Fill(context, client, theme::Window);
        Fill(context, layout.header, theme::Header);
        Fill(context, layout.navigation, theme::Navigation);
        Fill(context, layout.footer, theme::Header);
        Fill(context,
            {layout.header.left, layout.header.bottom - 1,
             layout.header.right, layout.header.bottom},
            theme::Divider);
        Fill(context,
            {layout.navigation.right - 1, layout.navigation.top,
             layout.navigation.right, layout.navigation.bottom},
            theme::Divider);
        Fill(context,
            {layout.footer.left, layout.footer.top,
             layout.footer.right, layout.footer.top + 1},
            theme::Divider);

        Text(context, headingFont_, theme::Ivory, L"AlbionTogether",
            {Scale(28), 0, Scale(230), layout.header.bottom});
        Text(context, smallFont_, theme::Muted,
            L"Fable Anniversary multiplayer",
            {Scale(220), 0, Scale(500), layout.header.bottom});
        HBRUSH gameDot = CreateSolidBrush(GameColor(diagnostics_.game));
        RECT gameDotRectangle = {
            client.right - Scale(236), Scale(31),
            client.right - Scale(225), Scale(42)};
        FillRect(context, &gameDotRectangle, gameDot);
        DeleteObject(gameDot);
        Text(context, bodyFont_, theme::Ivory,
            HeaderGameStatus(diagnostics_.game),
            {client.right - Scale(210), 0,
             client.right - Scale(28), layout.header.bottom});

        const wchar_t* pageTitle = page_ == Page::Play
            ? L"Play together" : page_ == Page::Network
                ? L"Network" : L"Settings";
        const wchar_t* pageDescription = page_ == Page::Play
            ? L"Host a session or join a friend directly by IP."
            : page_ == Page::Network
                ? L"Check your local network, host address, and Windows Firewall."
                : L"Choose your game folder and diagnostic preferences.";
        Text(context, titleFont_, theme::Ivory, pageTitle,
            {layout.content.left + Scale(38),
             layout.content.top + Scale(28),
             layout.content.right - Scale(30),
             layout.content.top + Scale(76)});
        Text(context, bodyFont_, theme::Muted, pageDescription,
            {layout.content.left + Scale(38),
             layout.content.top + Scale(79),
             layout.content.right - Scale(30),
             layout.content.top + Scale(112)});

        if (page_ == Page::Play)
        {
            Panel(context, layout.hostPanel, Scale(8));
            Panel(context, layout.joinPanel, Scale(8));
            Panel(context, layout.diagnosticsPanel, Scale(8));

            Text(context, headingFont_, theme::Ivory, L"Host a game",
                {layout.hostPanel.left + Scale(28), layout.hostPanel.top + Scale(18),
                 layout.hostPanel.right - Scale(20), layout.hostPanel.top + Scale(50)});
            Text(context, smallFont_, theme::Muted,
                L"Create a session and invite friends.",
                {layout.hostPanel.left + Scale(28), layout.hostPanel.top + Scale(50),
                 layout.hostPanel.right - Scale(20), layout.hostPanel.top + Scale(76)});
            Text(context, smallFont_, theme::Muted, L"Display name",
                {layout.hostPanel.left + Scale(28), layout.hostPanel.top + Scale(78),
                 layout.hostPanel.right, layout.hostPanel.top + Scale(98)});
            Text(context, smallFont_, theme::Muted, L"Port",
                {layout.hostPanel.left + Scale(28), layout.hostPanel.top + Scale(144),
                 layout.hostPanel.right, layout.hostPanel.top + Scale(164)});

            Text(context, headingFont_, theme::Ivory, L"Join a friend",
                {layout.joinPanel.left + Scale(28), layout.joinPanel.top + Scale(18),
                 layout.joinPanel.right - Scale(20), layout.joinPanel.top + Scale(50)});
            Text(context, smallFont_, theme::Muted,
                L"Connect directly to the host's IP.",
                {layout.joinPanel.left + Scale(28), layout.joinPanel.top + Scale(50),
                 layout.joinPanel.right - Scale(20), layout.joinPanel.top + Scale(76)});
            Text(context, smallFont_, theme::Muted, L"Display name",
                {layout.joinPanel.left + Scale(28), layout.joinPanel.top + Scale(78),
                 layout.joinPanel.right, layout.joinPanel.top + Scale(98)});
            Text(context, smallFont_, theme::Muted, L"Host IP address",
                {layout.joinPanel.left + Scale(28), layout.joinPanel.top + Scale(144),
                 layout.joinPanel.right, layout.joinPanel.top + Scale(164)});
            Text(context, smallFont_, theme::Muted, L"Port",
                {layout.joinPanel.left + Scale(28), layout.joinPanel.top + Scale(210),
                 layout.joinPanel.right, layout.joinPanel.top + Scale(230)});

            Text(context, headingFont_, theme::Ivory, L"Connection check",
                {layout.diagnosticsPanel.left + Scale(24),
                 layout.diagnosticsPanel.top + Scale(18),
                 layout.diagnosticsPanel.right - Scale(18),
                 layout.diagnosticsPanel.top + Scale(50)});
            const int statusScale = Scale(10);
            int row = layout.diagnosticsPanel.top + Scale(66);
            StatusLine(context, smallFont_, layout.diagnosticsPanel, row,
                statusScale, L"Game", GameStatus(diagnostics_.game),
                GameColor(diagnostics_.game));
            row += Scale(50);
            StatusLine(context, smallFont_, layout.diagnosticsPanel, row,
                statusScale, L"Windows Firewall",
                FirewallStatus(diagnostics_.firewall),
                FirewallColor(diagnostics_.firewall));
            row += Scale(50);
            StatusLine(context, smallFont_, layout.diagnosticsPanel, row,
                statusScale, L"Host address", HostStatus(diagnostics_.host),
                HostColor(diagnostics_.host));
            row += Scale(50);
            const std::wstring local = diagnostics_.localAddresses.empty()
                ? L"Not found" : diagnostics_.localAddresses.front();
            StatusLine(context, smallFont_, layout.diagnosticsPanel, row,
                statusScale, L"Local IP", local,
                diagnostics_.localAddresses.empty() ? theme::Error : theme::Green);
        }
        else if (page_ == Page::Network)
        {
            Panel(context, layout.networkPanel, Scale(8));
            Text(context, headingFont_, theme::Ivory, L"Local network",
                {layout.networkPanel.left + Scale(32),
                 layout.networkPanel.top + Scale(18),
                 layout.networkPanel.right, layout.networkPanel.top + Scale(50)});
            const std::wstring localAddresses = diagnostics_.localAddresses.empty()
                ? L"No active local IPv4 address found"
                : diagnostics_.localAddresses.front();
            Text(context, bodyFont_, theme::Green, localAddresses,
                {layout.networkPanel.left + Scale(32),
                 layout.networkPanel.top + Scale(50),
                 layout.networkPanel.right - Scale(30),
                 layout.networkPanel.top + Scale(78)});
            Text(context, smallFont_, theme::Muted, L"Host IP address",
                {layout.networkPanel.left + Scale(32),
                 layout.networkPanel.top + Scale(92),
                 layout.networkPanel.left + Scale(350),
                 layout.networkPanel.top + Scale(112)});
            Text(context, smallFont_, theme::Muted, L"Port",
                {layout.networkPanel.left + Scale(378),
                 layout.networkPanel.top + Scale(92),
                 layout.networkPanel.left + Scale(480),
                 layout.networkPanel.top + Scale(112)});

            const int detailTop = layout.networkPanel.top + Scale(214);
            Text(context, headingFont_, theme::Ivory, L"Latest results",
                {layout.networkPanel.left + Scale(32), detailTop,
                 layout.networkPanel.right - Scale(30), detailTop + Scale(35)});
            Text(context, smallFont_, FirewallColor(diagnostics_.firewall),
                diagnostics_.firewall.detail.empty()
                    ? L"Firewall has not been checked yet."
                    : diagnostics_.firewall.detail,
                {layout.networkPanel.left + Scale(32), detailTop + Scale(46),
                 layout.networkPanel.right - Scale(30), detailTop + Scale(78)});
            Text(context, smallFont_, HostColor(diagnostics_.host),
                diagnostics_.host.detail.empty()
                    ? L"Host address has not been checked yet."
                    : diagnostics_.host.detail,
                {layout.networkPanel.left + Scale(32), detailTop + Scale(86),
                 layout.networkPanel.right - Scale(30), detailTop + Scale(130)},
                DT_LEFT | DT_WORDBREAK);
        }
        else
        {
            Panel(context, layout.settingsPanel, Scale(8));
            Text(context, headingFont_, theme::Ivory, L"Game installation",
                {layout.settingsPanel.left + Scale(32),
                 layout.settingsPanel.top + Scale(18),
                 layout.settingsPanel.right, layout.settingsPanel.top + Scale(50)});
            Text(context, smallFont_, theme::Muted, L"Fable Anniversary folder",
                {layout.settingsPanel.left + Scale(32),
                 layout.settingsPanel.top + Scale(58),
                 layout.settingsPanel.right, layout.settingsPanel.top + Scale(80)});
            Text(context, headingFont_, theme::Ivory, L"Diagnostics",
                {layout.settingsPanel.left + Scale(32),
                 layout.settingsPanel.top + Scale(160),
                 layout.settingsPanel.right, layout.settingsPanel.top + Scale(192)});
        }

        Text(context, smallFont_, theme::Muted, kLauncherVersion,
            {Scale(26), layout.footer.top,
             Scale(210), layout.footer.bottom});
        HBRUSH statusDot = CreateSolidBrush(statusColor_);
        RECT statusDotRectangle = {
            client.right / 2 - Scale(130), layout.footer.top + Scale(19),
            client.right / 2 - Scale(120), layout.footer.top + Scale(29)};
        FillRect(context, &statusDotRectangle, statusDot);
        DeleteObject(statusDot);
        Text(context, smallFont_, statusColor_, statusText_,
            {client.right / 2 - Scale(108), layout.footer.top,
             client.right - Scale(250), layout.footer.bottom});

        BitBlt(target, 0, 0, client.right, client.bottom, context, 0, 0, SRCCOPY);
        SelectObject(context, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(context);
        EndPaint(window_, &paint);
    }

    void LauncherWindow::DrawButton(const DRAWITEMSTRUCT& item)
    {
        HDC context = item.hDC;
        RECT rectangle = item.rcItem;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool hovered = !disabled &&
            GetPropW(
                item.hwndItem,
                L"AlbionTogether.ButtonHot") != nullptr;
        const bool navigation = item.hwndItem == navPlay_ ||
            item.hwndItem == navNetwork_ || item.hwndItem == navSettings_;
        const bool selectedNavigation =
            item.hwndItem == navPlay_ && page_ == Page::Play ||
            item.hwndItem == navNetwork_ && page_ == Page::Network ||
            item.hwndItem == navSettings_ && page_ == Page::Settings;
        const bool primary = item.hwndItem == hostButton_ ||
            item.hwndItem == joinButton_;
        const bool checkbox = item.hwndItem == showConsole_ ||
            item.hwndItem == generateLogs_;
        const bool footerLink = item.hwndItem == discord_ ||
            item.hwndItem == openDiagnostics_;

        if (footerLink)
        {
            Fill(
                context,
                rectangle,
                hovered ? theme::PanelRaised : theme::Header);
            wchar_t label[128] = {};
            GetWindowTextW(item.hwndItem, label, std::size(label));
            Text(
                context,
                smallFont_,
                pressed ? theme::GoldPressed :
                    hovered ? theme::GoldHover : theme::Gold,
                label,
                rectangle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        COLORREF background = theme::Panel;
        COLORREF border = theme::Border;
        COLORREF foreground = theme::Ivory;
        if (checkbox)
        {
            background = pressed || hovered
                ? theme::PanelRaised : theme::Window;
            border = hovered ? theme::Border : theme::Divider;
        }
        else if (navigation)
        {
            background = selectedNavigation || hovered
                ? theme::PanelRaised : theme::Navigation;
            border = background;
            foreground = selectedNavigation || hovered
                ? hovered ? theme::GoldHover : theme::Gold
                : theme::Ivory;
        }
        else if (primary)
        {
            background = pressed ? theme::GoldPressed :
                hovered ? theme::GoldHover : theme::Gold;
            border = background;
            foreground = theme::Window;
        }
        else if (pressed || hovered)
        {
            background = theme::PanelRaised;
            border = hovered ? theme::Gold : border;
        }
        if (disabled)
        {
            foreground = theme::Muted;
            border = theme::Divider;
        }

        HBRUSH brush = CreateSolidBrush(background);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HBRUSH previousBrush = static_cast<HBRUSH>(SelectObject(context, brush));
        HPEN previousPen = static_cast<HPEN>(SelectObject(context, pen));
        RoundRect(
            context,
            rectangle.left,
            rectangle.top,
            rectangle.right,
            rectangle.bottom,
            navigation ? 0 : Scale(5),
            navigation ? 0 : Scale(5));
        if (selectedNavigation)
        {
            RECT accent = rectangle;
            accent.right = accent.left + Scale(4);
            Fill(context, accent, theme::Gold);
        }

        wchar_t label[128] = {};
        GetWindowTextW(item.hwndItem, label, std::size(label));
        if (checkbox)
        {
            const bool checked = item.hwndItem == showConsole_
                ? showConsoleChecked_ : generateLogsChecked_;
            const int boxSize = Scale(18);
            RECT box = {
                rectangle.left + Scale(13),
                rectangle.top + (rectangle.bottom - rectangle.top - boxSize) / 2,
                rectangle.left + Scale(13) + boxSize,
                rectangle.top + (rectangle.bottom - rectangle.top + boxSize) / 2};
            HBRUSH boxBrush = CreateSolidBrush(
                checked ? theme::Gold : theme::Edit);
            HPEN boxPen = CreatePen(
                PS_SOLID, 1, checked ? theme::Gold : theme::Muted);
            HBRUSH oldBoxBrush = static_cast<HBRUSH>(
                SelectObject(context, boxBrush));
            HPEN oldBoxPen = static_cast<HPEN>(SelectObject(context, boxPen));
            RoundRect(
                context,
                box.left,
                box.top,
                box.right,
                box.bottom,
                Scale(3),
                Scale(3));
            if (checked)
            {
                HPEN tickPen = CreatePen(PS_SOLID, Scale(2), theme::Window);
                HPEN oldTickPen = static_cast<HPEN>(
                    SelectObject(context, tickPen));
                MoveToEx(context, box.left + Scale(4), box.top + Scale(9), nullptr);
                LineTo(context, box.left + Scale(8), box.bottom - Scale(4));
                LineTo(context, box.right - Scale(3), box.top + Scale(4));
                SelectObject(context, oldTickPen);
                DeleteObject(tickPen);
            }
            SelectObject(context, oldBoxPen);
            SelectObject(context, oldBoxBrush);
            DeleteObject(boxPen);
            DeleteObject(boxBrush);

            rectangle.left += Scale(44);
            Text(context, bodyFont_, foreground, label, rectangle,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else if (navigation)
        {
            rectangle.left += Scale(24);
            Text(context, bodyFont_, foreground, label, rectangle,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            Text(context, bodyFont_, foreground, label, rectangle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(context, previousPen);
        SelectObject(context, previousBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }
}
