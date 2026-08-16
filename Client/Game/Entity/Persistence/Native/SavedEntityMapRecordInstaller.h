#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/Native/SavedEntitiesFunctions.h"
#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence::native
{
    // Applies one binary per-map record through the game's own vector
    // routines. Call only on the game thread while the CSavedEntities object
    // reported by the load observer is still current.
    class SavedEntityMapRecordInstaller final
    {
    public:
        static constexpr std::size_t MaximumMapRecords = 4'096;
        static constexpr std::size_t MaximumBlobBytes = 8 * 1024 * 1024;

        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        bool Install(
            void* savedEntities,
            const SavedEntityMapBlobSnapshot& snapshot) const noexcept;
        bool Clear(
            void* savedEntities,
            std::uint32_t mapId) const noexcept;
        [[nodiscard]] bool IsReady() const noexcept;

    private:
        static bool InstallGuarded(
            SavedEntitiesFunctions::RecordVectorResizePointer recordResize,
            SavedEntitiesFunctions::ByteVectorResizePointer byteResize,
            void* savedEntities,
            const SavedEntityMapBlobSnapshot& snapshot) noexcept;
        static bool ClearGuarded(
            SavedEntitiesFunctions::ByteVectorResizePointer byteResize,
            void* savedEntities,
            std::uint32_t mapId) noexcept;
        void Report(
            const SavedEntityMapBlobSnapshot& snapshot,
            bool installed,
            const char* reason) const noexcept;

        SavedEntitiesFunctions::RecordVectorResizePointer recordResize_ =
            nullptr;
        SavedEntitiesFunctions::ByteVectorResizePointer byteResize_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
