#include "SavedEntityCollectionBaselineTransfer.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace
{
    constexpr std::uint64_t EmptyHash = 14695981039346656037ull;
    constexpr std::size_t SubmissionBudget = 32;

    std::uint64_t HashBytes(
        const std::uint8_t* bytes,
        const std::size_t count) noexcept
    {
        std::uint64_t hash = EmptyHash;
        for (std::size_t i = 0; i < count; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

namespace fable::multiplayer::persistence
{
    void SavedEntityCollectionBaselineTransfer::Initialize(
        const PeerRole role,
        UdpPeer& transport,
        SavedEntityMapBlobDirectory& directory,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        transport_ = &transport;
        directory_ = &directory;
        diagnostics_ = diagnostics;
    }

    void SavedEntityCollectionBaselineTransfer::Shutdown() noexcept
    {
        role_ = PeerRole::Guest;
        transport_ = nullptr;
        directory_ = nullptr;
        diagnostics_ = {};
        nextTransferId_ = 0;
        publishedRevision_ = 0;
        publishedPeerSetRevision_ = 0;
        host_ = {};
        inbound_ = {};
        collection_ = {};
        committedRevision_ = 0;
        committedRecords_.clear();
    }

    void SavedEntityCollectionBaselineTransfer::InvalidateHostCapture() noexcept
    {
        host_ = {};
        publishedRevision_ = 0;
        publishedPeerSetRevision_ = 0;
    }

    authority::MapBaselinePreparationResult
    SavedEntityCollectionBaselineTransfer::PrepareHost(
        const std::uint64_t peerSetRevision)
    {
        using authority::MapBaselinePreparationResult;

        if (role_ != PeerRole::Host || transport_ == nullptr || directory_ == nullptr ||
            !directory_->IsComplete())
        {
            return MapBaselinePreparationResult::Deferred;
        }

        const std::uint64_t revision = directory_->CaptureRevision();
        if (revision == 0)
        {
            return MapBaselinePreparationResult::Deferred;
        }

        if (!host_.active && publishedRevision_ == revision &&
            publishedPeerSetRevision_ == peerSetRevision)
        {
            return MapBaselinePreparationResult::Ready;
        }

        if (host_.active &&
            (host_.revision != revision || host_.peerSetRevision != peerSetRevision))
        {
            host_ = {};
        }

        if (!host_.active)
        {
            host_.active = true;
            host_.revision = revision;
            host_.peerSetRevision = peerSetRevision;
            host_.stage = HostStage::Begin;
            host_.mapIds = directory_->PopulatedMapIds();
            host_.recordIndex = 0;
            ++nextTransferId_;
            if (nextTransferId_ == 0)
            {
                ++nextTransferId_;
            }
            host_.transferId = nextTransferId_;
        }

        for (std::size_t i = 0; i < SubmissionBudget; ++i)
        {
            if (directory_->CaptureRevision() != host_.revision)
            {
                host_ = {};
                return MapBaselinePreparationResult::Deferred;
            }

            if (host_.stage == HostStage::Begin)
            {
                if (!SubmitMarker(
                        protocol::SavedEntityMapBaselineOperation::CollectionBegin))
                {
                    return transport_->HasFailed()
                        ? FailHostTransfer(
                            "ordered transport failed while publishing the collection begin")
                        : MapBaselinePreparationResult::Deferred;
                }
                host_.stage = host_.mapIds.empty()
                    ? HostStage::Commit
                    : HostStage::RecordBegin;
                continue;
            }

            if (host_.stage == HostStage::RecordBegin)
            {
                const SavedEntityMapBlob* blob =
                    directory_->Find(host_.mapIds[host_.recordIndex]);
                if (blob == nullptr ||
                    blob->format != game::entity::persistence::SavedEntityMapBlobFormat::Binary ||
                    blob->bytes.size() > (std::numeric_limits<std::uint32_t>::max)())
                {
                    return FailHostTransfer(
                        "captured collection record was missing or invalid");
                }

                host_.offset = 0;
                if (!SubmitRecord(
                        protocol::SavedEntityMapBaselineOperation::Begin,
                        *blob,
                        host_.transferId,
                        static_cast<std::uint16_t>(host_.mapIds.size()),
                        host_.recordIndex,
                        0,
                        nullptr,
                        0))
                {
                    return transport_->HasFailed()
                        ? FailHostTransfer(
                            "ordered transport failed while publishing a record begin")
                        : MapBaselinePreparationResult::Deferred;
                }
                host_.stage = blob->bytes.empty()
                    ? HostStage::RecordCommit
                    : HostStage::RecordChunks;
                continue;
            }

            if (host_.stage == HostStage::Commit)
            {
                if (!SubmitMarker(
                        protocol::SavedEntityMapBaselineOperation::CollectionCommit))
                {
                    return transport_->HasFailed()
                        ? FailHostTransfer(
                            "ordered transport failed while publishing the collection commit")
                        : MapBaselinePreparationResult::Deferred;
                }
                publishedRevision_ = host_.revision;
                publishedPeerSetRevision_ = host_.peerSetRevision;
                host_.active = false;
                diagnostics_.Event(
                    "MultiplayerSavedEntityCollectionPublished",
                    "complete host world collection is ordered ahead of remote map grants");
                return MapBaselinePreparationResult::Ready;
            }

            if (host_.recordIndex >= host_.mapIds.size())
            {
                return FailHostTransfer(
                    "collection record cursor exceeded its captured roster");
            }

            const SavedEntityMapBlob* blob =
                directory_->Find(host_.mapIds[host_.recordIndex]);
            if (blob == nullptr)
            {
                return FailHostTransfer(
                    "captured collection record disappeared during transfer");
            }

            if (host_.stage == HostStage::RecordChunks)
            {
                const std::size_t size = (std::min)(
                    protocol::MaximumSavedEntityMapChunkBytes(),
                    blob->bytes.size() - host_.offset);
                if (!SubmitRecord(
                        protocol::SavedEntityMapBaselineOperation::Chunk,
                        *blob,
                        host_.transferId,
                        static_cast<std::uint16_t>(host_.mapIds.size()),
                        host_.recordIndex,
                        host_.offset,
                        blob->bytes.data() + host_.offset,
                        size))
                {
                    return transport_->HasFailed()
                        ? FailHostTransfer(
                            "ordered transport failed while publishing a record chunk")
                        : MapBaselinePreparationResult::Deferred;
                }
                host_.offset += static_cast<std::uint32_t>(size);
                if (host_.offset == blob->bytes.size())
                {
                    host_.stage = HostStage::RecordCommit;
                }
                continue;
            }

            if (host_.stage == HostStage::RecordCommit)
            {
                if (!SubmitRecord(
                        protocol::SavedEntityMapBaselineOperation::Commit,
                        *blob,
                        host_.transferId,
                        static_cast<std::uint16_t>(host_.mapIds.size()),
                        host_.recordIndex,
                        static_cast<std::uint32_t>(blob->bytes.size()),
                        nullptr,
                        0))
                {
                    return transport_->HasFailed()
                        ? FailHostTransfer(
                            "ordered transport failed while publishing a record commit")
                        : MapBaselinePreparationResult::Deferred;
                }
                ++host_.recordIndex;
                host_.stage = host_.recordIndex == host_.mapIds.size()
                    ? HostStage::Commit
                    : HostStage::RecordBegin;
                continue;
            }
        }

        return MapBaselinePreparationResult::Deferred;
    }

    authority::MapBaselinePreparationResult
    SavedEntityCollectionBaselineTransfer::FailHostTransfer(
        const char* const reason) noexcept
    {
        diagnostics_.Event(
            "MultiplayerSavedEntityCollectionFailed",
            reason != nullptr ? reason : "unknown collection transfer failure");
        host_ = {};
        return authority::MapBaselinePreparationResult::Failed;
    }

    bool SavedEntityCollectionBaselineTransfer::SubmitMarker(
        const protocol::SavedEntityMapBaselineOperation operation)
    {
        protocol::SavedEntityMapBaselineMessage message;
        message.operation = operation;
        message.format = game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        message.present = true;
        message.collection = true;
        message.collectionRecordCount =
            static_cast<std::uint16_t>(host_.mapIds.size());
        message.collectionRecordIndex = operation ==
            protocol::SavedEntityMapBaselineOperation::CollectionCommit
            ? message.collectionRecordCount
            : 0;
        message.transferId = host_.transferId;
        message.baselineRevision = host_.revision;
        message.hash = EmptyHash;

        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t size = 0;
        return protocol::EncodeSavedEntityMapBaselineMessage(
                   message,
                   payload.data(),
                   protocol::MaximumPayloadBytes(),
                   size) &&
            transport_->SubmitReliable(
                reliable_stream::Control,
                protocol::PacketType::SavedEntityMapBaseline,
                payload.data(),
                size);
    }

    bool SavedEntityCollectionBaselineTransfer::SubmitRecord(
        const protocol::SavedEntityMapBaselineOperation operation,
        const SavedEntityMapBlob& blob,
        const std::uint64_t transferId,
        const std::uint16_t recordCount,
        const std::uint16_t recordIndex,
        const std::uint32_t offset,
        const std::uint8_t* chunk,
        const std::size_t chunkSize)
    {
        protocol::SavedEntityMapBaselineMessage message;
        message.operation = operation;
        message.format = blob.format;
        message.present = true;
        message.collection = true;
        message.mapId = static_cast<std::uint16_t>(blob.mapId);
        message.collectionRecordCount = recordCount;
        message.collectionRecordIndex = recordIndex;
        message.transferId = transferId;
        message.baselineRevision = host_.revision;
        message.metadata = blob.metadata;
        message.totalBytes = static_cast<std::uint32_t>(blob.bytes.size());
        message.offset = offset;
        message.hash = blob.hash;
        message.chunk = chunk;
        message.chunkSize = chunkSize;

        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t size = 0;
        return protocol::EncodeSavedEntityMapBaselineMessage(
                   message,
                   payload.data(),
                   protocol::MaximumPayloadBytes(),
                   size) &&
            transport_->SubmitReliable(
                reliable_stream::Control,
                protocol::PacketType::SavedEntityMapBaseline,
                payload.data(),
                size);
    }

    bool SavedEntityCollectionBaselineTransfer::Handle(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        if (role_ != PeerRole::Guest)
        {
            return true;
        }
        if (message.operation ==
            protocol::SavedEntityMapBaselineOperation::CollectionBegin)
        {
            return BeginCollection(message);
        }
        if (message.operation ==
            protocol::SavedEntityMapBaselineOperation::CollectionCommit)
        {
            return CommitCollection(message);
        }
        if (!message.collection)
        {
            return true;
        }
        if (message.operation == protocol::SavedEntityMapBaselineOperation::Begin)
        {
            return BeginRecord(message);
        }
        if (message.operation == protocol::SavedEntityMapBaselineOperation::Chunk)
        {
            return AppendRecord(message);
        }
        return CommitRecord(message);
    }

    bool SavedEntityCollectionBaselineTransfer::BeginCollection(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        inbound_ = {};
        collection_ = {};
        collection_.revision = message.baselineRevision;
        collection_.transferId = message.transferId;
        collection_.recordCount = message.collectionRecordCount;
        collection_.active = true;
        return true;
    }

    bool SavedEntityCollectionBaselineTransfer::BeginRecord(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        inbound_ = {};
        if (!collection_.active || !message.present ||
            message.transferId != collection_.transferId ||
            message.baselineRevision != collection_.revision ||
            message.collectionRecordCount != collection_.recordCount ||
            message.collectionRecordIndex != collection_.receivedRecords)
        {
            return true;
        }

        inbound_.record.format = message.format;
        inbound_.record.mapId = message.mapId;
        inbound_.record.metadata = message.metadata;
        inbound_.record.hash = message.hash;
        inbound_.record.revision = message.baselineRevision;
        inbound_.transferId = message.transferId;
        inbound_.totalBytes = message.totalBytes;
        inbound_.recordIndex = message.collectionRecordIndex;
        try
        {
            inbound_.record.bytes.resize(message.totalBytes);
        }
        catch (...)
        {
            inbound_ = {};
            return true;
        }
        inbound_.active = true;
        return true;
    }

    bool SavedEntityCollectionBaselineTransfer::MatchesRecord(
        const protocol::SavedEntityMapBaselineMessage& message) const noexcept
    {
        return inbound_.active && message.collection &&
            message.mapId == inbound_.record.mapId &&
            message.transferId == inbound_.transferId &&
            message.baselineRevision == inbound_.record.revision &&
            message.collectionRecordCount == collection_.recordCount &&
            message.collectionRecordIndex == inbound_.recordIndex &&
            message.totalBytes == inbound_.totalBytes &&
            message.hash == inbound_.record.hash;
    }

    bool SavedEntityCollectionBaselineTransfer::AppendRecord(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        if (!MatchesRecord(message) || message.offset != inbound_.receivedBytes ||
            message.chunkSize > inbound_.totalBytes - inbound_.receivedBytes)
        {
            inbound_ = {};
            collection_ = {};
            return true;
        }

        std::memcpy(
            inbound_.record.bytes.data() + inbound_.receivedBytes,
            message.chunk,
            message.chunkSize);
        inbound_.receivedBytes += static_cast<std::uint32_t>(message.chunkSize);
        return true;
    }

    bool SavedEntityCollectionBaselineTransfer::CommitRecord(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        if (!MatchesRecord(message) || message.offset != inbound_.totalBytes ||
            message.chunkSize != 0 || inbound_.receivedBytes != inbound_.totalBytes ||
            HashBytes(inbound_.record.bytes.data(), inbound_.record.bytes.size()) !=
                inbound_.record.hash)
        {
            inbound_ = {};
            collection_ = {};
            return true;
        }
        if (collection_.records.find(inbound_.record.mapId) != collection_.records.end() ||
            inbound_.totalBytes > MaximumBytes - collection_.totalBytes)
        {
            inbound_ = {};
            collection_ = {};
            return true;
        }

        collection_.totalBytes += inbound_.totalBytes;
        collection_.records.emplace(
            inbound_.record.mapId,
            std::move(inbound_.record));
        ++collection_.receivedRecords;
        inbound_ = {};
        return true;
    }

    bool SavedEntityCollectionBaselineTransfer::CommitCollection(
        const protocol::SavedEntityMapBaselineMessage& message)
    {
        if (!collection_.active || message.transferId != collection_.transferId ||
            message.baselineRevision != collection_.revision ||
            message.collectionRecordCount != collection_.recordCount ||
            message.collectionRecordIndex != collection_.recordCount || inbound_.active ||
            collection_.receivedRecords != collection_.recordCount)
        {
            collection_ = {};
            inbound_ = {};
            return true;
        }

        committedRevision_ = collection_.revision;
        committedRecords_ = std::move(collection_.records);
        collection_ = {};
        return true;
    }

    bool SavedEntityCollectionBaselineTransfer::TakeCommitted(
        std::uint64_t& revision,
        std::map<std::uint16_t, SavedEntityCollectionRecord>& records)
    {
        if (committedRevision_ == 0)
        {
            return false;
        }
        revision = committedRevision_;
        records = std::move(committedRecords_);
        committedRevision_ = 0;
        return true;
    }
}
