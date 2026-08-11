#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::automation::fixture_documents::native
{
    struct DocumentsFolderImport final
    {
        using Function = HRESULT(WINAPI*)(
            HWND owner,
            int folder,
            HANDLE token,
            DWORD flags,
            LPWSTR path);

        static constexpr std::uintptr_t SlotRva = 0x0265BACC;

        struct Resolved final
        {
            Function* slot = nullptr;
            Function importedFunction = nullptr;
        };

        static bool Resolve(HMODULE gameModule, Resolved& resolved) noexcept;
    };
}
