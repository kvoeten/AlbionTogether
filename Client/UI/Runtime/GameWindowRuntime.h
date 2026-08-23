#pragma once

namespace fable::ui::runtime
{
    void OnGameWindowTimer();
    void OnGameWindowFocusChanged(bool focused);
    void OnGameWindowDestroyed();
    bool OnGameWindowCloseRequested();
    void OnGameWindowNumberRowOne(bool down, bool shiftPressed);
}

