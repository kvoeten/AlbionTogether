#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"
#include "ScriptTypes.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::native
{
    class GameInterfaceAccess final
    {
    public:
        bool Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics);

        [[nodiscard]] GameScriptInterface* Resolve() const;
        [[nodiscard]] void* ResolveFunction(
            std::size_t vtableIndex,
            std::uintptr_t expectedRva) const;
        [[nodiscard]] HMODULE GameModule() const noexcept;

    private:
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
