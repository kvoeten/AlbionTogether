#pragma once

#include "Automation/FixtureDocuments/Native/DocumentsFolderImport.h"
#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>
#include <string>

namespace fable::automation::fixture_documents
{
    class DocumentsFolderRedirectHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const wchar_t* fixtureDocumentsPath,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        void ReportInstalled();
        void ReportRedirected();

        static HRESULT WINAPI Redirect(
            HWND owner,
            int folder,
            HANDLE token,
            DWORD flags,
            LPWSTR path);

        static DocumentsFolderRedirectHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::DocumentsFolderImport::Function original_ = nullptr;
        core::hooking::CodePatch patch_;
        std::wstring fixtureDocumentsPath_;
        std::atomic_bool redirectOccurred_{false};
        std::atomic_bool readyReported_{false};
        std::atomic_bool redirectReported_{false};
    };
}
