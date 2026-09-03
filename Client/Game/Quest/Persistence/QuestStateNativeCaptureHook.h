#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Native/ScriptTypes.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::persistence
{
    // Bridges the two validated CQuestManager persistence seams. Hosts observe
    // SaveGameState after retail serialization; guests replace only the
    // manager parser passed to LoadGameState, at the exact QUESTS boundary.
    // The outer CPersistContext and the remaining save sections stay native.
    class QuestStateNativeCaptureHook final
    {
    public:
        using CaptureSink = void (*)(
            void* context,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        using SnapshotProvider = bool (*)(
            void* context,
            const std::uint8_t*& bytes,
            std::size_t& byteCount) noexcept;
        using ApplyResultSink = void (*)(
            void* context,
            bool applied) noexcept;

        void BindGameModule(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;

        bool Install(
            HMODULE gameModule,
            CaptureSink sink,
            void* context,
            const core::Diagnostics& diagnostics) noexcept;
        bool InstallLoadOverride(
            HMODULE gameModule,
            SnapshotProvider provider,
            ApplyResultSink resultSink,
            void* context,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;
        // Performs one bounded capture on the game thread. This is used when
        // the host world becomes ready so guests do not depend on a later
        // user save to receive the initial global quest state.
        bool CaptureCurrent();
        // Applies the retail parser sequence after the guest's native QUESTS
        // section has returned. The exact constructor/context/dtor ABI is
        // isolated here.
        bool ApplySnapshot(
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept
        {
            return detour_.IsInstalled();
        }
        [[nodiscard]] bool IsLoadOverrideInstalled() const noexcept
        {
            return loadDetour_.IsInstalled();
        }

    private:
        using SaveGameState = void(__thiscall*)(
            void* manager,
            game::native::CharString* output);
        using LoadGameState = void(__thiscall*)(
            void* manager,
            void* parser);

        static void __fastcall SaveGameStateObserved(
            void* manager,
            void* unused,
            game::native::CharString* output);
        void ObserveSaveGameState(
            void* manager,
            game::native::CharString* output);
        static void __fastcall LoadGameStateObserved(
            void* manager,
            void* unused,
            void* parser);
        void ObserveLoadGameState(void* manager, void* parser);
        bool CaptureOutput(const game::native::CharString& output) noexcept;
        bool ApplySnapshotToManager(
            void* manager,
            LoadGameState load,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;

        static QuestStateNativeCaptureHook* active_;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CaptureSink sink_ = nullptr;
        void* sinkContext_ = nullptr;
        SaveGameState original_ = nullptr;
        core::hooking::InlineHook detour_;
        SnapshotProvider snapshotProvider_ = nullptr;
        ApplyResultSink applyResultSink_ = nullptr;
        void* loadContext_ = nullptr;
        LoadGameState originalLoad_ = nullptr;
        core::hooking::InlineHook loadDetour_;
    };
}
