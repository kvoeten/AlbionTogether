#include "SavedEntityMapBlobDirectory.h"

#include <cstdio>
#include <utility>

namespace fable::multiplayer::persistence
{
    void SavedEntityMapBlobDirectory::Initialize(
        const core::Diagnostics& diagnostics)
    {
        Clear();
        diagnostics_ = diagnostics;
    }

    bool SavedEntityMapBlobDirectory::BeginCapture(
        game::entity::persistence::SavedEntityMapBlobFormat format,
        std::size_t recordCount) noexcept
    {
        if (recordCount > MaximumMapRecords)
        {
            captureOpen_ = false;
            captureValid_ = false;
            complete_ = false;
            ReportCapture(false, "record count exceeded the bounded map table");
            return false;
        }
        ++captureGeneration_;
        if (captureGeneration_ == 0)
        {
            ++captureGeneration_;
            for (auto& entry : blobs_)
            {
                entry.second.captureGeneration = 0;
            }
        }
        captureFormat_ = format;
        expectedRecordCount_ = recordCount;
        captureOpen_ = true;
        captureValid_ = true;
        complete_ = false;
        return true;
    }

    bool SavedEntityMapBlobDirectory::Capture(
        const game::entity::persistence::SavedEntityMapBlobSnapshot& snapshot)
        noexcept
    {
        if (!captureOpen_ || !captureValid_ ||
            snapshot.format != captureFormat_ ||
            snapshot.mapId >= expectedRecordCount_ ||
            snapshot.mapId >= MaximumMapRecords ||
            snapshot.byteCount > MaximumBlobBytes ||
            (snapshot.byteCount != 0 && snapshot.bytes == nullptr))
        {
            captureValid_ = false;
            return false;
        }

        auto existing = blobs_.find(snapshot.mapId);
        if (existing != blobs_.end() &&
            existing->second.format == snapshot.format &&
            existing->second.metadata == snapshot.metadata &&
            existing->second.hash == snapshot.hash &&
            existing->second.bytes.size() == snapshot.byteCount)
        {
            existing->second.captureGeneration = captureGeneration_;
            return true;
        }

        const std::size_t previousBytes = existing != blobs_.end()
            ? existing->second.bytes.size()
            : 0;
        if (snapshot.byteCount > MaximumTotalBytes -
                (totalBytes_ - previousBytes))
        {
            captureValid_ = false;
            return false;
        }

        try
        {
            SavedEntityMapBlob replacement;
            replacement.format = snapshot.format;
            replacement.mapId = snapshot.mapId;
            replacement.metadata = snapshot.metadata;
            replacement.hash = snapshot.hash;
            replacement.captureGeneration = captureGeneration_;
            if (snapshot.byteCount != 0)
            {
                replacement.bytes.assign(
                    snapshot.bytes,
                    snapshot.bytes + snapshot.byteCount);
            }
            ++nextRevision_;
            if (nextRevision_ == 0)
            {
                ++nextRevision_;
            }
            replacement.revision = nextRevision_;
            totalBytes_ = totalBytes_ - previousBytes + snapshot.byteCount;
            if (existing == blobs_.end())
            {
                blobs_.emplace(snapshot.mapId, std::move(replacement));
            }
            else
            {
                existing->second = std::move(replacement);
            }
            return true;
        }
        catch (...)
        {
            captureValid_ = false;
            return false;
        }
    }

    bool SavedEntityMapBlobDirectory::CompleteCapture(
        bool sourceValid) noexcept
    {
        if (!captureOpen_)
        {
            return false;
        }
        captureOpen_ = false;
        if (!sourceValid || !captureValid_)
        {
            complete_ = false;
            ReportCapture(false, "native map collection was incomplete or exceeded bounds");
            return false;
        }

        for (auto iterator = blobs_.begin(); iterator != blobs_.end();)
        {
            if (iterator->second.captureGeneration != captureGeneration_)
            {
                totalBytes_ -= iterator->second.bytes.size();
                iterator = blobs_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        ++captureRevision_;
        if (captureRevision_ == 0)
        {
            ++captureRevision_;
        }
        complete_ = true;
        ReportCapture(true, "complete current host map table");
        return true;
    }

    void SavedEntityMapBlobDirectory::Clear() noexcept
    {
        blobs_.clear();
        diagnostics_ = {};
        captureFormat_ =
            game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        captureGeneration_ = 0;
        captureRevision_ = 0;
        nextRevision_ = 0;
        totalBytes_ = 0;
        expectedRecordCount_ = 0;
        captureOpen_ = false;
        captureValid_ = false;
        complete_ = false;
    }

    const SavedEntityMapBlob* SavedEntityMapBlobDirectory::Find(
        std::uint32_t mapId) const noexcept
    {
        if (!complete_)
        {
            return nullptr;
        }
        const auto iterator = blobs_.find(mapId);
        return iterator != blobs_.end() ? &iterator->second : nullptr;
    }

    bool SavedEntityMapBlobDirectory::IsComplete() const noexcept
    {
        return complete_;
    }

    std::size_t SavedEntityMapBlobDirectory::Size() const noexcept
    {
        return blobs_.size();
    }

    std::size_t SavedEntityMapBlobDirectory::TotalBytes() const noexcept
    {
        return totalBytes_;
    }

    std::uint64_t SavedEntityMapBlobDirectory::CaptureRevision() const noexcept
    {
        return complete_ ? captureRevision_ : 0;
    }

    void SavedEntityMapBlobDirectory::ReportCapture(
        bool accepted,
        const char* reason) noexcept
    {
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "accepted=%s format=%s records=%zu populated=%zu bytes=%zu generation=%llu revision=%llu reason=%s",
            accepted ? "true" : "false",
            captureFormat_ ==
                    game::entity::persistence::SavedEntityMapBlobFormat::Binary
                ? "binary"
                : "text",
            expectedRecordCount_,
            blobs_.size(),
            totalBytes_,
            static_cast<unsigned long long>(captureGeneration_),
            static_cast<unsigned long long>(captureRevision_),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event("MultiplayerSavedEntityMapCapture", detail);
    }
}
