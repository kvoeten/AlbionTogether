#include "GuestHeroSaveBoundary.h"

#include <cstdio>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace fable::multiplayer::persistence
{
    bool GuestHeroSaveBoundary::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        diagnostics_ = diagnostics;
        return compression_.Initialize(gameModule, diagnostics);
    }

    void GuestHeroSaveBoundary::Shutdown() noexcept
    {
        compression_.Shutdown();
        hero_.Clear();
        candidateHero_.Clear();
        diagnostics_ = {};
        sourceMapId_ = 0;
        candidateMapId_ = 0;
        firstHeroMarkerMapId_ = 0;
        candidateCount_ = 0;
        eligibleRecordCount_ = 0;
        inflatedRecordCount_ = 0;
        heroMarkerRecordCount_ = 0;
        firstHeroMarkerOffset_ = 0;
        captureActive_ = false;
        captured_ = false;
    }

    void GuestHeroSaveBoundary::BeginGuestCollection() noexcept
    {
        if (captured_)
        {
            return;
        }
        candidateHero_.Clear();
        candidateMapId_ = 0;
        firstHeroMarkerMapId_ = 0;
        candidateCount_ = 0;
        eligibleRecordCount_ = 0;
        inflatedRecordCount_ = 0;
        heroMarkerRecordCount_ = 0;
        firstHeroMarkerOffset_ = 0;
        captureActive_ = true;
    }

    void GuestHeroSaveBoundary::ObserveGuestRecord(
        const game::entity::persistence::SavedEntityMapBlobSnapshot& snapshot)
        noexcept
    {
        using game::entity::persistence::SavedEntityMapBlobFormat;
        if (captured_ || !captureActive_ ||
            snapshot.format != SavedEntityMapBlobFormat::Binary ||
            snapshot.mapId == 0 || snapshot.mapId >
                (std::numeric_limits<std::uint16_t>::max)() ||
            snapshot.bytes == nullptr || snapshot.byteCount == 0 ||
            snapshot.metadata < sizeof(std::uint32_t))
        {
            return;
        }
        ++eligibleRecordCount_;
        std::vector<std::uint8_t> inflated;
        if (!compression_.Inflate(
                snapshot.bytes,
                snapshot.byteCount,
                snapshot.metadata,
                inflated))
        {
            return;
        }
        ++inflatedRecordCount_;
        constexpr char heroMarker[] = "PlayerCreature";
        const auto marker = std::search(
            inflated.begin(),
            inflated.end(),
            heroMarker,
            heroMarker + sizeof(heroMarker) - 1);
        if (marker != inflated.end())
        {
            ++heroMarkerRecordCount_;
            if (firstHeroMarkerMapId_ == 0)
            {
                firstHeroMarkerMapId_ = static_cast<std::uint16_t>(
                    snapshot.mapId);
                firstHeroMarkerOffset_ = static_cast<std::size_t>(
                    marker - inflated.begin());
            }
        }
        game::entity::persistence::serialization::HeroSavedEntityRecord hero;
        if (!game::entity::persistence::serialization::
                ExtractHeroSavedEntityRecord(
                    inflated.data(), inflated.size(), hero))
        {
            return;
        }

        ++candidateCount_;
        if (candidateCount_ == 1)
        {
            candidateHero_ = std::move(hero);
            candidateMapId_ = static_cast<std::uint16_t>(snapshot.mapId);
        }
    }

    bool GuestHeroSaveBoundary::CompleteGuestCollection(
        const bool complete) noexcept
    {
        if (captured_)
        {
            return true;
        }
        if (!captureActive_)
        {
            return false;
        }
        captureActive_ = false;
        if (!complete || candidateCount_ != 1 ||
            candidateHero_.bytes.empty() || candidateMapId_ == 0)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "collection_complete=%s eligible=%zu inflated=%zu marker_records=%zu hero_records=%zu first_marker_map=%u first_marker_offset=%zu",
                complete ? "true" : "false",
                eligibleRecordCount_,
                inflatedRecordCount_,
                heroMarkerRecordCount_,
                candidateCount_,
                static_cast<unsigned int>(firstHeroMarkerMapId_),
                firstHeroMarkerOffset_);
            diagnostics_.Event(
                "MultiplayerGuestHeroSaveRecordRejected", detail);
            candidateHero_.Clear();
            candidateMapId_ = 0;
            candidateCount_ = 0;
            return false;
        }

        hero_ = std::move(candidateHero_);
        sourceMapId_ = candidateMapId_;
        candidateMapId_ = 0;
        candidateCount_ = 0;
        captured_ = true;
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map_id=%u hero_uid=%016llX record_bytes=%zu",
            static_cast<unsigned int>(sourceMapId_),
            static_cast<unsigned long long>(hero_.uid),
            hero_.bytes.size());
        diagnostics_.Event("MultiplayerGuestHeroSaveRecordCaptured", detail);
        return true;
    }

    bool GuestHeroSaveBoundary::RewriteHostCollection(
        const std::uint64_t collectionRevision,
        std::map<std::uint16_t, SavedEntityCollectionRecord>& records,
        const bool insertGuestHero)
    {
        if (!captured_ || collectionRevision == 0)
        {
            return false;
        }
        // A fresh host record always has bootstrapOnly=false. This explicit
        // provenance survives repeated local preparation without mistaking a
        // later real host map update for our temporary Hero-only container.
        if (!insertGuestHero)
        {
            const auto source = records.find(sourceMapId_);
            if (source != records.end() && source->second.guestHeroBootstrapOnly)
                records.erase(source);
        }
        if (insertGuestHero && records.find(sourceMapId_) == records.end())
        {
            SavedEntityCollectionRecord empty;
            empty.mapId = sourceMapId_;
            empty.guestHeroBootstrapOnly = true;
            records.emplace(sourceMapId_, std::move(empty));
        }
        for (auto& [mapId, record] : records)
        {
            const bool sourceMap = mapId == sourceMapId_;
            if (!RewriteHostRecord(record, insertGuestHero && sourceMap))
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "map_id=%u bytes=%zu metadata=%u insert_guest_hero=%s",
                    static_cast<unsigned int>(mapId),
                    record.bytes.size(),
                    record.metadata,
                    insertGuestHero && sourceMap ? "true" : "false");
                diagnostics_.Event(
                    "MultiplayerHostWorldGuestHeroMergeFailed", detail);
                return false;
            }
            if (record.revision == 0) record.revision = collectionRevision;
        }
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "revision=%llu maps=%zu hero_map_id=%u hero_uid=%016llX guest_hero_record=%s world=host",
            static_cast<unsigned long long>(collectionRevision),
            records.size(),
            static_cast<unsigned int>(sourceMapId_),
            static_cast<unsigned long long>(hero_.uid),
            insertGuestHero ? "exact" : "retired");
        diagnostics_.Event("MultiplayerHostWorldGuestHeroMerged", detail);
        return true;
    }

    bool GuestHeroSaveBoundary::IsHeroCaptured() const noexcept
    {
        return captured_;
    }

    std::uint16_t GuestHeroSaveBoundary::SourceMapId() const noexcept
    {
        return sourceMapId_;
    }

    std::uint64_t GuestHeroSaveBoundary::HashBytes(
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool GuestHeroSaveBoundary::RewriteHostRecord(
        SavedEntityCollectionRecord& record,
        const bool insertGuestHero)
    {
        if (record.format != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            return false;
        }
        // Some Anniversary records are populated native blobs with metadata
        // zero rather than zlib cells. They cannot contain the framed Hero
        // record this boundary owns, so preserve them byte-for-byte.
        if ((!record.bytes.empty() && record.metadata < sizeof(std::uint32_t)) ||
            (record.bytes.empty() && !insertGuestHero))
        {
            return !insertGuestHero;
        }
        std::vector<std::uint8_t> inflated;
        if (record.bytes.empty())
        {
            // Zero native sections, not a copied guest world cell.
            inflated.resize(sizeof(std::uint32_t), 0);
        }
        else if (!compression_.Inflate(
                record.bytes.data(),
                record.bytes.size(),
                record.metadata,
                inflated))
        {
            return false;
        }
        std::vector<std::uint8_t> rewritten;
        const bool rewrittenOk = insertGuestHero
            ? game::entity::persistence::serialization::
                SpliceHeroIntoAuthoritativeCell(
                    inflated.data(), inflated.size(), hero_, rewritten)
            : game::entity::persistence::serialization::
                RemoveHeroSavedEntityRecords(
                    inflated.data(), inflated.size(), rewritten);
        if (!rewrittenOk)
        {
            return false;
        }
        if (rewritten == inflated)
        {
            return true;
        }
        std::vector<std::uint8_t> compressed;
        if (!compression_.Deflate(
                rewritten.data(), rewritten.size(), compressed))
        {
            return false;
        }
        record.metadata = static_cast<std::uint32_t>(rewritten.size());
        record.hash = HashBytes(compressed.data(), compressed.size());
        record.bytes = std::move(compressed);
        return true;
    }
}
