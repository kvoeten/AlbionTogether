#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

namespace fable::game::persistence
{
    // Observes completion of the retail save-bundle section sequence. The
    // original loader remains responsible for parsing every native section;
    // the sink runs only after FACTIONS has returned.
    class GameStateSectionLoadHook final
    {
    public:
        using CompletionSink = void (*)(void* context) noexcept;

        bool Install(
            HMODULE gameModule,
            CompletionSink sink,
            void* context,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept
        {
            return detour_.IsInstalled();
        }

    private:
        using LoadSections = void(__thiscall*)(void* bundle, void* reader);

        static void __fastcall LoadSectionsObserved(
            void* bundle,
            void* unused,
            void* reader);
        void ObserveLoadSections(void* bundle, void* reader);

        static GameStateSectionLoadHook* active_;
        core::Diagnostics diagnostics_ = {};
        CompletionSink sink_ = nullptr;
        void* sinkContext_ = nullptr;
        LoadSections original_ = nullptr;
        core::hooking::InlineHook detour_;
    };
}
