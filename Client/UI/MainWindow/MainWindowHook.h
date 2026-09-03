#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

namespace fable::ui
{
    struct MainWindowCallbacks
    {
        void (*onTimer)() = nullptr;
        void (*onFocusChanged)(bool focused) = nullptr;
        void (*onDestroyed)() = nullptr;
        bool (*onCloseRequested)() = nullptr;
        void (*onNumberRowOne)(bool down, bool shiftPressed) = nullptr;
        bool (*onDeveloperToolsToggle)() = nullptr;
        bool (*onWindowMessage)(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam) = nullptr;
    };

    class MainWindowHook final
    {
    public:
        HWND WaitForWindow(
            const core::Diagnostics& diagnostics,
            HANDLE cancelEvent = nullptr);
        bool Install(
            HWND window,
            UINT_PTR timerId,
            UINT timerIntervalMilliseconds,
            bool captureNumberRowOne,
            bool preserveBackgroundRendering,
            const MainWindowCallbacks& callbacks,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] HWND Window() const noexcept;
        [[nodiscard]] DWORD ThreadId() const noexcept;

    private:
        static LRESULT CALLBACK WindowProcedure(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam);
        LRESULT HandleMessage(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam);

        static MainWindowHook* active_;
        HWND window_ = nullptr;
        DWORD threadId_ = 0;
        WNDPROC originalProcedure_ = nullptr;
        UINT_PTR timerId_ = 0;
        bool captureNumberRowOne_ = false;
        bool developerToolsKeyCaptured_ = false;
        bool preserveBackgroundRendering_ = false;
        MainWindowCallbacks callbacks_ = {};
        core::Diagnostics diagnostics_ = {};
    };
}
