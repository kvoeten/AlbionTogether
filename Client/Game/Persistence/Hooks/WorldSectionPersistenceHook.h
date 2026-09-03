#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Multiplayer/Protocol/WorldSectionSnapshotMessage.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fable::game::persistence
{
    // Current-build bridge for the retail REGIONS and FACTIONS manager
    // persistence leaves. It observes/replaces only their CPersistContext
    // payload; transport ownership remains outside this class.
    class WorldSectionPersistenceHook final
    {
    public:
        using Section = multiplayer::protocol::WorldSection;
        using CaptureSink = void (*)(
            void* context,
            Section section,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        using SnapshotProvider = bool (*)(
            void* context,
            Section section,
            std::shared_ptr<const std::vector<std::uint8_t>>& snapshot)
            noexcept;
        using ApplyResultSink = void (*)(
            void* context,
            Section section,
            bool applied) noexcept;

        bool InstallHostCapture(
            HMODULE gameModule,
            CaptureSink sink,
            void* context,
            const core::Diagnostics& diagnostics) noexcept;
        bool InstallGuestOverride(
            HMODULE gameModule,
            SnapshotProvider provider,
            ApplyResultSink resultSink,
            void* context,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsHostCaptureInstalled() const noexcept;
        [[nodiscard]] bool IsGuestOverrideInstalled() const noexcept;

    private:
        using PersistFunction = void(__thiscall*)(void*, void*);

        static void __fastcall SaveRegionsObserved(void*, void*, void*);
        static void __fastcall SaveFactionsObserved(void*, void*, void*);
        static void __fastcall LoadRegionsObserved(void*, void*, void*);
        static void __fastcall LoadFactionsObserved(void*, void*, void*);

        void ObserveSave(
            Section section,
            PersistFunction original,
            void* manager,
            void* persistContext);
        void ObserveLoad(
            Section section,
            PersistFunction original,
            std::uintptr_t expectedReturnRva,
            void* manager,
            void* persistContext,
            const void* returnAddress);
        bool InstallOne(
            core::hooking::InlineHook& hook,
            std::uintptr_t targetRva,
            std::uintptr_t exceptionRecordRva,
            void* replacement,
            PersistFunction& original) noexcept;

        static WorldSectionPersistenceHook* active_;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CaptureSink captureSink_ = nullptr;
        void* captureContext_ = nullptr;
        SnapshotProvider snapshotProvider_ = nullptr;
        ApplyResultSink applyResultSink_ = nullptr;
        void* loadContext_ = nullptr;
        PersistFunction saveRegionsOriginal_ = nullptr;
        PersistFunction saveFactionsOriginal_ = nullptr;
        PersistFunction loadRegionsOriginal_ = nullptr;
        PersistFunction loadFactionsOriginal_ = nullptr;
        core::hooking::InlineHook saveRegionsHook_;
        core::hooking::InlineHook saveFactionsHook_;
        core::hooking::InlineHook loadRegionsHook_;
        core::hooking::InlineHook loadFactionsHook_;
        std::array<unsigned int, 2> invalidLoadContextReports_ = {};
    };
}
