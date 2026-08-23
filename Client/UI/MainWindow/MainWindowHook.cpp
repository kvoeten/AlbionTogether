#include "MainWindowHook.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <string>

namespace
{
    constexpr LONG MinimumWindowWidth = 640;
    constexpr LONG MinimumWindowHeight = 360;

    struct WindowSearch
    {
        HWND bestWindow = nullptr;
        unsigned long long bestArea = 0;
        unsigned int processWindowCount = 0;
        unsigned int visibleWindowCount = 0;
        unsigned int eligibleWindowCount = 0;
        LONG minimumWidth = MinimumWindowWidth;
        LONG minimumHeight = MinimumWindowHeight;
    };

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
    {
        auto& search = *reinterpret_cast<WindowSearch*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId())
        {
            return TRUE;
        }
        ++search.processWindowCount;
        if (!IsWindowVisible(window))
        {
            return TRUE;
        }
        ++search.visibleWindowCount;

        wchar_t className[128] = {};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (std::wcscmp(className, L"#32770") == 0)
        {
            return TRUE;
        }

        RECT client = {};
        if (!GetClientRect(window, &client))
        {
            return TRUE;
        }
        const LONG width = client.right - client.left;
        const LONG height = client.bottom - client.top;
        if (width < search.minimumWidth || height < search.minimumHeight)
        {
            return TRUE;
        }
        ++search.eligibleWindowCount;
        const auto area = static_cast<unsigned long long>(width) *
            static_cast<unsigned long long>(height);
        if (area > search.bestArea)
        {
            search.bestArea = area;
            search.bestWindow = window;
        }
        return TRUE;
    }
}

namespace fable::ui
{
    MainWindowHook* MainWindowHook::active_ = nullptr;

