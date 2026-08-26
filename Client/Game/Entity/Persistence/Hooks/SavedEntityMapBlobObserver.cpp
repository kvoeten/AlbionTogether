#include "SavedEntityMapBlobObserver.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    std::uint64_t HashBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

namespace fable::game::entity::persistence
{
    SavedEntityMapBlobObserver* SavedEntityMapBlobObserver::active_ = nullptr;

    bool SavedEntityMapBlobObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: saved-entity map observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        if (active_ == this)
        {
            diagnostics_.Log(
                "Hook: saved-entity map installation is partially active; shutdown is required before retrying.");
            return false;
        }

        std::uint8_t* textTarget = nullptr;
        std::uint8_t* binaryTarget = nullptr;
        if (!native::SavedEntitiesFunctions::ResolveLoadText(
                gameModule,
                textTarget) ||
            !native::SavedEntitiesFunctions::ResolveLoadBinary(
                gameModule,
                binaryTarget))
        {
            diagnostics_.Log(
                "Hook: CSavedEntities load definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                textTarget,
                reinterpret_cast<void*>(
                    &SavedEntityMapBlobObserver::LoadTextObserved),
                loadTextDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalLoadText_ =
            reinterpret_cast<native::SavedEntitiesFunctions::LoadPointer>(
                loadTextDetour_.Original());

        if (!InstallDetour(
                binaryTarget,
                reinterpret_cast<void*>(
                    &SavedEntityMapBlobObserver::LoadBinaryObserved),
                loadBinaryDetour_))
        {
            const bool rollbackRestored = RestoreDetour(loadTextDetour_);
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: saved-entity map rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalLoadText_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalLoadBinary_ =
            reinterpret_cast<native::SavedEntitiesFunctions::LoadPointer>(
                loadBinaryDetour_.Original());

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "text=%p text_trampoline=%p binary=%p binary_trampoline=%p vector_offset=0x10 record_bytes=0x1C populated_offset=0x18",
            textTarget,
            loadTextDetour_.Original(),
            binaryTarget,
            loadBinaryDetour_.Original());
        diagnostics_.Event("SavedEntityMapBlobObserverReady", detail);
        return true;
#endif
    }

    void SavedEntityMapBlobObserver::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(loadBinaryDetour_) && allRestored;
        allRestored = RestoreDetour(loadTextDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: saved-entity map shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetPostLoadBarrierSink(nullptr, nullptr);
        SetCollectionSink(nullptr, nullptr);
        SetSnapshotSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalLoadBinary_ = nullptr;
        originalLoadText_ = nullptr;
        diagnostics_ = {};
    }

    void SavedEntityMapBlobObserver::SetSnapshotSink(
        SnapshotSink sink,
        void* context) noexcept
    {
        sinkContext_.store(context, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }

    void SavedEntityMapBlobObserver::SetCollectionSink(
        CollectionSink sink,
        void* context) noexcept
    {
        collectionSinkContext_.store(context, std::memory_order_release);
        collectionSink_.store(sink, std::memory_order_release);
    }

    void SavedEntityMapBlobObserver::SetPostLoadBarrierSink(
        PostLoadBarrierSink sink,
        void* context) noexcept
    {
        postLoadBarrierContext_.store(context, std::memory_order_release);
        postLoadBarrierSink_.store(sink, std::memory_order_release);
    }

    bool SavedEntityMapBlobObserver::IsInstalled() const noexcept
    {
        return active_ == this && loadTextDetour_.IsInstalled() &&
            loadBinaryDetour_.IsInstalled() &&
            originalLoadText_ != nullptr && originalLoadBinary_ != nullptr;
    }

    void __fastcall SavedEntityMapBlobObserver::LoadTextObserved(
        void* savedEntities,
        void*,
        void* reader)
    {
        SavedEntityMapBlobObserver* const observer = active_;
        if (observer == nullptr || observer->originalLoadText_ == nullptr)
        {
            return;
        }
        observer->originalLoadText_(savedEntities, reader);
        observer->Observe(savedEntities, SavedEntityMapBlobFormat::Text);
    }

    void __fastcall SavedEntityMapBlobObserver::LoadBinaryObserved(
        void* savedEntities,
        void*,
        void* reader)
    {
        SavedEntityMapBlobObserver* const observer = active_;
        if (observer == nullptr || observer->originalLoadBinary_ == nullptr)
        {
            return;
        }
        observer->originalLoadBinary_(savedEntities, reader);
        observer->Observe(savedEntities, SavedEntityMapBlobFormat::Binary);
    }

    void SavedEntityMapBlobObserver::Observe(
        void* savedEntities,
        SavedEntityMapBlobFormat format) noexcept
    {
        ObservationSummary summary;
        bool collectionStarted = false;
        bool observationSucceeded = true;
        __try
        {
            if (savedEntities == nullptr)
            {
                ++summary.invalidCount;
            }
            else
            {
                const auto* const object =
                    static_cast<const std::uint8_t*>(savedEntities);
                const auto* const begin =
                    *reinterpret_cast<const std::uint8_t* const*>(
                        object + 0x10);
                const auto* const end =
                    *reinterpret_cast<const std::uint8_t* const*>(
                        object + 0x14);
                if ((begin == nullptr) != (end == nullptr) || end < begin)
                {
                    ++summary.invalidCount;
                }
                else
                {
                    const std::size_t vectorBytes =
                        begin != nullptr
                            ? static_cast<std::size_t>(end - begin)
                            : 0;
                    if (vectorBytes % RecordBytes != 0 ||
                        vectorBytes / RecordBytes > MaximumMapRecords)
                    {
                        ++summary.invalidCount;
                    }
                    else
                    {
                        summary.recordCount = vectorBytes / RecordBytes;
                        const CollectionSink collectionSink =
                            collectionSink_.load(std::memory_order_acquire);
                        void* const collectionContext =
                            collectionSinkContext_.load(
                                std::memory_order_acquire);
                        if (collectionSink != nullptr)
                        {
                            SavedEntityMapCollectionEvent event;
                            event.format = format;
                            event.phase =
                                SavedEntityMapCollectionPhase::Begin;
                            event.savedEntities = savedEntities;
                            event.recordCount = summary.recordCount;
                            collectionSink(collectionContext, event);
                            collectionStarted = true;
                        }
                        const SnapshotSink sink = sink_.load(
                            std::memory_order_acquire);
                        void* const sinkContext = sinkContext_.load(
                            std::memory_order_acquire);
                        for (std::size_t index = 0;
                             index < summary.recordCount;
                             ++index)
                        {
                            const std::uint8_t* const record =
                                begin + index * RecordBytes;
                            if (record[0x18] == 0)
                            {
                                continue;
                            }
                            ++summary.populatedCount;
                            SavedEntityMapBlobSnapshot snapshot;
                            if (!ReadSnapshot(
                                    record,
                                    static_cast<std::uint32_t>(index),
                                    format,
                                    snapshot))
                            {
                                ++summary.invalidCount;
                                continue;
                            }
                            ++summary.validCount;
                            summary.totalBytes += snapshot.byteCount;
                            ReportSnapshot(snapshot);
                            if (sink != nullptr)
                            {
                                sink(sinkContext, snapshot);
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++summary.invalidCount;
            observationSucceeded = false;
        }
        if (collectionStarted)
        {
            const CollectionSink collectionSink = collectionSink_.load(
                std::memory_order_acquire);
            SavedEntityMapCollectionEvent event;
            event.format = format;
            event.phase = observationSucceeded &&
                    summary.invalidCount == 0
                ? SavedEntityMapCollectionPhase::Complete
                : SavedEntityMapCollectionPhase::Failed;
            event.savedEntities = savedEntities;
            event.recordCount = summary.recordCount;
            if (collectionSink != nullptr)
            {
                collectionSink(
                    collectionSinkContext_.load(
                        std::memory_order_acquire),
                    event);
            }
            if (event.phase == SavedEntityMapCollectionPhase::Complete)
            {
                const PostLoadBarrierSink barrier =
                    postLoadBarrierSink_.load(std::memory_order_acquire);
                if (barrier != nullptr)
                {
                    barrier(
                        postLoadBarrierContext_.load(
                            std::memory_order_acquire),
                        event);
                }
            }
        }
        Report(format, summary);
    }

    bool SavedEntityMapBlobObserver::ReadSnapshot(
        const std::uint8_t* record,
        std::uint32_t mapId,
        SavedEntityMapBlobFormat format,
        SavedEntityMapBlobSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        snapshot.format = format;
        snapshot.mapId = mapId;
        snapshot.metadata = *reinterpret_cast<const std::uint32_t*>(
            record + 0x14);

        if (format == SavedEntityMapBlobFormat::Binary)
        {
            const auto* const begin =
                *reinterpret_cast<const std::uint8_t* const*>(record + 0x04);
            const auto* const end =
                *reinterpret_cast<const std::uint8_t* const*>(record + 0x08);
            const auto* const capacity =
                *reinterpret_cast<const std::uint8_t* const*>(record + 0x0C);
            if ((begin == nullptr) != (end == nullptr) ||
                (begin == nullptr) != (capacity == nullptr) ||
                (begin != nullptr && (end < begin || capacity < end)))
            {
                return false;
            }
            snapshot.bytes = begin;
            snapshot.byteCount = begin != nullptr
                ? static_cast<std::size_t>(end - begin)
                : 0;
        }
        else if (format == SavedEntityMapBlobFormat::Text)
        {
            const auto* const stringBlock =
                *reinterpret_cast<const std::uint8_t* const*>(record);
            if (stringBlock == nullptr)
            {
                snapshot.bytes = nullptr;
                snapshot.byteCount = 0;
            }
            else
            {
                const auto* const text =
                    *reinterpret_cast<const std::uint8_t* const*>(
                        stringBlock + 0x04);
                if (text == nullptr)
                {
                    return false;
                }
                std::size_t length = 0;
                while (length < MaximumBlobBytes && text[length] != 0)
                {
                    ++length;
                }
                if (length == MaximumBlobBytes)
                {
                    return false;
                }
                snapshot.bytes = text;
                snapshot.byteCount = length;
            }
        }
        else
        {
            return false;
        }

        if (snapshot.byteCount > MaximumBlobBytes ||
            (snapshot.byteCount != 0 && snapshot.bytes == nullptr))
        {
            return false;
        }
        snapshot.hash = HashBytes(snapshot.bytes, snapshot.byteCount);
        return true;
    }

    void SavedEntityMapBlobObserver::Report(
        SavedEntityMapBlobFormat format,
        const ObservationSummary& summary) noexcept
    {
        const unsigned int ordinal = observationCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u format=%s records=%zu populated=%zu valid=%zu invalid=%zu bytes=%zu",
            ordinal,
            format == SavedEntityMapBlobFormat::Binary ? "binary" : "text",
            summary.recordCount,
            summary.populatedCount,
            summary.validCount,
            summary.invalidCount,
            summary.totalBytes);
        diagnostics_.Event("SavedEntityMapBlobsObserved", detail);
    }

    void SavedEntityMapBlobObserver::ReportSnapshot(
        const SavedEntityMapBlobSnapshot& snapshot) noexcept
    {
        const unsigned int ordinal = snapshotCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticSnapshotLimit)
        {
            return;
        }
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "ordinal=%u format=%s map_id=%u bytes=%zu metadata=%u hash=%016llX",
            ordinal,
            snapshot.format == SavedEntityMapBlobFormat::Binary
                ? "binary"
                : "text",
            snapshot.mapId,
            snapshot.byteCount,
            snapshot.metadata,
            static_cast<unsigned long long>(snapshot.hash));
        diagnostics_.Event("SavedEntityMapBlobObserved", detail);
    }

    bool SavedEntityMapBlobObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        core::hooking::InlineHook& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::SavedEntitiesFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr ||
            detour.IsInstalled())
        {
            return false;
        }
        return detour.Install(
            target,
            target,
            displacedBytes,
            replacement,
            displacedBytes);
    }

    bool SavedEntityMapBlobObserver::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
    }
}
