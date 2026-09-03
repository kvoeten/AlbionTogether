#include "SavedEntityMapRecordInstaller.h"

#include <cstdio>
#include <cstring>

namespace fable::game::entity::persistence::native
{
    namespace
    {
        constexpr std::size_t RecordVectorOffset = 0x10;
        constexpr std::size_t RecordBytes = 0x1C;
        constexpr std::size_t ByteVectorOffset = 0x04;
        constexpr std::size_t MetadataOffset = 0x14;
        constexpr std::size_t PopulatedOffset = 0x18;
    }

    bool SavedEntityMapRecordInstaller::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        diagnostics_ = diagnostics;
        if (!SavedEntitiesFunctions::ResolveRecordVectorResize(
                gameModule,
                recordResize_) ||
            !SavedEntitiesFunctions::ResolveByteVectorResize(
                gameModule,
                byteResize_))
        {
            diagnostics_.Event(
                "SavedEntityMapRecordInstallerUnavailable",
                "native record or byte-vector resize signature failed validation");
            Shutdown();
            return false;
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "record_resize=%p byte_resize=%p record_bytes=0x%zX",
            reinterpret_cast<void*>(recordResize_),
            reinterpret_cast<void*>(byteResize_),
            RecordBytes);
        diagnostics_.Event("SavedEntityMapRecordInstallerReady", detail);
        return true;
    }

    void SavedEntityMapRecordInstaller::Shutdown() noexcept
    {
        recordResize_ = nullptr;
        byteResize_ = nullptr;
        diagnostics_ = {};
    }

    bool SavedEntityMapRecordInstaller::Install(
        void* savedEntities,
        const SavedEntityMapBlobSnapshot& snapshot) const noexcept
    {
        if (!IsReady())
        {
            Report(snapshot, false, "native functions unavailable");
            return false;
        }
        if (snapshot.format != SavedEntityMapBlobFormat::Binary)
        {
            Report(snapshot, false, "text map records are not installed by the binary path");
            return false;
        }
        if (savedEntities == nullptr ||
            snapshot.mapId >= MaximumMapRecords ||
            snapshot.byteCount > MaximumBlobBytes ||
            (snapshot.byteCount != 0 && snapshot.bytes == nullptr))
        {
            Report(snapshot, false, "invalid or out-of-bounds record view");
            return false;
        }

        const bool installed = InstallGuarded(
            recordResize_,
            byteResize_,
            savedEntities,
            snapshot);
        Report(
            snapshot,
            installed,
            installed ? "installed through native vectors" : "native mutation faulted or failed validation");
        return installed;
    }

    bool SavedEntityMapRecordInstaller::IsReady() const noexcept
    {
        return recordResize_ != nullptr && byteResize_ != nullptr;
    }

    bool SavedEntityMapRecordInstaller::Clear(
        void* savedEntities,
        std::uint32_t mapId) const noexcept
    {
        SavedEntityMapBlobSnapshot snapshot;
        snapshot.format = SavedEntityMapBlobFormat::Binary;
        snapshot.mapId = mapId;
        snapshot.hash = 14695981039346656037ull;
        if (!IsReady() || savedEntities == nullptr ||
            mapId >= MaximumMapRecords)
        {
            Report(snapshot, false, "invalid clear target or native functions unavailable");
            return false;
        }
        const bool cleared = ClearGuarded(byteResize_, savedEntities, mapId);
        Report(
            snapshot,
            cleared,
            cleared ? "unpopulated host record applied" : "native clear faulted or failed validation");
        return cleared;
    }

    bool SavedEntityMapRecordInstaller::ClearAll(
        void* savedEntities) const noexcept
    {
        if (!IsReady() || savedEntities == nullptr)
        {
            return false;
        }
        bool cleared = false;
        std::size_t recordCount = 0;
        __try
        {
            const auto* const object =
                static_cast<const std::uint8_t*>(savedEntities);
            const auto* const recordVector = object + RecordVectorOffset;
            const auto* const begin =
                *reinterpret_cast<const std::uint8_t* const*>(recordVector);
            const auto* const end = *reinterpret_cast<
                const std::uint8_t* const*>(
                    recordVector + sizeof(void*));
            if ((begin == nullptr) != (end == nullptr) ||
                (begin != nullptr &&
                    (end < begin ||
                        static_cast<std::size_t>(end - begin) %
                            RecordBytes != 0)))
            {
                return false;
            }
            recordCount = begin != nullptr
                ? static_cast<std::size_t>(end - begin) / RecordBytes
                : 0;
            if (recordCount > MaximumMapRecords)
            {
                return false;
            }
            for (std::size_t mapId = 0; mapId < recordCount; ++mapId)
            {
                auto* const record =
                    const_cast<std::uint8_t*>(begin) + mapId * RecordBytes;
                record[PopulatedOffset] = 0;
                byteResize_(record + ByteVectorOffset, 0);
                *reinterpret_cast<std::uint32_t*>(record + MetadataOffset) = 0;
            }
            cleared = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            cleared = false;
        }
        char detail[128] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "records=%zu cleared=%s",
            recordCount,
            cleared ? "true" : "false");
        diagnostics_.Event("SavedEntityMapRecordsCleared", detail);
        return cleared;
    }

    bool SavedEntityMapRecordInstaller::InstallGuarded(
        SavedEntitiesFunctions::RecordVectorResizePointer recordResize,
        SavedEntitiesFunctions::ByteVectorResizePointer byteResize,
        void* savedEntities,
        const SavedEntityMapBlobSnapshot& snapshot) noexcept
    {
        bool installed = false;
        __try
        {
            auto* const object = static_cast<std::uint8_t*>(savedEntities);
            auto* const recordVector = object + RecordVectorOffset;
            auto* begin = *reinterpret_cast<std::uint8_t**>(recordVector);
            auto* end = *reinterpret_cast<std::uint8_t**>(
                recordVector + sizeof(void*));
            auto* capacity = *reinterpret_cast<std::uint8_t**>(
                recordVector + 2 * sizeof(void*));
            if ((begin == nullptr) != (end == nullptr) ||
                (begin == nullptr) != (capacity == nullptr) ||
                (begin != nullptr &&
                    (end < begin || capacity < end ||
                        static_cast<std::size_t>(end - begin) %
                                RecordBytes !=
                            0 ||
                        static_cast<std::size_t>(capacity - begin) %
                                RecordBytes !=
                            0)))
            {
                return false;
            }

            const std::size_t recordCount = begin != nullptr
                ? static_cast<std::size_t>(end - begin) / RecordBytes
                : 0;
            const std::size_t requiredCount =
                static_cast<std::size_t>(snapshot.mapId) + 1;
            if (recordCount > MaximumMapRecords ||
                requiredCount > MaximumMapRecords)
            {
                return false;
            }
            if (recordCount < requiredCount)
            {
                recordResize(recordVector, requiredCount);
            }

            begin = *reinterpret_cast<std::uint8_t**>(recordVector);
            end = *reinterpret_cast<std::uint8_t**>(
                recordVector + sizeof(void*));
            if (begin == nullptr || end < begin ||
                static_cast<std::size_t>(end - begin) / RecordBytes <
                    requiredCount)
            {
                return false;
            }

            auto* const record =
                begin + static_cast<std::size_t>(snapshot.mapId) * RecordBytes;
            record[PopulatedOffset] = 0;
            byteResize(record + ByteVectorOffset, snapshot.byteCount);

            auto* const bytes = *reinterpret_cast<std::uint8_t**>(
                record + ByteVectorOffset);
            auto* const bytesEnd = *reinterpret_cast<std::uint8_t**>(
                record + ByteVectorOffset + sizeof(void*));
            auto* const bytesCapacity = *reinterpret_cast<std::uint8_t**>(
                record + ByteVectorOffset + 2 * sizeof(void*));
            if ((bytes == nullptr) != (bytesEnd == nullptr) ||
                (bytes == nullptr) != (bytesCapacity == nullptr) ||
                (bytes != nullptr &&
                    (bytesEnd < bytes || bytesCapacity < bytesEnd)) ||
                (bytes != nullptr
                    ? static_cast<std::size_t>(bytesEnd - bytes)
                    : 0) != snapshot.byteCount)
            {
                return false;
            }
            if (snapshot.byteCount != 0)
            {
                std::memcpy(bytes, snapshot.bytes, snapshot.byteCount);
            }
            *reinterpret_cast<std::uint32_t*>(record + MetadataOffset) =
                snapshot.metadata;
            record[PopulatedOffset] = 1;
            installed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            installed = false;
        }
        return installed;
    }

    bool SavedEntityMapRecordInstaller::ClearGuarded(
        SavedEntitiesFunctions::ByteVectorResizePointer byteResize,
        void* savedEntities,
        std::uint32_t mapId) noexcept
    {
        bool cleared = false;
        __try
        {
            auto* const object = static_cast<std::uint8_t*>(savedEntities);
            auto* const recordVector = object + RecordVectorOffset;
            auto* const begin =
                *reinterpret_cast<std::uint8_t**>(recordVector);
            auto* const end = *reinterpret_cast<std::uint8_t**>(
                recordVector + sizeof(void*));
            if ((begin == nullptr) != (end == nullptr) ||
                (begin != nullptr &&
                    (end < begin ||
                        static_cast<std::size_t>(end - begin) %
                                RecordBytes !=
                            0)))
            {
                return false;
            }
            const std::size_t recordCount = begin != nullptr
                ? static_cast<std::size_t>(end - begin) / RecordBytes
                : 0;
            if (mapId >= recordCount)
            {
                return true;
            }
            auto* const record =
                begin + static_cast<std::size_t>(mapId) * RecordBytes;
            record[PopulatedOffset] = 0;
            byteResize(record + ByteVectorOffset, 0);
            *reinterpret_cast<std::uint32_t*>(record + MetadataOffset) = 0;
            cleared = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            cleared = false;
        }
        return cleared;
    }

    void SavedEntityMapRecordInstaller::Report(
        const SavedEntityMapBlobSnapshot& snapshot,
        bool installed,
        const char* reason) const noexcept
    {
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "installed=%s map_id=%u format=%s bytes=%zu metadata=%u hash=%llu reason=%s",
            installed ? "true" : "false",
            snapshot.mapId,
            snapshot.format == SavedEntityMapBlobFormat::Binary
                ? "binary"
                : "text",
            snapshot.byteCount,
            snapshot.metadata,
            static_cast<unsigned long long>(snapshot.hash),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event("SavedEntityMapRecordInstall", detail);
    }
}
