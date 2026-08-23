#include "DocumentsFolderRedirectHook.h"

#include <ShlObj.h>

#include <cstdio>
#include <string>

namespace
{
    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || value[0] == L'\0')
        {
            return {};
        }

        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 1)
        {
            return {};
        }

        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            required,
            nullptr,
            nullptr);
        result.resize(static_cast<std::size_t>(required - 1));
        return result;
    }
}

namespace fable::automation::fixture_documents
{
    DocumentsFolderRedirectHook* DocumentsFolderRedirectHook::active_ = nullptr;

    bool DocumentsFolderRedirectHook::Install(
        HMODULE gameModule,
        const wchar_t* fixtureDocumentsPath,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            diagnostics_ = diagnostics;
            ReportInstalled();
            ReportRedirected();
            return true;
        }
        diagnostics_ = diagnostics;
        if (fixtureDocumentsPath == nullptr || fixtureDocumentsPath[0] == L'\0')
        {
            return true;
        }
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another fixture Documents redirect is already active.");
            return false;
        }

        native::DocumentsFolderImport::Resolved imported = {};
        if (!native::DocumentsFolderImport::Resolve(gameModule, imported))
        {
            diagnostics_.Log(
                "Hook: SHGetFolderPathW import definition validation failed; the executable IAT drifted.");
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
                "Hook: fixture Documents redirect could not change IAT protection.");
            return false;
        }

        fixtureDocumentsPath_ = fixtureDocumentsPath;
        original_ = imported.importedFunction;
        slot_ = imported.slot;
        active_ = this;
        *imported.slot = &DocumentsFolderRedirectHook::Redirect;

        DWORD discardedProtection = 0;
        if (!VirtualProtect(
                imported.slot,
                sizeof(*imported.slot),
                previousProtection,
                &discardedProtection))
        {
            diagnostics_.Log(
                "Hook: fixture Documents redirect installed, but IAT protection restoration failed.");
        }

        installed_ = true;
        ReportInstalled();
        return true;
    }

    void DocumentsFolderRedirectHook::Shutdown() noexcept
    {
        if (slot_ != nullptr && original_ != nullptr)
        {
            DWORD protection = 0;
            if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection))
            {
                if (*slot_ == &DocumentsFolderRedirectHook::Redirect)
                {
                    *slot_ = original_;
                }
                DWORD discarded = 0;
                VirtualProtect(slot_, sizeof(*slot_), protection, &discarded);
                FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(*slot_));
            }
        }
        if (active_ == this) active_ = nullptr;
        installed_ = false;
        slot_ = nullptr;
        original_ = nullptr;
        diagnostics_ = {};
    }

    void DocumentsFolderRedirectHook::ReportInstalled()
    {
        if ((diagnostics_.log == nullptr && diagnostics_.event == nullptr) ||
            readyReported_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        const std::string path = WideToUtf8(fixtureDocumentsPath_.c_str());
        char message[512] = {};
        std::snprintf(
            message,
            sizeof(message),
            "Hook: fixture Documents redirect installed; original=%p replacement=%p root=%s.",
            reinterpret_cast<void*>(original_),
            &DocumentsFolderRedirectHook::Redirect,
            path.c_str());
        diagnostics_.Log(message);
        diagnostics_.Event("FixtureDocumentsRedirectReady", path.c_str());
    }

    void DocumentsFolderRedirectHook::ReportRedirected()
    {
        if (!redirectOccurred_.load(std::memory_order_acquire) ||
            (diagnostics_.log == nullptr && diagnostics_.event == nullptr) ||
            redirectReported_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        const std::string detail = WideToUtf8(fixtureDocumentsPath_.c_str());
        diagnostics_.Log(
            "Fixture: redirected CSIDL_PERSONAL to the isolated run directory.");
        diagnostics_.Event("FixtureDocumentsRedirected", detail.c_str());
    }

    bool DocumentsFolderRedirectHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this && original_ != nullptr;
    }

    HRESULT WINAPI DocumentsFolderRedirectHook::Redirect(
        HWND owner,
        int folder,
        HANDLE token,
        DWORD flags,
        LPWSTR path)
    {
        DocumentsFolderRedirectHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return E_FAIL;
        }

        const HRESULT result = hook->original_(owner, folder, token, flags, path);
        if (FAILED(result) ||
            path == nullptr ||
            (folder & 0xFF) != CSIDL_PERSONAL ||
            hook->fixtureDocumentsPath_.empty())
        {
            return result;
        }

        if (wcscpy_s(
                path,
                MAX_PATH,
                hook->fixtureDocumentsPath_.c_str()) != 0)
        {
            hook->diagnostics_.Event(
                "ClientFailed",
                "fixture-documents-path-exceeds-SHGetFolderPath-contract");
            return E_INVALIDARG;
        }

        hook->redirectOccurred_.store(true, std::memory_order_release);
        hook->ReportRedirected();
        return result;
    }
}
