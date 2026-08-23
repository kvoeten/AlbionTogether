#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/GameThread/Native/PeekMessageImport.h"

#include <Windows.h>

namespace fable::core::game_thread
{
    class GameThreadIdleHook final
    {
    public:
        using IdleCallback = void(*)();

        bool Install(
            HMODULE gameModule,
            DWORD gameThreadId,
            const core::Diagnostics& diagnostics,
            IdleCallback idleCallback);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static BOOL WINAPI PeekMessage(
            LPMSG message,
            HWND window,
            UINT minimumMessage,
            UINT maximumMessage,
            UINT removeMessage);

        static GameThreadIdleHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::PeekMessageImport::Function original_ = nullptr;
        native::PeekMessageImport::Function* slot_ = nullptr;
        IdleCallback idleCallback_ = nullptr;
        DWORD gameThreadId_ = 0;
        bool installed_ = false;
    };
}
