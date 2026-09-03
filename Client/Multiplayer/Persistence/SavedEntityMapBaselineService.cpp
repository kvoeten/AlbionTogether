#include "SavedEntityMapBaselineService.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"

#include "Game/Entity/Persistence/Hooks/SavedEntityMapBlobObserver.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    constexpr std::uint64_t EmptyHash = 14695981039346656037ull;
    constexpr std::size_t SubmissionBudget = 32;

    std::uint64_t HashBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount) noexcept
    {
        std::uint64_t hash = EmptyHash;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolveSavedEntityBaselineSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.world.savedEntityMapBaseline;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkSavedEntityBaseline,
    0x1008u,
    "saved-entity-map-baseline",
    800u,
    "multiplayer-saved-entity-map-baseline-dispatch",
    ResolveSavedEntityBaselineSink);

namespace fable::multiplayer::persistence
{
    void SavedEntityMapBaselineService::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        diagnostics_ = diagnostics;
        initialGuestHeroConstructionPending_ = role == PeerRole::Guest;
        directory_.Initialize(diagnostics);
        collectionTransfer_.Initialize(role, transport, directory_, diagnostics);
    }

    bool SavedEntityMapBaselineService::Attach(
        game::entity::persistence::SavedEntityMapBlobObserver& observer)
    {
        observer_ = &observer;
        if (!observer_->IsInstalled() ||
            !installer_.Initialize(GetModuleHandleW(nullptr), diagnostics_) ||
            (role_ == PeerRole::Guest &&
                (!guestHeroBoundary_.Initialize(
                    GetModuleHandleW(nullptr), diagnostics_) ||
                 !localShopBoundary_.Initialize(
                    GetModuleHandleW(nullptr), diagnostics_))))
        {
            return false;
        }
        observer_->SetCollectionSink(
            &SavedEntityMapBaselineService::ObserveCollection,
            this);
        observer_->SetSnapshotSink(
            &SavedEntityMapBaselineService::ObserveSnapshot,
            this);
        diagnostics_.Event(
            "MultiplayerSavedEntityMapBaselineReady",
            role_ == PeerRole::Host
                ? "host captures one bounded current record per native map ID and orders it ahead of grants"
                : "guest captures its selected-save Hero and merges it into the host world before retail construction");
        return true;
    }

    authority::MapBaselinePreparationResult
    SavedEntityMapBaselineService::PrepareHostGrant(
        std::uint16_t mapId,
        std::uint64_t& baselineRevision)
    {
        using authority::MapBaselinePreparationResult;
        baselineRevision = 0;
        if (role_ != PeerRole::Host || transport_ == nullptr || mapId == 0)
        {
            return MapBaselinePreparationResult::Failed;
        }
        if (!directory_.IsComplete())
        {
            return MapBaselinePreparationResult::Deferred;
        }
        baselineRevision = directory_.CaptureRevision();
        if (baselineRevision == 0)
        {
            return MapBaselinePreparationResult::Failed;
        }
        const std::uint64_t peerSetRevision =
            transport_->PeerSetRevision();
        const auto published = published_.find(mapId);
        if (published != published_.end() &&
            published->second.baselineRevision == baselineRevision &&
            published->second.peerSetRevision == peerSetRevision)
        {
            return MapBaselinePreparationResult::Ready;
        }
        if (!outbound_.active || outbound_.mapId != mapId ||
            outbound_.baselineRevision != baselineRevision ||
            outbound_.peerSetRevision != peerSetRevision)
        {
            if (!StartOutbound(
                    mapId,
                    baselineRevision,
                    peerSetRevision))
            {
                return MapBaselinePreparationResult::Failed;
            }
        }

        for (std::size_t submitted = 0;
             submitted < SubmissionBudget;
             ++submitted)
        {
            if (outbound_.stage == OutboundStage::Begin)
            {
                if (!SubmitOutboundMessage(
                        protocol::SavedEntityMapBaselineOperation::Begin,
                        nullptr,
                        0))
                {
                    return transport_->HasFailed()
                        ? MapBaselinePreparationResult::Failed
                        : MapBaselinePreparationResult::Deferred;
                }
                outbound_.stage = outbound_.totalBytes == 0
                    ? OutboundStage::Commit
                    : OutboundStage::Chunks;
                continue;
            }
            if (outbound_.stage == OutboundStage::Chunks)
            {
                const SavedEntityMapBlob* const blob =
                    directory_.Find(mapId);
                if (blob == nullptr ||
                    blob->bytes.size() != outbound_.totalBytes ||
                    outbound_.offset >= outbound_.totalBytes)
                {
                    outbound_ = {};
                    return MapBaselinePreparationResult::Failed;
                }
                const std::size_t chunkSize = (std::min)(
                    protocol::MaximumSavedEntityMapChunkBytes(),
                    static_cast<std::size_t>(
                        outbound_.totalBytes - outbound_.offset));
                if (!SubmitOutboundMessage(
                        protocol::SavedEntityMapBaselineOperation::Chunk,
                        blob->bytes.data() + outbound_.offset,
                        chunkSize))
                {
                    return transport_->HasFailed()
                        ? MapBaselinePreparationResult::Failed
                        : MapBaselinePreparationResult::Deferred;
                }
                outbound_.offset += static_cast<std::uint32_t>(chunkSize);
                if (outbound_.offset == outbound_.totalBytes)
                {
                    outbound_.stage = OutboundStage::Commit;
                }
                continue;
            }
            if (!SubmitOutboundMessage(
                    protocol::SavedEntityMapBaselineOperation::Commit,
                    nullptr,
                    0))
            {
                return transport_->HasFailed()
                    ? MapBaselinePreparationResult::Failed
                    : MapBaselinePreparationResult::Deferred;
            }
            published_[mapId] = {baselineRevision, peerSetRevision};
            ReportTransfer(
                "MultiplayerSavedEntityMapBaselinePublished",
                mapId,
                baselineRevision,
                "begin, chunks, and commit precede the map grant");
            outbound_ = {};
            return MapBaselinePreparationResult::Ready;
        }
        return MapBaselinePreparationResult::Deferred;
    }

    authority::MapBaselinePreparationResult
    SavedEntityMapBaselineService::PrepareHostCollection(
        const std::uint64_t peerSetRevision)
    {
        return collectionTransfer_.PrepareHost(peerSetRevision);
    }

    bool SavedEntityMapBaselineService::IsGuestCollectionReady() const noexcept
    {
        return role_ == PeerRole::Guest && guestCollectionRevision_ != 0 &&
            guestCollectionApplied_;
    }

    bool SavedEntityMapBaselineService::CompleteInitialGuestHeroConstruction()
        noexcept
    {
        if (role_ != PeerRole::Guest ||
            !initialGuestHeroConstructionPending_)
        {
            return true;
        }
        if (!guestCollectionApplied_ || !nativeCollectionReady_ ||
            nativeSavedEntities_ == nullptr ||
            !guestHeroBoundary_.IsHeroCaptured())
        {
            return false;
        }

        // The initial local Hero is now leaving its source map. Retire that one
        // source-scoped exception before a later revisit can construct a
        // duplicate Hero. Keeping it pinned for the whole initial incarnation
        // lets Fable finish any lazy inventory/ability reads from the selected
        // save; every record retained after departure is host-owned world state.
        initialGuestHeroConstructionPending_ = false;
        guestCollectionPrepared_ = false;
        if (!ApplyGuestCollection())
        {
            initialGuestHeroConstructionPending_ = true;
            guestCollectionPrepared_ = false;
            return false;
        }
        diagnostics_.Event(
            "MultiplayerGuestHeroInitialRecordRetired",
            "selected-save Hero departed its initial map; future map loads retain only host world records");
        return true;
    }

    bool SavedEntityMapBaselineService::IsGuestGrantReady(
        std::uint16_t mapId,
        std::uint64_t baselineRevision) const noexcept
    {
        if (role_ != PeerRole::Guest || mapId == 0 ||
            baselineRevision == 0)
        {
            return false;
        }
        const auto baseline = guestBaselines_.find(mapId);
        if (baseline == guestBaselines_.end() ||
            baseline->second.revision != baselineRevision)
        {
            return false;
        }
        // A staged packet is not enough once the guest has loaded a native
        // collection. The map grant must observe the installer having
        // applied (or explicitly cleared) the host record first; otherwise
        // retail construction would consume the guest's stale save.
        return baseline->second.appliedToCurrentCollection;
    }

    bool SavedEntityMapBaselineService::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (transportMessage.type !=
            protocol::PacketType::SavedEntityMapBaseline)
        {
            return false;
        }
        if (role_ != PeerRole::Guest)
        {
            diagnostics_.Event(
                "MultiplayerSavedEntityMapBaselineRejected",
                "guest cannot publish host saved-map baselines");
            return true;
        }
        protocol::SavedEntityMapBaselineMessage message;
        if (!protocol::DecodeSavedEntityMapBaselineMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            ResetInbound();
            diagnostics_.Event(
                "MultiplayerSavedEntityMapBaselineRejected",
                "invalid saved-map baseline packet");
            return true;
        }
        if (message.collection)
        {
            return collectionTransfer_.Handle(message) &&
                ConsumeCommittedCollection();
        }
        if (message.operation ==
            protocol::SavedEntityMapBaselineOperation::Begin)
        {
            return BeginInbound(message);
        }
        if (message.operation ==
            protocol::SavedEntityMapBaselineOperation::Chunk)
        {
            return AppendInbound(message);
        }
        return CommitInbound(message);
    }

    void SavedEntityMapBaselineService::Shutdown() noexcept
    {
        if (observer_ != nullptr)
        {
            observer_->SetSnapshotSink(nullptr, nullptr);
            observer_->SetCollectionSink(nullptr, nullptr);
        }
        observer_ = nullptr;
        installer_.Shutdown();
        guestHeroBoundary_.Shutdown();
        localShopBoundary_.Shutdown();
        collectionTransfer_.Shutdown();
        directory_.Clear();
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextTransferId_ = 0;
        outbound_ = {};
        ResetInbound();
        published_.clear();
        guestBaselines_.clear();
        guestBaselineBytes_ = 0;
        guestCollectionRevision_ = 0;
        guestCollectionApplied_ = false;
        guestCollectionPrepared_ = false;
        preparedCollectionIncludesGuestHero_ = false;
        initialGuestHeroConstructionPending_ = false;
        nativeSavedEntities_ = nullptr;
        nativeCollectionFormat_ = game::entity::persistence::
            SavedEntityMapBlobFormat::Binary;
        nativeCollectionReady_ = false;
    }

    const SavedEntityMapBlobDirectory&
        SavedEntityMapBaselineService::Directory() const noexcept
    {
        return directory_;
    }

    void SavedEntityMapBaselineService::ObserveCollection(
        void* context,
        const game::entity::persistence::SavedEntityMapCollectionEvent& event)
        noexcept
    {
        auto* const service = static_cast<SavedEntityMapBaselineService*>(
            context);
        if (service == nullptr)
        {
            return;
        }
        using game::entity::persistence::SavedEntityMapCollectionPhase;
        if (event.phase == SavedEntityMapCollectionPhase::Begin)
        {
            service->nativeSavedEntities_ = nullptr;
            service->nativeCollectionReady_ = false;
            for (auto& entry : service->guestBaselines_)
            {
                entry.second.appliedToCurrentCollection = false;
            }
            service->guestCollectionApplied_ = false;
            service->guestCollectionPrepared_ = false;
            service->preparedCollectionIncludesGuestHero_ = false;
            if (service->role_ == PeerRole::Guest)
            {
                service->guestHeroBoundary_.BeginGuestCollection();
                service->localShopBoundary_.BeginGuestCollection();
            }
            if (service->role_ == PeerRole::Host)
            {
                service->directory_.BeginCapture(
                    event.format,
                    event.recordCount);
                service->collectionTransfer_.InvalidateHostCapture();
                service->guestCollectionRevision_ = 0;
            }
            return;
        }

        const bool complete =
            event.phase == SavedEntityMapCollectionPhase::Complete;
        if (service->role_ == PeerRole::Host)
        {
            service->directory_.CompleteCapture(complete);
            service->published_.clear();
            service->outbound_ = {};
        }
        if (!complete)
        {
            return;
        }
        service->nativeSavedEntities_ = event.savedEntities;
        service->nativeCollectionFormat_ = event.format;
        service->nativeCollectionReady_ = event.savedEntities != nullptr;
        if (service->role_ == PeerRole::Guest)
        {
            if (!service->guestHeroBoundary_.CompleteGuestCollection(true))
            {
                service->diagnostics_.Event(
                    "MultiplayerGuestHeroSaveRecordMissing",
                    "the selected save did not contain exactly one supported Hero record");
                return;
            }
            if (!service->ApplyGuestCollection() &&
                service->guestCollectionRevision_ != 0)
            {
                service->diagnostics_.Event(
                    "MultiplayerGuestHostWorldMergeDeferred",
                    "the complete host collection could not yet be rewritten and installed");
            }
        }
    }

    void SavedEntityMapBaselineService::ObserveSnapshot(
        void* context,
        const game::entity::persistence::SavedEntityMapBlobSnapshot& snapshot)
        noexcept
    {
        auto* const service = static_cast<SavedEntityMapBaselineService*>(
            context);
        if (service == nullptr)
        {
            return;
        }
        if (service->role_ == PeerRole::Host)
        {
            service->directory_.Capture(snapshot);
        }
        else
        {
            service->guestHeroBoundary_.ObserveGuestRecord(snapshot);
            service->localShopBoundary_.ObserveGuestRecord(snapshot);
        }
    }

    bool SavedEntityMapBaselineService::StartOutbound(
        std::uint16_t mapId,
        std::uint64_t baselineRevision,
        std::uint64_t peerSetRevision)
    {
        // The host map record is authoritative for map-scoped shared state:
        // NPC rows, doors, shops, and dormant/low-sim state. Global quest
        // state has its own host-authoritative stream. Never
        // replace it with an empty "preserve local player" marker. The local
        // SCRIPT_NAME_HERO save boundary must be preserved by the native
        // Hero/TNG seam; this service intentionally does not suppress or
        // merge the host world record.
        const SavedEntityMapBlob* const blob = directory_.Find(mapId);
        if (blob != nullptr &&
            blob->format != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            ReportTransfer(
                "MultiplayerSavedEntityMapBaselineDeferred",
                mapId,
                baselineRevision,
                "text save record cannot use the validated binary installer");
            return false;
        }
        if (blob != nullptr &&
            blob->bytes.size() >
                (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }
        ++nextTransferId_;
        if (nextTransferId_ == 0)
        {
            ++nextTransferId_;
        }
        outbound_ = {};
        outbound_.format = blob != nullptr
            ? blob->format
            : game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        outbound_.stage = OutboundStage::Begin;
        outbound_.mapId = mapId;
        outbound_.transferId = nextTransferId_;
        outbound_.baselineRevision = baselineRevision;
        outbound_.peerSetRevision = peerSetRevision;
        outbound_.hash = blob != nullptr ? blob->hash : EmptyHash;
        outbound_.metadata = blob != nullptr ? blob->metadata : 0;
        outbound_.totalBytes = blob != nullptr
            ? static_cast<std::uint32_t>(blob->bytes.size())
            : 0;
        outbound_.present = blob != nullptr;
        outbound_.active = true;
        return true;
    }

    bool SavedEntityMapBaselineService::SubmitOutboundMessage(
        protocol::SavedEntityMapBaselineOperation operation,
        const std::uint8_t* chunk,
        std::size_t chunkSize)
    {
        if (transport_ == nullptr || !outbound_.active) return false;
        protocol::SavedEntityMapBaselineMessage message;
        message.operation = operation;
        message.format = outbound_.format;
        message.present = outbound_.present;
        message.mapId = outbound_.mapId;
        message.transferId = outbound_.transferId;
        message.baselineRevision = outbound_.baselineRevision;
        message.metadata = outbound_.metadata;
        message.totalBytes = outbound_.totalBytes;
        message.offset = operation ==
                protocol::SavedEntityMapBaselineOperation::Commit
            ? outbound_.totalBytes
            : outbound_.offset;
        message.hash = outbound_.hash;
        message.chunk = chunk;
        message.chunkSize = chunkSize;
        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        return protocol::EncodeSavedEntityMapBaselineMessage(
                message,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize) &&
            transport_->SubmitReliable(
                reliable_stream::Control,
                protocol::PacketType::SavedEntityMapBaseline,
                payload.data(),
                payloadSize);
    }

    bool SavedEntityMapBaselineService::BeginInbound(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        ResetInbound();
        if (message.format != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            ReportTransfer(
                "MultiplayerSavedEntityMapBaselineRejected",
                message.mapId,
                message.baselineRevision,
                "only validated binary records are accepted");
            return true;
        }
        try
        {
            inbound_.baseline.format = message.format;
            inbound_.baseline.revision = message.baselineRevision;
            inbound_.baseline.hash = message.hash;
            inbound_.baseline.metadata = message.metadata;
            inbound_.baseline.present = message.present;
            if (message.present && message.totalBytes != 0)
            {
                inbound_.baseline.bytes.resize(message.totalBytes);
            }
            inbound_.mapId = message.mapId;
            inbound_.transferId = message.transferId;
            inbound_.totalBytes = message.totalBytes;
            inbound_.active = true;
            return true;
        }
        catch (...)
        {
            ResetInbound();
            return true;
        }
    }

    bool SavedEntityMapBaselineService::AppendInbound(
        const protocol::SavedEntityMapBaselineMessage& message) noexcept
    {
        if (!MatchesInbound(message) || !message.present ||
            message.offset != inbound_.receivedBytes ||
            message.chunkSize >
                inbound_.totalBytes - inbound_.receivedBytes)
        {
            ResetInbound();
            return true;
        }
        std::memcpy(
            inbound_.baseline.bytes.data() + inbound_.receivedBytes,
            message.chunk,
            message.chunkSize);
        inbound_.receivedBytes += static_cast<std::uint32_t>(
            message.chunkSize);
        return true;
    }

    bool SavedEntityMapBaselineService::CommitInbound(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        if (!MatchesInbound(message) ||
            message.offset != inbound_.totalBytes ||
            message.chunkSize != 0 ||
            inbound_.receivedBytes != inbound_.totalBytes ||
            HashBytes(
                inbound_.baseline.bytes.data(),
                inbound_.baseline.bytes.size()) != inbound_.baseline.hash)
        {
            ResetInbound();
            return true;
        }
        const std::uint16_t mapId = inbound_.mapId;
        const std::uint64_t revision = inbound_.baseline.revision;
        GuestBaseline baseline = std::move(inbound_.baseline);
        ResetInbound();
        const bool accepted = AcceptGuestBaseline(mapId, std::move(baseline));
        ReportTransfer(
            accepted
                ? "MultiplayerSavedEntityMapBaselineAccepted"
                : "MultiplayerSavedEntityMapBaselineRejected",
            mapId,
            revision,
            accepted
                ? "validated current host baseline is staged or installed"
                : "guest baseline bounds or native installation failed");
        return true;
    }

    bool SavedEntityMapBaselineService::ConsumeCommittedCollection()
    {
        std::uint64_t revision = 0;
        std::map<std::uint16_t, SavedEntityCollectionRecord> records;
        if (!collectionTransfer_.TakeCommitted(revision, records))
        {
            return true;
        }
        try
        {
            std::map<std::uint16_t, GuestBaseline> replacement;
            std::size_t replacementBytes = 0;
            for (auto& entry : records)
            {
                if (entry.second.bytes.size() > MaximumGuestBaselineBytes -
                        replacementBytes)
                {
                    return false;
                }
                GuestBaseline baseline;
                baseline.format = entry.second.format;
                baseline.revision = entry.second.revision;
                baseline.hash = entry.second.hash;
                baseline.metadata = entry.second.metadata;
                baseline.present = true;
                baseline.bytes = std::move(entry.second.bytes);
                replacementBytes += baseline.bytes.size();
                replacement.emplace(entry.first, std::move(baseline));
            }
            guestBaselines_.swap(replacement);
            guestBaselineBytes_ = replacementBytes;
            guestCollectionRevision_ = revision;
            guestCollectionApplied_ = false;
            guestCollectionPrepared_ = false;
        }
        catch (...)
        {
            return false;
        }
        if (nativeCollectionReady_)
        {
            if (!ApplyGuestCollection())
            {
                diagnostics_.Event(
                    "MultiplayerGuestHostWorldMergeDeferred",
                    "the committed host collection could not be rewritten and installed");
            }
        }
        return true;
    }

    bool SavedEntityMapBaselineService::MatchesInbound(
        const protocol::SavedEntityMapBaselineMessage& message) const noexcept
    {
        return inbound_.active && message.mapId == inbound_.mapId &&
            message.transferId == inbound_.transferId &&
            message.baselineRevision == inbound_.baseline.revision &&
            message.format == inbound_.baseline.format &&
            message.present == inbound_.baseline.present &&
            message.metadata == inbound_.baseline.metadata &&
            message.totalBytes == inbound_.totalBytes &&
            message.hash == inbound_.baseline.hash &&
            !message.collection;
    }

    bool SavedEntityMapBaselineService::AcceptGuestBaseline(
        std::uint16_t mapId,
        GuestBaseline baseline)
    {
        auto existing = guestBaselines_.find(mapId);
        if (existing != guestBaselines_.end() &&
            existing->second.revision > baseline.revision)
        {
            return true;
        }
        const std::size_t previousBytes = existing != guestBaselines_.end()
            ? existing->second.bytes.size()
            : 0;
        if (baseline.bytes.size() > MaximumGuestBaselineBytes -
                (guestBaselineBytes_ - previousBytes))
        {
            return false;
        }
        baseline.appliedToCurrentCollection = false;
        guestCollectionApplied_ = false;
        guestCollectionPrepared_ = false;
        guestBaselineBytes_ = guestBaselineBytes_ - previousBytes +
            baseline.bytes.size();
        if (existing == guestBaselines_.end())
        {
            existing = guestBaselines_.emplace(
                mapId,
                std::move(baseline)).first;
        }
        else
        {
            existing->second = std::move(baseline);
        }
        if (!nativeCollectionReady_)
        {
            return true;
        }
        return ApplyGuestCollection();
    }

    bool SavedEntityMapBaselineService::PrepareGuestCollection()
    {
        if (role_ != PeerRole::Guest || guestCollectionRevision_ == 0 ||
            !guestHeroBoundary_.IsHeroCaptured())
        {
            return false;
        }
        const bool includeGuestHero = initialGuestHeroConstructionPending_;
        if (guestCollectionPrepared_ &&
            preparedCollectionIncludesGuestHero_ == includeGuestHero)
        {
            return true;
        }

        try
        {
            // Rewrite a copy so allocation or compression failure cannot
            // corrupt the last complete authoritative collection.
            std::map<std::uint16_t, SavedEntityCollectionRecord> records;
            for (const auto& [mapId, baseline] : guestBaselines_)
            {
                if (!baseline.present)
                {
                    continue;
                }
                SavedEntityCollectionRecord record;
                record.format = baseline.format;
                record.mapId = mapId;
                record.metadata = baseline.metadata;
                record.hash = baseline.hash;
                record.revision = baseline.revision;
                record.bytes = baseline.bytes;
                record.guestHeroBootstrapOnly = baseline.guestHeroBootstrapOnly;
                records.emplace(mapId, std::move(record));
            }
            if (!guestHeroBoundary_.RewriteHostCollection(
                    guestCollectionRevision_, records, includeGuestHero))
            {
                return false;
            }
            std::map<std::uint16_t, GuestBaseline> rewritten;
            std::size_t rewrittenBytes = 0;
            for (const auto& [mapId, baseline] : guestBaselines_)
            {
                if (!baseline.present)
                {
                    rewritten.emplace(mapId, baseline);
                }
            }
            for (auto& [mapId, record] : records)
            {
                if (record.bytes.size() > MaximumGuestBaselineBytes -
                        rewrittenBytes)
                {
                    return false;
                }
                GuestBaseline baseline;
                baseline.format = record.format;
                baseline.revision = record.revision != 0
                    ? record.revision
                    : guestCollectionRevision_;
                baseline.hash = record.hash;
                baseline.metadata = record.metadata;
                baseline.present = true;
                baseline.bytes = std::move(record.bytes);
                baseline.guestHeroBootstrapOnly = record.guestHeroBootstrapOnly;
                rewrittenBytes += baseline.bytes.size();
                rewritten.insert_or_assign(mapId, std::move(baseline));
            }
            guestBaselines_ = std::move(rewritten);
            guestBaselineBytes_ = rewrittenBytes;
            guestCollectionPrepared_ = true;
            preparedCollectionIncludesGuestHero_ = includeGuestHero;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SavedEntityMapBaselineService::ApplyGuestCollection() noexcept
    {
        if (role_ != PeerRole::Guest || guestCollectionRevision_ == 0 ||
            !nativeCollectionReady_ || nativeSavedEntities_ == nullptr ||
            nativeCollectionFormat_ != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            return false;
        }
        // Preserve the local merchant payloads just before replacing native
        // map records. This runs at baseline application, never as a tick
        // observer, and cannot re-enter the retail load barrier.
        if (observer_ != nullptr)
        {
            for (const auto& [mapId, baseline] : guestBaselines_)
            {
                (void)baseline;
                game::entity::persistence::SavedEntityMapBlobSnapshot snapshot;
                if (observer_->ReadBinarySnapshot(nativeSavedEntities_, mapId, snapshot))
                    localShopBoundary_.ObserveGuestRecord(snapshot);
            }
        }
        if (!PrepareGuestCollection())
        {
            return false;
        }
        if (!installer_.ClearAll(nativeSavedEntities_))
        {
            return false;
        }
        diagnostics_.Event(
            "MultiplayerSavedEntityCollectionAbsentApplied",
            "cleared every guest-native record before installing the committed host-populated set");
        for (auto& [mapId, baseline] : guestBaselines_)
        {
            if (!ApplyGuestBaseline(mapId, baseline))
            {
                return false;
            }
        }
        guestCollectionApplied_ = true;
        return true;
    }

    bool SavedEntityMapBaselineService::ApplyGuestBaseline(
        std::uint16_t mapId,
        GuestBaseline& baseline) noexcept
    {
        baseline.appliedToCurrentCollection = false;
        if (!nativeCollectionReady_ || nativeSavedEntities_ == nullptr ||
            nativeCollectionFormat_ != game::entity::persistence::
                SavedEntityMapBlobFormat::Binary)
        {
            return false;
        }
        bool applied = false;
        if (baseline.present)
        {
            // Project private shop data only into this installation. Keep the
            // authoritative baseline free of a previous selected save's stock.
            SavedEntityCollectionRecord projected;
            try
            {
                projected.format = baseline.format;
                projected.mapId = mapId;
                projected.metadata = baseline.metadata;
                projected.hash = baseline.hash;
                projected.bytes = baseline.bytes;
                if (!localShopBoundary_.RewriteHostRecord(projected))
                {
                    diagnostics_.Event("MultiplayerLocalShopProjectionSkipped",
                        "private shop projection unavailable; retaining native host map data");
                }
            }
            catch (...)
            {
                return false;
            }
            game::entity::persistence::SavedEntityMapBlobSnapshot snapshot;
            snapshot.format = projected.format;
            snapshot.mapId = mapId;
            snapshot.bytes = projected.bytes.empty()
                ? nullptr
                : projected.bytes.data();
            snapshot.byteCount = projected.bytes.size();
            snapshot.metadata = projected.metadata;
            snapshot.hash = projected.hash;
            applied = installer_.Install(nativeSavedEntities_, snapshot);
        }
        else
        {
            applied = installer_.Clear(nativeSavedEntities_, mapId);
        }
        baseline.appliedToCurrentCollection = applied;
        return applied;
    }

    void SavedEntityMapBaselineService::ResetInbound() noexcept
    {
        inbound_ = {};
    }

    void SavedEntityMapBaselineService::ReportTransfer(
        const char* eventName,
        std::uint16_t mapId,
        std::uint64_t revision,
        const char* reason) const noexcept
    {
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map_id=%u revision=%llu role=%s local_actor_id=%llu reason=%s",
            static_cast<unsigned int>(mapId),
            static_cast<unsigned long long>(revision),
            role_ == PeerRole::Host ? "host" : "guest",
            static_cast<unsigned long long>(localActorId_),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event(
            eventName != nullptr
                ? eventName
                : "MultiplayerSavedEntityMapBaseline",
            detail);
    }
}
