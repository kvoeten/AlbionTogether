#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Authority/MapAuthorityBaselineGate.h"
#include "Multiplayer/Persistence/SavedEntityMapBlobDirectory.h"
#include "Multiplayer/Protocol/SavedEntityMapBaselineMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace fable::multiplayer { class UdpPeer; }

namespace fable::multiplayer::persistence
{
    struct SavedEntityCollectionRecord final
    {
        game::entity::persistence::SavedEntityMapBlobFormat format =
            game::entity::persistence::SavedEntityMapBlobFormat::Binary;
        std::uint16_t mapId = 0;
        std::uint32_t metadata = 0;
        std::uint64_t hash = 0;
        std::uint64_t revision = 0;
        std::vector<std::uint8_t> bytes;
        // Local overlay provenance, never part of the transport payload.
        // Only a Hero-only cell for an absent host map carries this marker.
        bool guestHeroBootstrapOnly = false;
    };

    // Owns only the ordered collection begin/record/chunk/commit protocol.
    // The baseline service remains responsible for native observation,
    // installation, and ordinary per-map grants.
    class SavedEntityCollectionBaselineTransfer final
    {
    public:
        static constexpr std::size_t MaximumRecords =
            SavedEntityMapBlobDirectory::MaximumMapRecords;
        static constexpr std::size_t MaximumBytes = 64 * 1024 * 1024;

        void Initialize(
            PeerRole role,
            UdpPeer& transport,
            SavedEntityMapBlobDirectory& directory,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void InvalidateHostCapture() noexcept;
        authority::MapBaselinePreparationResult PrepareHost(
            std::uint64_t peerSetRevision);
        bool Handle(const protocol::SavedEntityMapBaselineMessage& message);
        bool TakeCommitted(
            std::uint64_t& revision,
            std::map<std::uint16_t, SavedEntityCollectionRecord>& records);

    private:
        enum class HostStage : std::uint8_t
        {
            Begin, RecordBegin, RecordChunks, RecordCommit, Commit,
        };
        struct HostState final
        {
            HostStage stage = HostStage::Begin;
            std::uint64_t revision = 0;
            std::uint64_t peerSetRevision = 0;
            std::uint64_t transferId = 0;
            std::uint16_t recordIndex = 0;
            std::uint32_t offset = 0;
            std::vector<std::uint16_t> mapIds;
            bool active = false;
        };
        struct InboundState final
        {
            SavedEntityCollectionRecord record;
            std::uint64_t transferId = 0;
            std::uint32_t totalBytes = 0;
            std::uint32_t receivedBytes = 0;
            std::uint16_t recordIndex = 0;
            bool active = false;
        };
        struct CollectionState final
        {
            std::uint64_t revision = 0;
            std::uint64_t transferId = 0;
            std::uint16_t recordCount = 0;
            std::uint16_t receivedRecords = 0;
            std::size_t totalBytes = 0;
            std::map<std::uint16_t, SavedEntityCollectionRecord> records;
            bool active = false;
        };

        authority::MapBaselinePreparationResult FailHostTransfer(
            const char* reason) noexcept;
        bool SubmitMarker(protocol::SavedEntityMapBaselineOperation operation);
        bool SubmitRecord(
            protocol::SavedEntityMapBaselineOperation operation,
            const SavedEntityMapBlob& blob,
            std::uint64_t transferId,
            std::uint16_t recordCount,
            std::uint16_t recordIndex,
            std::uint32_t offset,
            const std::uint8_t* chunk,
            std::size_t chunkSize);
        bool BeginCollection(const protocol::SavedEntityMapBaselineMessage& message);
        bool BeginRecord(const protocol::SavedEntityMapBaselineMessage& message);
        bool AppendRecord(const protocol::SavedEntityMapBaselineMessage& message);
        bool CommitRecord(const protocol::SavedEntityMapBaselineMessage& message);
        bool CommitCollection(const protocol::SavedEntityMapBaselineMessage& message);
        bool MatchesRecord(const protocol::SavedEntityMapBaselineMessage& message) const noexcept;

        PeerRole role_ = PeerRole::Guest;
        UdpPeer* transport_ = nullptr;
        SavedEntityMapBlobDirectory* directory_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t nextTransferId_ = 0;
        std::uint64_t publishedRevision_ = 0;
        std::uint64_t publishedPeerSetRevision_ = 0;
        HostState host_;
        InboundState inbound_;
        CollectionState collection_;
        std::uint64_t committedRevision_ = 0;
        std::map<std::uint16_t, SavedEntityCollectionRecord> committedRecords_;
    };
}
