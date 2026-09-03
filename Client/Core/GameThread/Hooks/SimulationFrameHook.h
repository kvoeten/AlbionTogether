#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

namespace fable::core::game_thread
{
    // One process-lifetime hook, installed while the launcher holds the game
    // suspended. The DLL and trampoline stay resident: runtime shutdown does
    // not imply that native threads have stopped executing the patched code.
    class SimulationFrameHook final
    {
    public:
        using Sink = void (*)(void*);
        static bool InstallBeforeResume(HMODULE gameModule) noexcept;
        static bool Enable(Sink sink, void* context,
            const Diagnostics& diagnostics) noexcept;

        // Detaches the consumer and waits for its in-flight callback, without
        // unpatching/freeing executable memory. Do not call from the sink.
        static void Disable() noexcept;

    private:
        static void __fastcall Observe(void* application, void*);
    };
}
