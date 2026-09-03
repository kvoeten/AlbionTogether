#pragma once

#include <Windows.h>

namespace fable::ui::runtime
{
    void OnGameWindowTimer();
    void OnGameWindowFocusChanged(bool focused);
    void OnGameWindowDestroyed();
    bool OnGameWindowCloseRequested();
    void OnGameWindowNumberRowOne(bool down, bool shiftPressed);
    bool OnGameWindowDeveloperToolsToggle();
    bool OnGameWindowMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);
}

