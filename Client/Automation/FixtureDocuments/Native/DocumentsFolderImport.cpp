#include "DocumentsFolderImport.h"

namespace fable::automation::fixture_documents::native
{
    bool DocumentsFolderImport::Resolve(
        HMODULE gameModule,
        Resolved& resolved) noexcept
    {
        resolved = {};
        if (gameModule == nullptr)
        {
            return false;
        }

        const HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        const auto importedFunction = shell32 == nullptr
            ? nullptr
            : reinterpret_cast<Function>(
                GetProcAddress(shell32, "SHGetFolderPathW"));
        if (importedFunction == nullptr)
        {
            return false;
        }

        auto* const slot = reinterpret_cast<Function*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + SlotRva);
        Function current = nullptr;
        __try
        {
            current = *slot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (current != importedFunction)
        {
            return false;
        }

        resolved.slot = slot;
        resolved.importedFunction = importedFunction;
        return true;
    }
}
