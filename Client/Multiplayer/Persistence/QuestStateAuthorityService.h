#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/QuestStateSnapshotMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Game/Quest/Persistence/QuestStateNativeCaptureHook.h"
#include "Game/Persistence/Hooks/GameStateSectionLoadHook.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::persistence
{
    // Transfers only the host's opaque global CQuestManager snapshot. Guest
    // Hero-owned state never enters this service. Native calls are isolated in
    // the small, current-build-validated seam below; the transport remains
    // independent of the CStringParser ABI.
    class QuestStateAuthorityService final : public ReliableMessageSink
    {
    public:
        using GuestApplySink = bool (*)(
            void* context,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;

        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::QuestStateSnapshot};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        static constexpr std::size_t MaximumSnapshotBytes =
            protocol::MaximumQuestStateSnapshotBytes;

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics,
            std::uint32_t authorityEpoch = 0) noexcept;
        void Shutdown() noexcept;

        // Called by an already validated host-side capture boundary. The
        // bytes are copied before any reliable submission and are bounded.
        bool PublishHostSnapshot(
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t snapshotRevision,
            const std::uint8_t* bytes,
            std::size_t byteCount);
        // Called by the native save hook after retail has serialized the
        // manager. Identical serialized state is ignored so periodic captures
        // do not create revisions or reliable traffic without real progress.
        void CaptureHostSerializedBytes(
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        bool CaptureHostCurrent();
        [[nodiscard]] bool CanCaptureHostCurrent() const noexcept
        {
            return role_ == PeerRole::Host && nativeCaptureHook_.IsInstalled();
        }
        // Advances a bounded number of begin/chunk/commit messages. Calling
        // this each game-thread tick provides backpressure without a burst.
        bool Process();

        bool HandleReliableMessage(
            const TransportMessage& message) override;

        // A guest may bind an application override for tests. Without one,
        // the validated native bridge applies the staged manager text at the
        // post-section completion boundary.
        void SetGuestApplySink(
            GuestApplySink sink,
            void* context) noexcept;
        // The ENTITIES construction barrier waits only for delivery. Applying
        // here would be overwritten by the guest QUESTS section, which retail
        // loads later in the same bundle.
        [[nodiscard]] bool IsReadyForGuestWorldLoad() const noexcept;
        // Called after the retail bundle has finished through FACTIONS.
        bool ApplyAfterNativeWorldSections() noexcept;

        void SetExpectedHostActor(std::uint64_t actorId) noexcept;
        [[nodiscard]] bool HasStagedSnapshot() const noexcept
        {
            return staged_.present;
        }
        [[nodiscard]] bool StagedSnapshotApplied() const noexcept
        {
            return staged_.applied;
        }
        [[nodiscard]] std::uint64_t StagedSnapshotRevision() const noexcept
        {
            return staged_.snapshotRevision;
        }
        [[nodiscard]] bool HasCurrentSnapshot() const noexcept
        {
            return role_ == PeerRole::Host
                ? latestHostSnapshot_.present
                : staged_.present;
        }
        [[nodiscard]] std::uint64_t CurrentSnapshotRevision() const noexcept
        {
            return role_ == PeerRole::Host
                ? latestHostSnapshot_.snapshotRevision
                : staged_.snapshotRevision;
        }
        [[nodiscard]] std::uint64_t CurrentSnapshotFingerprint() const noexcept
        {
            return role_ == PeerRole::Host
                ? latestHostSnapshot_.hash
                : staged_.hash;
        }
        [[nodiscard]] std::size_t CurrentSnapshotBytes() const noexcept
        {
            return role_ == PeerRole::Host
                ? latestHostSnapshot_.bytes.size()
                : staged_.bytes.size();
        }

    private:
        enum class SubmissionResult : std::uint8_t
        {
            Submitted,
            Deferred,
            Failed,
        };

        enum class OutboundStage : std::uint8_t
        {
            Begin,
            Chunks,
            Commit,
        };

        struct Outbound final
        {
            OutboundStage stage = OutboundStage::Begin;
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t transferId = 0;
            std::uint64_t hash = 0;
            std::uint64_t peerSetRevision = 0;
            std::uint32_t offset = 0;
            bool active = false;
            std::vector<std::uint8_t> bytes;
        };

        struct LatestHostSnapshot final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t hash = 0;
            bool present = false;
            std::vector<std::uint8_t> bytes;
        };

        struct Inbound final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t transferId = 0;
            std::uint64_t connectionNonce = 0;
            std::uint64_t hash = 0;
            std::uint32_t receivedBytes = 0;
            bool active = false;
            std::vector<std::uint8_t> bytes;
        };

        struct Staged final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t hash = 0;
            bool present = false;
            bool applied = false;
            std::vector<std::uint8_t> bytes;
        };

        [[nodiscard]] SubmissionResult Submit(
            protocol::QuestStateSnapshotOperation operation,
            const std::uint8_t* chunk,
            std::size_t chunkSize);
        bool QueueLatestForPeer(std::uint64_t peerSetRevision);
        bool BeginInbound(
            const TransportMessage& transportMessage,
            const protocol::QuestStateSnapshotMessage& message);
        bool AppendInbound(
            const TransportMessage& transportMessage,
            const protocol::QuestStateSnapshotMessage& message) noexcept;
        bool CommitInbound(
            const TransportMessage& transportMessage,
            const protocol::QuestStateSnapshotMessage& message);
        [[nodiscard]] bool IsHostSource(
            const TransportMessage& message) const noexcept;
        void ResetInbound() noexcept;
        void Report(const char* event, const char* reason) const noexcept;
        static void OnNativeCapture(
            void* context,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        static bool ProvideNativeSnapshot(
            void* context,
            const std::uint8_t*& bytes,
            std::size_t& byteCount) noexcept;
        static void OnNativeApplyResult(
            void* context,
            bool applied) noexcept;
        static void OnNativeSectionsLoaded(void* context) noexcept;

        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t expectedHostActorId_ = 0;
        std::uint64_t latchedHostActorId_ = 0;
        std::uint64_t nextTransferId_ = 0;
        std::uint32_t acceptedAuthorityEpoch_ = 0;
        std::uint64_t acceptedSessionRevision_ = 0;
        std::uint64_t acceptedSnapshotRevision_ = 0;
        std::uint32_t hostAuthorityEpoch_ = 0;
        std::uint64_t hostSessionRevision_ = 0;
        std::uint64_t nextSnapshotRevision_ = 0;
        std::uint64_t lastPublishedPeerSetRevision_ = 0;
        std::uint64_t lastPublishedSnapshotRevision_ = 0;
        GuestApplySink guestApplySink_ = nullptr;
        void* guestApplyContext_ = nullptr;
        Outbound outbound_;
        LatestHostSnapshot latestHostSnapshot_;
        Inbound inbound_;
        Staged staged_;
        QuestStateNativeCaptureHook nativeCaptureHook_;
        game::persistence::GameStateSectionLoadHook sectionLoadHook_;
    };
}
