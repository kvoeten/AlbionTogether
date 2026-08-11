#pragma once

#include "Automation/LocalInstance/Native/UnrealSingletonImport.h"
#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <atomic>
#include <string>

namespace fable::automation::local_instance
{
    class UnrealSingletonHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const wchar_t* sessionId,
            const wchar_t* instanceId) noexcept;
        void Report(const core::Diagnostics& diagnostics) const;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] const std::wstring& NamespacedMutex() const noexcept;
        [[nodiscard]] unsigned long RedirectCount() const noexcept;

    private:
        static HANDLE WINAPI CreateMutexRedirect(
            LPSECURITY_ATTRIBUTES attributes,
            BOOL initialOwner,
            LPCWSTR name);

        static UnrealSingletonHook* active_;

        native::UnrealSingletonImport::Function original_ = nullptr;
        std::wstring namespacedMutex_;
        std::atomic_ulong redirectCount_{0};
        bool installed_ = false;
    };
}
