#pragma once

#include "Core/Bootstrap/ClientRuntimeState.h"

namespace fable::automation::transform_probe
{
    void ShutdownTransformProbe() noexcept;
    void PollHotkey();
    void ObserveOneKeyState(bool isDown, bool shiftPressed, const char* source);
    void OnGameWindowNumberRowOne(bool down, bool shiftPressed);
}
