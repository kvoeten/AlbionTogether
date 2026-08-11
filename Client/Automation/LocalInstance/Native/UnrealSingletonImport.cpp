#include "UnrealSingletonImport.h"

namespace fable::automation::local_instance::native
{
    bool UnrealSingletonImport::Resolve(
        HMODULE gameModule,
        Resolved& resolved) noexcept
    {
        resolved = {};
        if (gameModule == nullptr)
        {
            return false;
        }

        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const auto importedFunction = kernel32 == nullptr
            ? nullptr
            : reinterpret_cast<Function>(
                GetProcAddress(kernel32, "CreateMutexW"));
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
