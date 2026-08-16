#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/Native/SavedEntityMapRecordInstaller.h"
#include "Multiplayer/Authority/MapAuthorityBaselineGate.h"
#include "Multiplayer/Persistence/SavedEntityMapBlobDirectory.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Protocol/SavedEntityMapBaselineMessage.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace fable::game::entity::persistence
{
    class SavedEntityMapBlobObserver;
}

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::persistence
{
    // Owns exactly one bounded current host-save record per native map ID.
    // The host streams begin/chunks/commit ahead of a map Grant; guests retain
    // only the latest validated baseline so it can be re-applied after their
    // local save is loaded again.
    class SavedEntityMapBaselineService final
        : public ReliableMessageSink,
          public authority::MapAuthorityBaselineGate
    {
    public:
        static constexpr std::size_t MaximumGuestBaselineBytes =
            64 * 1024 * 1024;

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics);
        bool Attach(
            game::entity::persistence::SavedEntityMapBlobObserver& observer);
        authority::MapBaselinePreparationResult PrepareHostGrant(
            std::uint16_t mapId,
            std::uint64_t& baselineRevision) override;
        [[nodiscard]] bool IsGuestGrantReady(
            std::uint16_t mapId,
            std::uint64_t baselineRevision) const noexcept override;
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void Shutdown() noexcept;

        [[nodiscard]] const SavedEntityMapBlobDirectory& Directory()
            const noexcept;

    private:
        enum class OutboundStage : std::uint8_t
        {
            Begin,
            Chunks,
            Commit,
        };

        struct OutboundTransfer final
        {
            game::entity::persistence::SavedEntityMapBlobFormat format =
                game::entity::persistence::SavedEntityMapBlobFormat::Binary;
            OutboundStage stage = OutboundStage::Begin;
            std::uint16_t mapId = 0;
            std::uint64_t transferId = 0;
            std::uint64_t baselineRevision = 0;
            std::uint64_t peerSetRevision = 0;
            std::uint64_t hash = 0;
            std::uint32_t metadata = 0;
            std::uint32_t totalBytes = 0;
            std::uint32_t offset = 0;
            bool present = false;
            bool active = false;
        };

        struct PublishedBaseline final
        {
            std::uint64_t baselineRevision = 0;
            std::uint64_t peerSetRevision = 0;
        };

        struct GuestBaseline final
        {
            game::entity::persistence::SavedEntityMapBlobFormat format =
                game::entity::persistence::SavedEntityMapBlobFormat::Binary;
            std::uint64_t revision = 0;
            std::uint64_t hash = 0;
            std::uint32_t metadata = 0;
            bool present = false;
            bool appliedToCurrentCollection = false;
            std::vector<std::uint8_t> bytes;
        };

        struct InboundTransfer final
        {
            GuestBaseline baseline;
            std::uint16_t mapId = 0;
            std::uint64_t transferId = 0;
            std::uint32_t totalBytes = 0;
            std::uint32_t receivedBytes = 0;
            bool active = false;
        };

        static void ObserveCollection(
            void* context,
            const game::entity::persistence::SavedEntityMapCollectionEvent&
                event) noexcept;
        static void ObserveSnapshot(
            void* context,
            const game::entity::persistence::SavedEntityMapBlobSnapshot&
                snapshot) noexcept;
        bool StartOutbound(
            std::uint16_t mapId,
            std::uint64_t baselineRevision,
            std::uint64_t peerSetRevision);
        bool SubmitOutboundMessage(
            protocol::SavedEntityMapBaselineOperation operation,
            const std::uint8_t* chunk,
            std::size_t chunkSize);
        bool BeginInbound(
            const protocol::SavedEntityMapBaselineMessage& message);
        bool AppendInbound(
            const protocol::SavedEntityMapBaselineMessage& message) noexcept;
        bool CommitInbound(
            const protocol::SavedEntityMapBaselineMessage& message);
        [[nodiscard]] bool MatchesInbound(
            const protocol::SavedEntityMapBaselineMessage& message) const
            noexcept;
        bool AcceptGuestBaseline(
            std::uint16_t mapId,
            GuestBaseline baseline);
        bool ApplyGuestBaseline(
            std::uint16_t mapId,
            GuestBaseline& baseline) noexcept;
        void ApplyGuestBaselines() noexcept;
        void ResetInbound() noexcept;
        void ReportTransfer(
            const char* eventName,
            std::uint16_t mapId,
            std::uint64_t revision,
            const char* reason) const noexcept;

        SavedEntityMapBlobDirectory directory_;
        game::entity::persistence::native::SavedEntityMapRecordInstaller
            installer_;
        game::entity::persistence::SavedEntityMapBlobObserver* observer_ =
            nullptr;
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextTransferId_ = 0;
        OutboundTransfer outbound_;
        InboundTransfer inbound_;
        std::map<std::uint16_t, PublishedBaseline> published_;
        std::map<std::uint16_t, GuestBaseline> guestBaselines_;
        std::size_t guestBaselineBytes_ = 0;
        void* nativeSavedEntities_ = nullptr;
        game::entity::persistence::SavedEntityMapBlobFormat
            nativeCollectionFormat_ = game::entity::persistence::
                SavedEntityMapBlobFormat::Binary;
        bool nativeCollectionReady_ = false;
    };
}
