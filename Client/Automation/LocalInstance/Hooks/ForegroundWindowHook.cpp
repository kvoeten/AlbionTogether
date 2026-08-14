#include "ForegroundWindowHook.h"

#include <cstdio>

namespace fable::automation::local_instance
{
    ForegroundWindowHook* ForegroundWindowHook::active_ = nullptr;

    bool ForegroundWindowHook::Install(
        HMODULE gameModule,
        HWND localWindow,
        const core::Diagnostics& diagnostics) noexcept
    {
        if (IsInstalled())
        {
            return true;
        }
        if (gameModule == nullptr || localWindow == nullptr ||
            active_ != nullptr)
        {
            return false;
        }

        native::ForegroundWindowImport::Resolved imported = {};
        if (!native::ForegroundWindowImport::Resolve(gameModule, imported))
        {
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                imported.slot,
                sizeof(*imported.slot),
                PAGE_READWRITE,
                &previousProtection))
        {
            return false;
        }

        original_ = imported.importedFunction;
        localWindow_ = localWindow;
        active_ = this;
        *imported.slot = &ForegroundWindowHook::GetForegroundWindowRedirect;

        DWORD discardedProtection = 0;
        VirtualProtect(
            imported.slot,
            sizeof(*imported.slot),
            previousProtection,
            &discardedProtection);
        FlushInstructionCache(
            GetCurrentProcess(), imported.slot, sizeof(*imported.slot));
        installed_ = true;

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "window=%p slot=%p original=%p replacement=%p retail_foreground_simulation_predicate_rva=0x%08X",
            localWindow_,
            imported.slot,
            reinterpret_cast<void*>(original_),
            &ForegroundWindowHook::GetForegroundWindowRedirect,
            0x01B9F0A0u);
        diagnostics.Event("LocalInstanceForegroundWindowPinned", detail);
        return true;
    }

    bool ForegroundWindowHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this && original_ != nullptr &&
            localWindow_ != nullptr;
    }

    unsigned long ForegroundWindowHook::RedirectCount() const noexcept
    {
        return redirectCount_.load(std::memory_order_acquire);
    }

    HWND WINAPI ForegroundWindowHook::GetForegroundWindowRedirect()
    {
        ForegroundWindowHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return nullptr;
        }
        const HWND actual = hook->original_();
        if (hook->localWindow_ != nullptr && actual != hook->localWindow_)
        {
            hook->redirectCount_.fetch_add(1, std::memory_order_acq_rel);
            return hook->localWindow_;
        }
        return actual;
    }
}
