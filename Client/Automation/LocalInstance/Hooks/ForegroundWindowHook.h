#pragma once

#include "Automation/LocalInstance/Native/ForegroundWindowImport.h"
#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <atomic>

namespace fable::automation::local_instance
{
    // The retail engine gates simulation on its GetForegroundWindow import.
    // Local two-process acceptance still uses real Windows/DirectInput focus,
    // but this executable-scoped IAT route keeps the unfocused peer's actor
    // and animation updates alive.
    class ForegroundWindowHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            HWND localWindow,
            const core::Diagnostics& diagnostics) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned long RedirectCount() const noexcept;

    private:
        static HWND WINAPI GetForegroundWindowRedirect();

        static ForegroundWindowHook* active_;

        native::ForegroundWindowImport::Function original_ = nullptr;
        HWND localWindow_ = nullptr;
        std::atomic_ulong redirectCount_{0};
        bool installed_ = false;
    };
}
