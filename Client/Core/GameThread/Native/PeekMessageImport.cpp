#include "PeekMessageImport.h"

namespace fable::core::game_thread::native
{
    bool PeekMessageImport::Resolve(
        HMODULE gameModule,
        Resolved& resolved) noexcept
    {
        resolved = {};
        if (gameModule == nullptr)
        {
            return false;
        }

        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto importedFunction = user32 == nullptr
            ? nullptr
            : reinterpret_cast<Function>(GetProcAddress(user32, "PeekMessageW"));
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
