#include "GameThreadIdleHook.h"

#include <cstdio>

namespace fable::core::game_thread
{
    GameThreadIdleHook* GameThreadIdleHook::active_ = nullptr;

    bool GameThreadIdleHook::Install(
        HMODULE gameModule,
        DWORD gameThreadId,
        const core::Diagnostics& diagnostics,
        IdleCallback idleCallback)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another game-thread idle hook is already active.");
            return false;
        }
        if (gameThreadId == 0 || idleCallback == nullptr)
        {
            diagnostics_.Log(
                "Hook: game-thread idle boundary requires a thread and callback.");
            return false;
        }

        native::PeekMessageImport::Resolved imported = {};
        if (!native::PeekMessageImport::Resolve(gameModule, imported))
        {
            diagnostics_.Log(
                "Hook: PeekMessageW import definition validation failed; the executable IAT drifted.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                imported.slot,
                sizeof(*imported.slot),
                PAGE_READWRITE,
                &previousProtection))
        {
            diagnostics_.Log(
                "Hook: game-thread idle boundary could not change IAT protection.");
            return false;
        }

        original_ = imported.importedFunction;
        slot_ = imported.slot;
        idleCallback_ = idleCallback;
        gameThreadId_ = gameThreadId;
        active_ = this;
        *imported.slot = &GameThreadIdleHook::PeekMessage;

        DWORD discardedProtection = 0;
        if (!VirtualProtect(
                imported.slot,
                sizeof(*imported.slot),
                previousProtection,
                &discardedProtection))
        {
            diagnostics_.Log(
                "Hook: game-thread idle boundary installed, but IAT protection restoration failed.");
        }

        installed_ = true;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "PeekMessageW idle boundary thread=%lu slot=%p original=%p replacement=%p",
            static_cast<unsigned long>(gameThreadId_),
            imported.slot,
            reinterpret_cast<void*>(original_),
            &GameThreadIdleHook::PeekMessage);
        diagnostics_.Log("Hook: game-thread idle boundary installed.");
        diagnostics_.Event("GameThreadQueueReady", detail);
        return true;
    }

    void GameThreadIdleHook::Shutdown() noexcept
    {
        if (slot_ != nullptr && original_ != nullptr)
        {
            DWORD protection = 0;
            if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection))
            {
                if (*slot_ == &GameThreadIdleHook::PeekMessage) *slot_ = original_;
                DWORD discarded = 0;
                VirtualProtect(slot_, sizeof(*slot_), protection, &discarded);
                FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(*slot_));
            }
        }
        if (active_ == this) active_ = nullptr;
        installed_ = false;
        slot_ = nullptr;
        original_ = nullptr;
        idleCallback_ = nullptr;
        gameThreadId_ = 0;
    }

    bool GameThreadIdleHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this && original_ != nullptr;
    }

    BOOL WINAPI GameThreadIdleHook::PeekMessage(
        LPMSG message,
        HWND window,
        UINT minimumMessage,
        UINT maximumMessage,
        UINT removeMessage)
    {
        GameThreadIdleHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return FALSE;
        }

        const BOOL result = hook->original_(
            message,
            window,
            minimumMessage,
            maximumMessage,
            removeMessage);
        if (result == FALSE &&
            GetCurrentThreadId() == hook->gameThreadId_ &&
            hook->idleCallback_ != nullptr)
        {
            hook->idleCallback_();
        }
        return result;
    }
}