    HWND MainWindowHook::WaitForWindow(
        const core::Diagnostics& diagnostics,
        HANDLE cancelEvent)
    {
        for (unsigned int attempt = 0;; ++attempt)
        {
            if (cancelEvent != nullptr &&
                WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
            {
                diagnostics.Log("Startup: game-window wait cancelled.");
                return nullptr;
            }
            WindowSearch search;
            if (attempt >= 120)
            {
                search.minimumWidth = 320;
                search.minimumHeight = 200;
            }
            EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
            if (search.bestWindow != nullptr)
            {
                wchar_t title[256] = {};
                wchar_t className[128] = {};
                GetWindowTextW(search.bestWindow, title, static_cast<int>(std::size(title)));
                GetClassNameW(search.bestWindow, className, static_cast<int>(std::size(className)));
                const DWORD threadId = GetWindowThreadProcessId(search.bestWindow, nullptr);
                char message[768] = {};
                std::snprintf(
                    message,
                    sizeof(message),
                    "Startup: selected game window hwnd=%p tid=%lu class=%s title=%s area=%llu.",
                    search.bestWindow,
                    static_cast<unsigned long>(threadId),
                    WideToUtf8(className).c_str(),
                    WideToUtf8(title).c_str(),
                    search.bestArea);
                diagnostics.Log(message);
                return search.bestWindow;
            }
            if (attempt % 20 == 0)
            {
                char message[320] = {};
                std::snprintf(
                    message,
                    sizeof(message),
                    "Startup: waiting for game window; process=%u visible=%u eligible=%u minimum=%ldx%ld.",
                    search.processWindowCount,
                    search.visibleWindowCount,
                    search.eligibleWindowCount,
                    search.minimumWidth,
                    search.minimumHeight);
                diagnostics.Log(message);
            }
            if (cancelEvent != nullptr)
            {
                if (WaitForSingleObject(cancelEvent, 250) == WAIT_OBJECT_0)
                {
                    diagnostics.Log("Startup: game-window wait cancelled.");
                    return nullptr;
                }
            }
            else
            {
                Sleep(250);
            }
        }
    }

    bool MainWindowHook::Install(
        HWND window,
        UINT_PTR timerId,
        UINT timerIntervalMilliseconds,
        bool captureNumberRowOne,
        bool preserveBackgroundRendering,
        const MainWindowCallbacks& callbacks,
        const core::Diagnostics& diagnostics)
    {
        if (window == nullptr || active_ != nullptr)
        {
            return false;
        }
        window_ = window;
        threadId_ = GetWindowThreadProcessId(window_, nullptr);
        timerId_ = timerId;
        captureNumberRowOne_ = captureNumberRowOne;
        preserveBackgroundRendering_ = preserveBackgroundRendering;
        callbacks_ = callbacks;
        diagnostics_ = diagnostics;
        active_ = this;

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtrW(
            window_,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(WindowProcedure));
        if (previous == 0 && GetLastError() != ERROR_SUCCESS)
        {
            active_ = nullptr;
            return false;
        }
        originalProcedure_ = reinterpret_cast<WNDPROC>(previous);

        char hookMessage[256] = {};
        std::snprintf(
            hookMessage,
            sizeof(hookMessage),
            "Hook: MainWindow procedure installed; replacement=%p original=%p.",
            WindowProcedure,
            originalProcedure_);
        diagnostics_.Log(hookMessage);

        const UINT_PTR installed = SetTimer(
            window_, timerId_, timerIntervalMilliseconds, nullptr);
        if (installed == 0)
        {
            diagnostics_.Log("Hook: MainWindow timer installation failed; window messages remain active.");
        }
        else
        {
            char timerMessage[192] = {};
            std::snprintf(
                timerMessage,
                sizeof(timerMessage),
                "Hook: MainWindow timer installed; id=%llu interval_ms=%u.",
                static_cast<unsigned long long>(installed),
                timerIntervalMilliseconds);
            diagnostics_.Log(timerMessage);
        }
        return true;
    }

    void MainWindowHook::Shutdown() noexcept
    {
        if (window_ != nullptr)
        {
            if (timerId_ != 0) KillTimer(window_, timerId_);
            if (originalProcedure_ != nullptr && IsWindow(window_))
            {
                SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProcedure_));
            }
        }
        if (active_ == this) active_ = nullptr;
        window_ = nullptr;
        threadId_ = 0;
        originalProcedure_ = nullptr;
        timerId_ = 0;
        callbacks_ = {};
        diagnostics_ = {};
    }

    LRESULT CALLBACK MainWindowHook::WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        return active_ != nullptr
            ? active_->HandleMessage(window, message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT MainWindowHook::HandleMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        if (message == WM_TIMER && wParam == timerId_ && callbacks_.onTimer != nullptr)
        {
            callbacks_.onTimer();
            return 0;
        }
        if (message == WM_SETFOCUS && callbacks_.onFocusChanged != nullptr)
        {
            callbacks_.onFocusChanged(true);
        }
        else if (message == WM_KILLFOCUS && callbacks_.onFocusChanged != nullptr)
        {
            callbacks_.onFocusChanged(false);
        }
        else if (message == WM_DESTROY && callbacks_.onDestroyed != nullptr)
        {
            callbacks_.onDestroyed();
        }
        else if (message == WM_CLOSE && callbacks_.onCloseRequested != nullptr &&
            callbacks_.onCloseRequested())
        {
            return 0;
        }

        // UE3's standalone window treats deactivation as permission to stop
        // presenting. For the launcher's same-machine multiplayer harness,
        // keep the engine's activation state intact while Windows moves real
        // keyboard focus to the peer window. This affects only local test
        // instances; normal single-process and real network launches retain
        // the retail focus behavior.
        if (preserveBackgroundRendering_ &&
            (message == WM_KILLFOCUS ||
                (message == WM_ACTIVATEAPP && wParam == FALSE) ||
                (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)))
        {
            return 0;
        }

        if (captureNumberRowOne_ && message == WM_KEYDOWN &&
            wParam == static_cast<WPARAM>('1'))
        {
            if (callbacks_.onNumberRowOne != nullptr)
            {
                callbacks_.onNumberRowOne(
                    true,
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            }
            return 0;
        }
        if (captureNumberRowOne_ && message == WM_KEYUP &&
            wParam == static_cast<WPARAM>('1'))
        {
            if (callbacks_.onNumberRowOne != nullptr)
            {
                callbacks_.onNumberRowOne(false, false);
            }
            return 0;
        }
        if (captureNumberRowOne_ && message == WM_CHAR &&
            (wParam == static_cast<WPARAM>('1') || wParam == static_cast<WPARAM>('!')))
        {
            return 0;
        }

        return originalProcedure_ != nullptr
            ? CallWindowProcW(originalProcedure_, window, message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    HWND MainWindowHook::Window() const noexcept
    {
        return window_;
    }

    DWORD MainWindowHook::ThreadId() const noexcept
    {
        return threadId_;
    }
}
