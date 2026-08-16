#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace fable::multiplayer::persistence
{
    struct SavedEntityMapBlob final
    {
        game::entity::persistence::SavedEntityMapBlobFormat format =
            game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        std::uint32_t mapId = 0;
        std::uint32_t metadata = 0;
        std::uint64_t hash = 0;
        std::uint64_t revision = 0;
        std::vector<std::uint8_t> bytes;

    private:
        friend class SavedEntityMapBlobDirectory;
        std::uint64_t captureGeneration = 0;
    };

    // One bounded current host-save record per native map ID. Captures replace
    // current payloads and never append save history.
    class SavedEntityMapBlobDirectory final
    {
    public:
        static constexpr std::size_t MaximumMapRecords = 4'096;
        static constexpr std::size_t MaximumBlobBytes = 8 * 1024 * 1024;
        static constexpr std::size_t MaximumTotalBytes = 64 * 1024 * 1024;

        void Initialize(const core::Diagnostics& diagnostics);
        bool BeginCapture(
            game::entity::persistence::SavedEntityMapBlobFormat format,
            std::size_t recordCount) noexcept;
        bool Capture(
            const game::entity::persistence::SavedEntityMapBlobSnapshot&
                snapshot) noexcept;
        bool CompleteCapture(bool sourceValid) noexcept;
        void Clear() noexcept;

        [[nodiscard]] const SavedEntityMapBlob* Find(
            std::uint32_t mapId) const noexcept;
        [[nodiscard]] bool IsComplete() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t TotalBytes() const noexcept;
        [[nodiscard]] std::uint64_t CaptureRevision() const noexcept;

    private:
        void ReportCapture(
            bool accepted,
            const char* reason) noexcept;

        std::map<std::uint32_t, SavedEntityMapBlob> blobs_;
        core::Diagnostics diagnostics_ = {};
        game::entity::persistence::SavedEntityMapBlobFormat captureFormat_ =
            game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        std::uint64_t captureGeneration_ = 0;
        std::uint64_t captureRevision_ = 0;
        std::uint64_t nextRevision_ = 0;
        std::size_t totalBytes_ = 0;
        std::size_t expectedRecordCount_ = 0;
        bool captureOpen_ = false;
        bool captureValid_ = false;
        bool complete_ = false;
    };
}
