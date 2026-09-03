#include "LocalShopSaveBoundary.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
    using fable::game::entity::persistence::SavedEntityMapBlobFormat;
    using fable::game::entity::persistence::serialization::
        ShopSavedEntityRecord;

    constexpr std::size_t MaximumCellBytes = 8 * 1024 * 1024;
    constexpr unsigned int MaximumUnsupportedDiagnostics = 16;

}

namespace fable::multiplayer::persistence
{
    bool LocalShopSaveBoundary::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        diagnostics_ = diagnostics;
        return compression_.Initialize(gameModule, diagnostics);
    }

    void LocalShopSaveBoundary::Shutdown() noexcept
    {
        compression_.Shutdown();
        records_.clear();
        fingerprints_.clear();
        scope_.clear();
        diagnostics_ = {};
        scopeGeneration_ = 0;
        revision_ = 0;
        componentBytes_ = 0;
        unsupportedDiagnosticCount_ = 0;
        captureActive_ = false;
    }

    void LocalShopSaveBoundary::AdvanceRevision() noexcept
    {
        ++revision_;
        if (revision_ == 0)
        {
            ++revision_;
        }
    }

    void LocalShopSaveBoundary::BeginGuestCollection() noexcept
    {
        try
        {
            records_.clear();
            fingerprints_.clear();
            componentBytes_ = 0;
            ++scopeGeneration_;
            if (scopeGeneration_ == 0)
            {
                ++scopeGeneration_;
            }
            char scope[64] = {};
            const int written = std::snprintf(
                scope,
                sizeof(scope),
                "guest-shop-%llu",
                static_cast<unsigned long long>(scopeGeneration_));
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(scope))
            {
                scope_.clear();
                captureActive_ = false;
            }
            else
            {
                scope_.assign(scope, static_cast<std::size_t>(written));
                captureActive_ = true;
            }
        }
        catch (...)
        {
            records_.clear();
            fingerprints_.clear();
            scope_.clear();
            componentBytes_ = 0;
            captureActive_ = false;
        }
        AdvanceRevision();
    }

    std::uint64_t LocalShopSaveBoundary::HashBytes(
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        if (bytes == nullptr && byteCount != 0)
        {
            return 0;
        }
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool LocalShopSaveBoundary::SameRecord(
        const ShopRecord& left,
        const ShopRecord& right) noexcept
    {
        return left.identity == right.identity &&
            left.componentBytes == right.componentBytes;
    }

    bool LocalShopSaveBoundary::IsSupportedSnapshot(
        const game::entity::persistence::SavedEntityMapBlobSnapshot& snapshot)
        noexcept
    {
        return snapshot.format == SavedEntityMapBlobFormat::Binary &&
            snapshot.mapId != 0 && snapshot.mapId <= MaximumMapId &&
            snapshot.bytes != nullptr && snapshot.byteCount != 0 &&
            snapshot.byteCount <= MaximumCellBytes &&
            snapshot.metadata >= sizeof(std::uint32_t) &&
            snapshot.metadata <= MaximumCellBytes;
    }

    void LocalShopSaveBoundary::ReportUnsupported(
        const char* const reason) const noexcept
    {
        if (unsupportedDiagnosticCount_ >= MaximumUnsupportedDiagnostics)
        {
            return;
        }
        ++unsupportedDiagnosticCount_;
        try
        {
            diagnostics_.Event(
                "MultiplayerLocalShopSaveBoundaryUnsupported",
                reason != nullptr ? reason : "unsupported native shop record");
        }
        catch (...)
        {
        }
    }

    void LocalShopSaveBoundary::ObserveGuestRecord(
        const game::entity::persistence::SavedEntityMapBlobSnapshot& snapshot)
        noexcept
    {
        if (!captureActive_ || scope_.empty())
        {
            return;
        }
        if (snapshot.format != SavedEntityMapBlobFormat::Binary)
        {
            ReportUnsupported("only binary native shop cells are supported");
            return;
        }
        if (!IsSupportedSnapshot(snapshot))
        {
            ReportUnsupported("binary shop cell failed bounded metadata checks");
            return;
        }

        const std::uint64_t snapshotHash = snapshot.hash != 0
            ? snapshot.hash
            : HashBytes(snapshot.bytes, snapshot.byteCount);
        const auto fingerprint = fingerprints_.find(snapshot.mapId);
        if (fingerprint != fingerprints_.end() && fingerprint->second.valid &&
            fingerprint->second.hash == snapshotHash &&
            fingerprint->second.metadata == snapshot.metadata)
        {
            return;
        }

        try
        {
            std::vector<std::uint8_t> inflated;
            if (!compression_.Inflate(
                    snapshot.bytes,
                    snapshot.byteCount,
                    snapshot.metadata,
                    inflated))
            {
                ReportUnsupported("binary shop cell failed native inflation");
                return;
            }

            std::vector<ShopRecord> candidate;
            if (!game::entity::persistence::serialization::
                    ExtractShopSavedEntityRecords(
                        inflated.data(), inflated.size(), snapshot.mapId,
                        scope_, candidate))
            {
                // A malformed new observation must not discard a previous
                // valid merchant payload for this map.
                ReportUnsupported("binary shop cell failed CTCShop extraction");
                return;
            }
            if (candidate.size() > MaximumMerchants)
            {
                ReportUnsupported("CTCShop record count exceeds boundary");
                return;
            }

            std::size_t candidateBytes = 0;
            for (const ShopRecord& shop : candidate)
            {
                if (shop.identity.mapId != snapshot.mapId ||
                    shop.identity.uid == 0 || shop.identity.scope != scope_ ||
                    shop.componentBytes.empty() ||
                    shop.componentBytes.size() >
                        MaximumComponentBytes - candidateBytes)
                {
                    ReportUnsupported("CTCShop identity or size is invalid");
                    return;
                }
                candidateBytes += shop.componentBytes.size();
            }

            std::vector<ShopRecord> previous;
            for (const auto& entry : records_)
            {
                if (entry.first.mapId == snapshot.mapId)
                {
                    previous.push_back(entry.second);
                }
            }
            std::sort(previous.begin(), previous.end(),
                [](const ShopRecord& left, const ShopRecord& right)
                {
                    return left.identity < right.identity;
                });
            std::sort(candidate.begin(), candidate.end(),
                [](const ShopRecord& left, const ShopRecord& right)
                {
                    return left.identity < right.identity;
                });

            const bool payloadChanged = previous.size() != candidate.size() ||
                !std::equal(
                    previous.begin(), previous.end(), candidate.begin(),
                    [](const ShopRecord& left, const ShopRecord& right)
                    {
                        return SameRecord(left, right);
                    });

            if (payloadChanged)
            {
                std::size_t retainedBytes = componentBytes_;
                for (const ShopRecord& shop : previous)
                {
                    if (shop.componentBytes.size() > retainedBytes)
                    {
                        ReportUnsupported("CTCShop cache accounting underflow");
                        return;
                    }
                    retainedBytes -= shop.componentBytes.size();
                }
                if (candidate.size() > MaximumMerchants ||
                    records_.size() - previous.size() >
                        MaximumMerchants - candidate.size() ||
                    candidateBytes > MaximumComponentBytes - retainedBytes)
                {
                    ReportUnsupported("CTCShop cache bound exceeded");
                    return;
                }

                // Copy/swap keeps the last valid cache intact if any map
                // allocation fails while replacing this map's payload.
                auto replacement = records_;
                for (auto entry = replacement.begin();
                     entry != replacement.end();)
                {
                    if (entry->first.mapId == snapshot.mapId)
                    {
                        entry = replacement.erase(entry);
                    }
                    else
                    {
                        ++entry;
                    }
                }
                for (ShopRecord& shop : candidate)
                {
                    replacement.emplace(shop.identity, std::move(shop));
                }

                auto fingerprintReplacement = fingerprints_;
                fingerprintReplacement[snapshot.mapId] = {
                    snapshotHash,
                    snapshot.metadata,
                    true};
                records_.swap(replacement);
                componentBytes_ = retainedBytes + candidateBytes;
                fingerprints_.swap(fingerprintReplacement);
                AdvanceRevision();
            }
            else
            {
                fingerprints_[snapshot.mapId] = {
                    snapshotHash,
                    snapshot.metadata,
                    true};
            }
        }
        catch (...)
        {
            ReportUnsupported("CTCShop observation allocation failed");
        }
    }

    bool LocalShopSaveBoundary::RewriteHostRecord(
        SavedEntityCollectionRecord& record) noexcept
    {
        if (records_.empty())
        {
            return true;
        }
        if (record.mapId == 0 || record.mapId > MaximumMapId)
        {
            ReportUnsupported("host map id is outside native shop bounds");
            return true;
        }
        if (record.format != SavedEntityMapBlobFormat::Binary)
        {
            ReportUnsupported("host record is not native binary");
            return true;
        }
        if (record.bytes.empty() || record.metadata < sizeof(std::uint32_t) ||
            record.bytes.size() > MaximumCellBytes ||
            record.metadata > MaximumCellBytes)
        {
            ReportUnsupported("host record failed native shop bounds");
            return true;
        }

        std::vector<ShopRecord> local;
        try
        {
            for (const auto& entry : records_)
            {
                if (entry.first.mapId == record.mapId)
                {
                    local.push_back(entry.second);
                }
            }
            if (local.empty())
            {
                return true;
            }

            const auto fingerprint = fingerprints_.find(record.mapId);
            if (fingerprint != fingerprints_.end() && fingerprint->second.valid &&
                fingerprint->second.hash == record.hash &&
                fingerprint->second.metadata == record.metadata)
            {
                return true;
            }

            std::vector<std::uint8_t> inflated;
            if (!compression_.Inflate(
                    record.bytes.data(), record.bytes.size(), record.metadata,
                    inflated))
            {
                return false;
            }
            std::vector<std::uint8_t> rewritten;
            if (!game::entity::persistence::serialization::
                    SpliceShopSavedEntityRecords(
                        inflated.data(), inflated.size(), record.mapId,
                        scope_, local, rewritten))
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
            if (compressed.empty() || rewritten.size() >
                    (std::numeric_limits<std::uint32_t>::max)())
            {
                return false;
            }
            record.bytes = std::move(compressed);
            record.metadata = static_cast<std::uint32_t>(rewritten.size());
            record.hash = HashBytes(record.bytes.data(), record.bytes.size());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
