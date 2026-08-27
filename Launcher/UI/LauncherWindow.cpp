#include "LauncherWindow.h"

#include "LauncherTheme.h"
#include "../Runtime/GameProcess.h"

#include <CommCtrl.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace fable::launcher::ui
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"AlbionTogetherLauncherWindow";
        constexpr UINT DiagnosticsCompleteMessage = WM_APP + 1;
        constexpr UINT_PTR GameProcessTimerId = 1;
    }

    LauncherWindow::LauncherWindow()
        : settings_(workflow_.LoadSettings())
    {
    }

    LauncherWindow::~LauncherWindow()
    {
        if (titleFont_ != nullptr) DeleteObject(titleFont_);
        if (headingFont_ != nullptr) DeleteObject(headingFont_);
        if (bodyFont_ != nullptr) DeleteObject(bodyFont_);
        if (smallFont_ != nullptr) DeleteObject(smallFont_);
        if (windowBrush_ != nullptr) DeleteObject(windowBrush_);
        if (panelBrush_ != nullptr) DeleteObject(panelBrush_);
        if (editBrush_ != nullptr) DeleteObject(editBrush_);
    }

    int LauncherWindow::Run(HINSTANCE instance)
    {
        if (!Create(instance))
        {
            MessageBoxW(
                nullptr,
                L"The AlbionTogether launcher window could not be created.",
                L"AlbionTogether",
                MB_OK | MB_ICONERROR);
            return 1;
        }
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        BeginDiagnostics(false);

        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    bool LauncherWindow::Create(HINSTANCE instance)
    {
        INITCOMMONCONTROLSEX controls = {
            sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.lpszClassName = WindowClassName;
        if (RegisterClassExW(&windowClass) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        window_ = CreateWindowExW(
            0,
            WindowClassName,
            L"AlbionTogether",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1'180,
            660,
            nullptr,
            nullptr,
            instance,
            this);
        if (window_ == nullptr)
        {
            return false;
        }

        dpi_ = GetDpiForWindow(window_);
        RECT desired = {0, 0, Scale(1'180), Scale(660)};
        AdjustWindowRectExForDpi(
            &desired,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            FALSE,
            0,
            dpi_);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(
            MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const int desiredWidth = desired.right - desired.left;
        const int desiredHeight = desired.bottom - desired.top;
        const int x = monitor.rcWork.left +
            ((monitor.rcWork.right - monitor.rcWork.left) - desiredWidth) / 2;
        const int y = monitor.rcWork.top +
            ((monitor.rcWork.bottom - monitor.rcWork.top) - desiredHeight) / 2;
        SetWindowPos(
            window_, nullptr, x, y, desiredWidth, desiredHeight,
            SWP_NOACTIVATE | SWP_NOZORDER);

        const BOOL dark = TRUE;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        CreateFonts();
        windowBrush_ = CreateSolidBrush(theme::Window);
        panelBrush_ = CreateSolidBrush(theme::Panel);
        editBrush_ = CreateSolidBrush(theme::Edit);
        CreateControls();
        LoadControlsFromSettings();
        LayoutControls();
        ShowPage(Page::Play);
        UpdateGameRunningState();
        SetTimer(window_, GameProcessTimerId, 1'000, nullptr);
        return true;
    }

    LRESULT CALLBACK LauncherWindow::WindowProcedure(
        HWND window,
        const UINT message,
        const WPARAM wparam,
        const LPARAM lparam)
    {
        LauncherWindow* instance = reinterpret_cast<LauncherWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            instance = static_cast<LauncherWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
            instance->window_ = window;
        }
        return instance != nullptr
            ? instance->HandleMessage(message, wparam, lparam)
            : DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT LauncherWindow::HandleMessage(
        const UINT message,
        const WPARAM wparam,
        const LPARAM lparam)
    {
        switch (message)
        {
        case WM_COMMAND:
            HandleCommand(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_DRAWITEM:
            DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            LayoutControls();
            InvalidateRect(window_, nullptr, TRUE);
            return 0;
        case WM_TIMER:
            if (wparam == GameProcessTimerId)
            {
                UpdateGameRunningState();
                return 0;
            }
            return DefWindowProcW(window_, message, wparam, lparam);
        case WM_DPICHANGED:
        {
            dpi_ = HIWORD(wparam);
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window_, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            LayoutControls();
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize.x = Scale(1'120);
            limits->ptMinTrackSize.y = Scale(690);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            HDC context = reinterpret_cast<HDC>(wparam);
            SetTextColor(context, theme::Ivory);
            SetBkMode(context, TRANSPARENT);
            return reinterpret_cast<LRESULT>(windowBrush_);
        }
        case WM_CTLCOLOREDIT:
        {
            HDC context = reinterpret_cast<HDC>(wparam);
            SetTextColor(context, theme::Ivory);
            SetBkColor(context, theme::Edit);
            return reinterpret_cast<LRESULT>(editBrush_);
        }
        case DiagnosticsCompleteMessage:
            CompleteDiagnostics(
                reinterpret_cast<diagnostics::LauncherDiagnosticsReport*>(lparam));
            return 0;
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            KillTimer(window_, GameProcessTimerId);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wparam, lparam);
        }
    }

    int RunLauncherUi(HINSTANCE instance)
    {
        LauncherWindow window;
        return window.Run(instance);
    }
}
