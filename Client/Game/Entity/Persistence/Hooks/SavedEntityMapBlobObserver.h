#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Entity/Persistence/Native/SavedEntitiesFunctions.h"
#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence
{
    // Observes the complete per-map CSavedEntities table immediately after the
    // retail save loader has reconstructed it. It never mutates game-owned
    // strings, buffers, or vector storage.
    class SavedEntityMapBlobObserver final
    {
    public:
        using SnapshotSink = void(*)(
            void* context,
            const SavedEntityMapBlobSnapshot& snapshot) noexcept;
        using CollectionSink = void(*)(
            void* context,
            const SavedEntityMapCollectionEvent& event) noexcept;
        // Runs after the retail loader and collection observer have completed,
        // but before CSavedEntities_LoadBinary returns to the save-section
        // loader. A multiplayer guest can hold this boundary while its
        // authoritative map record is installed, preventing retail Thing
        // construction from consuming the local save's stale simulation.
        using PostLoadBarrierSink = void(*)(
            void* context,
            const SavedEntityMapCollectionEvent& event) noexcept;

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetSnapshotSink(SnapshotSink sink, void* context) noexcept;
        void SetCollectionSink(CollectionSink sink, void* context) noexcept;
        void SetPostLoadBarrierSink(
            PostLoadBarrierSink sink,
            void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static constexpr std::size_t RecordBytes = 0x1C;
        static constexpr std::size_t MaximumMapRecords = 4'096;
        static constexpr std::size_t MaximumBlobBytes = 8 * 1024 * 1024;
        static constexpr unsigned int DiagnosticEventLimit = 16;
        static constexpr unsigned int DiagnosticSnapshotLimit = 128;

        struct ObservationSummary final
        {
            std::size_t recordCount = 0;
            std::size_t populatedCount = 0;
            std::size_t validCount = 0;
            std::size_t invalidCount = 0;
            std::size_t totalBytes = 0;
        };

        static void __fastcall LoadTextObserved(
            void* savedEntities,
            void* unused,
            void* reader);
        static void __fastcall LoadBinaryObserved(
            void* savedEntities,
            void* unused,
            void* reader);
        void Observe(
            void* savedEntities,
            SavedEntityMapBlobFormat format) noexcept;
        bool ReadSnapshot(
            const std::uint8_t* record,
            std::uint32_t mapId,
            SavedEntityMapBlobFormat format,
            SavedEntityMapBlobSnapshot& snapshot) const noexcept;
        void Report(
            SavedEntityMapBlobFormat format,
            const ObservationSummary& summary) noexcept;
        void ReportSnapshot(
            const SavedEntityMapBlobSnapshot& snapshot) noexcept;
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            core::hooking::InlineHook& detour) noexcept;
        static bool RestoreDetour(core::hooking::InlineHook& detour) noexcept;

        static SavedEntityMapBlobObserver* active_;

        core::Diagnostics diagnostics_ = {};
        native::SavedEntitiesFunctions::LoadPointer originalLoadText_ =
            nullptr;
        native::SavedEntitiesFunctions::LoadPointer originalLoadBinary_ =
            nullptr;
        core::hooking::InlineHook loadTextDetour_;
        core::hooking::InlineHook loadBinaryDetour_;
        std::atomic<SnapshotSink> sink_{nullptr};
        std::atomic<void*> sinkContext_{nullptr};
        std::atomic<CollectionSink> collectionSink_{nullptr};
        std::atomic<void*> collectionSinkContext_{nullptr};
        std::atomic<PostLoadBarrierSink> postLoadBarrierSink_{nullptr};
        std::atomic<void*> postLoadBarrierContext_{nullptr};
        std::atomic_uint observationCount_{0};
        std::atomic_uint snapshotCount_{0};
    };
}
