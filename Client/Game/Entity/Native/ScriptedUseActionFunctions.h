#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::native
{
    // Current-build access to CTCActionUseScriptedHook. This executes the
    // component's own scripted-use callback; it is distinct from the generic
    // GameScriptInterface OpenDoor mutation, which does not run region-entry
    // hooks.
    struct ScriptedUseActionFunctions final
    {
        static constexpr std::uintptr_t VtableRva = 0x02AEC7A4;
        static constexpr std::uintptr_t ExecuteRva = 0x0192D2E0;
        static constexpr std::size_t ExecuteVtableSlot = 28;
        static constexpr std::size_t PendingUseOffset = 0x0F;
        static constexpr std::size_t TeleportToRegionEntranceOffset = 0x10;

        static bool Execute(
            void* nativeThing,
            HMODULE gameModule,
            bool requireRegionEntrance,
            const char*& failure) noexcept;
    };
}
