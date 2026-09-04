#include "QuestStateAuthorityService.h"

#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"
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
    constexpr std::size_t SubmissionBudget = 16;

    std::uint64_t HashBytes(
        const std::uint8_t* bytes,
        const std::size_t byteCount) noexcept
    {
        std::uint64_t hash = EmptyHash;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    fable::multiplayer::ReliableMessageSink* ResolveQuestStateSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.world.questState;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkQuestState,
    0x1009u,
    "quest-state-snapshot",
    810u,
    "multiplayer-quest-state-snapshot-dispatch",
    ResolveQuestStateSink);

namespace fable::multiplayer::persistence
{
    void QuestStateAuthorityService::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        UdpPeer& transport,
        const core::Diagnostics& diagnostics,
        const std::uint32_t authorityEpoch) noexcept
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        diagnostics_ = diagnostics;
        nativeCaptureHook_.BindGameModule(GetModuleHandleW(nullptr), diagnostics_);
        hostAuthorityEpoch_ = authorityEpoch == 0 ? 1 : authorityEpoch;
        hostSessionRevision_ = transport.ConnectionNonce();
        if (hostSessionRevision_ == 0) hostSessionRevision_ = 1;
        if (role_ == PeerRole::Host && !nativeCaptureHook_.Install(
                GetModuleHandleW(nullptr),
                &QuestStateAuthorityService::OnNativeCapture,
                this,
                diagnostics_))
        {
            diagnostics_.Event(
                "MultiplayerQuestStateNativeCaptureBlocked",
                "validated native SaveGameState hook could not be installed; no host snapshot is published automatically");
        }
        if (role_ == PeerRole::Guest)
        {
            const bool loadOverrideReady =
                nativeCaptureHook_.InstallLoadOverride(
                    GetModuleHandleW(nullptr),
                    &QuestStateAuthorityService::ProvideNativeSnapshot,
                    &QuestStateAuthorityService::OnNativeApplyResult,
                    this,
                    diagnostics_);
            // The bundle-completion hook is only a compatibility fallback.
            // The exact CQuestManager load seam is preferred because it
            // replaces QUESTS before retail proceeds to REGIONS/FACTIONS.
            if (!loadOverrideReady && !sectionLoadHook_.Install(
                    GetModuleHandleW(nullptr),
                    &QuestStateAuthorityService::OnNativeSectionsLoaded,
                    this,
                    diagnostics_))
            {
                diagnostics_.Event(
                    "MultiplayerQuestStateSectionBoundaryBlocked",
                    "neither the exact QUESTS load override nor the post-bundle fallback could be installed");
            }
        }
    }

    void QuestStateAuthorityService::Shutdown() noexcept
    {
        sectionLoadHook_.Shutdown();
        nativeCaptureHook_.Shutdown();
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        expectedHostActorId_ = 0;
        latchedHostActorId_ = 0;
        nextTransferId_ = 0;
        acceptedAuthorityEpoch_ = 0;
        acceptedSessionRevision_ = 0;
        acceptedSnapshotRevision_ = 0;
        hostAuthorityEpoch_ = 0;
        hostSessionRevision_ = 0;
        nextSnapshotRevision_ = 0;
        lastPublishedPeerSetRevision_ = 0;
        lastPublishedSnapshotRevision_ = 0;
        guestApplySink_ = nullptr;
        guestApplyContext_ = nullptr;
        outbound_ = {};
        latestHostSnapshot_ = {};
        inbound_ = {};
        staged_ = {};
    }

    void QuestStateAuthorityService::CaptureHostSerializedBytes(
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        if (role_ != PeerRole::Host || hostAuthorityEpoch_ == 0 ||
            hostSessionRevision_ == 0 || byteCount > MaximumSnapshotBytes ||
            (byteCount != 0 && bytes == nullptr))
        {
            return;
        }

        const std::uint64_t hash = HashBytes(bytes, byteCount);
        const bool unchanged = latestHostSnapshot_.present &&
            latestHostSnapshot_.hash == hash &&
            latestHostSnapshot_.bytes.size() == byteCount &&
            (byteCount == 0 || std::memcmp(
                latestHostSnapshot_.bytes.data(), bytes, byteCount) == 0);
        if (unchanged)
        {
            return;
        }

        ++nextSnapshotRevision_;
        if (nextSnapshotRevision_ == 0) ++nextSnapshotRevision_;
        if (!PublishHostSnapshot(
                hostAuthorityEpoch_,
                hostSessionRevision_,
                nextSnapshotRevision_,
                bytes,
                byteCount))
        {
            Report(
                "MultiplayerQuestStateNativeCaptureBlocked",
                "bounded host SaveGameState output could not be queued");
        }
    }

    bool QuestStateAuthorityService::CaptureHostCurrent()
    {
        return role_ == PeerRole::Host && nativeCaptureHook_.CaptureCurrent();
    }

    bool QuestStateAuthorityService::PublishHostSnapshot(
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t snapshotRevision,
        const std::uint8_t* bytes,
        const std::size_t byteCount)
    {
        if (role_ != PeerRole::Host || transport_ == nullptr ||
            authorityEpoch == 0 || sessionRevision == 0 ||
            snapshotRevision == 0 || byteCount > MaximumSnapshotBytes ||
            (byteCount != 0 && bytes == nullptr) ||
            byteCount > (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }
        try
        {
            std::vector<std::uint8_t> copy;
            if (byteCount != 0)
            {
                copy.assign(bytes, bytes + byteCount);
            }
            latestHostSnapshot_ = {};
            latestHostSnapshot_.authorityEpoch = authorityEpoch;
            latestHostSnapshot_.sessionRevision = sessionRevision;
            latestHostSnapshot_.snapshotRevision = snapshotRevision;
            latestHostSnapshot_.hash = HashBytes(copy.data(), copy.size());
            latestHostSnapshot_.bytes = std::move(copy);
            latestHostSnapshot_.present = true;
            return QueueLatestForPeer(transport_->PeerSetRevision());
        }
        catch (...)
        {
            outbound_ = {};
            return false;
        }
    }

    bool QuestStateAuthorityService::Process()
    {
        if (transport_ == nullptr || transport_->HasFailed())
        {
            return transport_ != nullptr && !transport_->HasFailed();
        }
        const bool needsLatestHostPublish =
            role_ == PeerRole::Host && latestHostSnapshot_.present &&
            !outbound_.active &&
            (transport_->PeerSetRevision() != lastPublishedPeerSetRevision_ ||
                latestHostSnapshot_.snapshotRevision !=
                    lastPublishedSnapshotRevision_);
        if (needsLatestHostPublish &&
            !QueueLatestForPeer(transport_->PeerSetRevision()))
        {
            return false;
        }
        for (std::size_t submitted = 0; submitted < SubmissionBudget;
             ++submitted)
        {
            if (!outbound_.active)
            {
                return true;
            }
            if (outbound_.stage == OutboundStage::Begin)
            {
                const SubmissionResult result = Submit(
                    protocol::QuestStateSnapshotOperation::Begin,
                    nullptr,
                    0);
                if (result != SubmissionResult::Submitted)
                {
                    return result == SubmissionResult::Deferred;
                }
                outbound_.stage = outbound_.bytes.empty()
                    ? OutboundStage::Commit
                    : OutboundStage::Chunks;
                continue;
            }
            if (outbound_.stage == OutboundStage::Chunks)
            {
                const std::size_t chunkSize = (std::min)(
                    protocol::MaximumQuestStateSnapshotChunkBytes(),
                    outbound_.bytes.size() - outbound_.offset);
                if (chunkSize == 0)
                {
                    return false;
                }
                const SubmissionResult result = Submit(
                    protocol::QuestStateSnapshotOperation::Chunk,
                    outbound_.bytes.data() + outbound_.offset,
                    chunkSize);
                if (result != SubmissionResult::Submitted)
                {
                    return result == SubmissionResult::Deferred;
                }
                outbound_.offset += static_cast<std::uint32_t>(chunkSize);
                if (outbound_.offset == outbound_.bytes.size())
                {
                    outbound_.stage = OutboundStage::Commit;
                }
                continue;
            }
            const SubmissionResult result = Submit(
                protocol::QuestStateSnapshotOperation::Commit,
                nullptr,
                0);
            if (result != SubmissionResult::Submitted)
            {
                return result == SubmissionResult::Deferred;
            }
            Report("MultiplayerQuestStateSnapshotPublished",
                "host snapshot begin/chunks/commit queued reliably");
            lastPublishedPeerSetRevision_ = outbound_.peerSetRevision;
            lastPublishedSnapshotRevision_ = outbound_.snapshotRevision;
            outbound_ = {};
            return true;
        }
        return true;
    }

    bool QuestStateAuthorityService::QueueLatestForPeer(
        const std::uint64_t peerSetRevision)
    {
        if (!latestHostSnapshot_.present || latestHostSnapshot_.bytes.size() >
                MaximumSnapshotBytes)
        {
            return false;
        }
        ++nextTransferId_;
        if (nextTransferId_ == 0)
        {
            ++nextTransferId_;
        }
        outbound_ = {};
        outbound_.authorityEpoch = latestHostSnapshot_.authorityEpoch;
        outbound_.sessionRevision = latestHostSnapshot_.sessionRevision;
        outbound_.snapshotRevision = latestHostSnapshot_.snapshotRevision;
        outbound_.transferId = nextTransferId_;
        outbound_.hash = latestHostSnapshot_.hash;
        outbound_.peerSetRevision = peerSetRevision;
        outbound_.bytes = latestHostSnapshot_.bytes;
        outbound_.active = true;
        return true;
    }

    QuestStateAuthorityService::SubmissionResult
    QuestStateAuthorityService::Submit(
        const protocol::QuestStateSnapshotOperation operation,
        const std::uint8_t* chunk,
        const std::size_t chunkSize)
    {
        std::array<std::uint8_t, protocol::MaximumReliableMessageBytes> bytes =
            {};
        protocol::QuestStateSnapshotMessage message;
        message.operation = operation;
        message.authorityEpoch = outbound_.authorityEpoch;
        message.sessionRevision = outbound_.sessionRevision;
        message.snapshotRevision = outbound_.snapshotRevision;
        message.transferId = outbound_.transferId;
        message.totalBytes = static_cast<std::uint32_t>(outbound_.bytes.size());
        message.offset = outbound_.offset;
        message.hash = outbound_.hash;
        message.chunk = chunk;
        message.chunkSize = chunkSize;
        std::size_t encodedSize = 0;
        if (!protocol::EncodeQuestStateSnapshotMessage(message, bytes.data(),
                bytes.size(), encodedSize))
        {
            return SubmissionResult::Failed;
        }
        if (transport_ == nullptr || transport_->HasFailed())
        {
            return SubmissionResult::Failed;
        }
        if (!transport_->SubmitReliable(
                reliable_stream::Control,
                protocol::PacketType::QuestStateSnapshot,
                bytes.data(),
                encodedSize))
        {
            // A full reliable window is ordinary backpressure. Keep the
            // current stage and retry it on the next game-thread pass.
            return transport_->HasFailed()
                ? SubmissionResult::Failed
                : SubmissionResult::Deferred;
        }
        return SubmissionResult::Submitted;
    }

    void QuestStateAuthorityService::SetExpectedHostActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0)
        {
            return;
        }
        if (latchedHostActorId_ != 0 && latchedHostActorId_ != actorId)
        {
            // A provisional Begin arrived before PlayerActorState. If that
            // source is disproved, never allow its staged global quest state
            // to cross the construction gate. Require a fresh Begin from
            // the confirmed actor for this session.
            expectedHostActorId_ = actorId;
            latchedHostActorId_ = actorId;
            ResetInbound();
            staged_ = {};
            acceptedAuthorityEpoch_ = 0;
            acceptedSessionRevision_ = 0;
            acceptedSnapshotRevision_ = 0;
            Report("MultiplayerQuestStateSnapshotRejected",
                "player-state host identity disagreed with provisional quest source; staged state was invalidated");
            return;
        }
        expectedHostActorId_ = actorId;
        if (latchedHostActorId_ == 0)
        {
            latchedHostActorId_ = actorId;
        }
    }

    void QuestStateAuthorityService::SetGuestApplySink(
        const GuestApplySink sink,
        void* const context) noexcept
    {
        guestApplySink_ = sink;
        guestApplyContext_ = context;
        if (sink == nullptr)
        {
            staged_.applied = false;
        }
    }

    bool QuestStateAuthorityService::IsReadyForGuestWorldLoad() const noexcept
    {
        return role_ == PeerRole::Guest && staged_.present;
    }

    bool QuestStateAuthorityService::ApplyAfterNativeWorldSections() noexcept
    {
        if (role_ != PeerRole::Guest || !staged_.present)
        {
            Report("MultiplayerQuestStateSnapshotApplyBlocked",
                "no complete host snapshot is staged after the native world sections");
            return false;
        }
        bool applied = false;
        if (guestApplySink_ != nullptr)
        {
            try
            {
                applied = guestApplySink_(guestApplyContext_, staged_.bytes.data(),
                    staged_.bytes.size());
            }
            catch (...)
            {
                applied = false;
            }
        }
        else
        {
            applied = nativeCaptureHook_.ApplySnapshot(
                staged_.bytes.data(), staged_.bytes.size());
        }
        staged_.applied = applied;
        Report(applied ? "MultiplayerQuestStateSnapshotApplied"
                       : "MultiplayerQuestStateSnapshotApplyBlocked",
            applied ? "validated fallback bridge applied host state after the guest save bundle"
                    : "post-section native bridge rejected host state");
        return applied;
    }

    void QuestStateAuthorityService::OnNativeCapture(
        void* const context,
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        auto* const service = static_cast<QuestStateAuthorityService*>(context);
        if (service != nullptr)
        {
            service->CaptureHostSerializedBytes(bytes, byteCount);
        }
    }

    bool QuestStateAuthorityService::ProvideNativeSnapshot(
        void* const context,
        const std::uint8_t*& bytes,
        std::size_t& byteCount) noexcept
    {
        bytes = nullptr;
        byteCount = 0;
        auto* const service = static_cast<QuestStateAuthorityService*>(context);
        if (service == nullptr || service->role_ != PeerRole::Guest ||
            !service->staged_.present)
        {
            return false;
        }
        bytes = service->staged_.bytes.data();
        byteCount = service->staged_.bytes.size();
        return true;
    }

    void QuestStateAuthorityService::OnNativeApplyResult(
        void* const context,
        const bool applied) noexcept
    {
        auto* const service = static_cast<QuestStateAuthorityService*>(context);
        if (service == nullptr)
        {
            return;
        }
        service->staged_.applied = applied;
        service->Report(
            applied ? "MultiplayerQuestStateSnapshotApplied"
                    : "MultiplayerQuestStateSnapshotApplyBlocked",
            applied
                ? "host QUESTS parser replaced the guest parser before REGIONS and FACTIONS loaded"
                : "native CQuestManager rejected the staged host parser");
        if (!applied)
        {
            service->diagnostics_.Event(
                "ClientFailed",
                "multiplayer-host-quest-state-native-load-override");
        }
    }

    void QuestStateAuthorityService::OnNativeSectionsLoaded(
        void* const context) noexcept
    {
        auto* const service = static_cast<QuestStateAuthorityService*>(context);
        if (service != nullptr &&
            !service->ApplyAfterNativeWorldSections())
        {
            service->diagnostics_.Event(
                "ClientFailed",
                "multiplayer-host-quest-state-post-section-apply");
        }
    }

    bool QuestStateAuthorityService::IsHostSource(
        const TransportMessage& message) const noexcept
    {
        const std::uint64_t hostActorId = expectedHostActorId_ != 0
            ? expectedHostActorId_
            : latchedHostActorId_;
        return role_ == PeerRole::Guest && message.sourceActorId != 0 &&
            hostActorId != 0 && hostActorId == message.sourceActorId;
    }

    bool QuestStateAuthorityService::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (transportMessage.type != protocol::PacketType::QuestStateSnapshot)
        {
            return false;
        }
        protocol::QuestStateSnapshotMessage message;
        if (!protocol::DecodeQuestStateSnapshotMessage(
                transportMessage.payload.data(), transportMessage.payloadSize,
                message))
        {
            ResetInbound();
            Report("MultiplayerQuestStateSnapshotRejected",
                "invalid quest snapshot packet");
            return true;
        }
        bool latchedForBegin = false;
        if (message.operation == protocol::QuestStateSnapshotOperation::Begin &&
            expectedHostActorId_ == 0 && latchedHostActorId_ == 0)
        {
            // PlayerActorState and QuestStateSnapshot use independent reliable
            // streams. Latch only a fully sane Begin with a source and nonce.
            if (transportMessage.sourceActorId == 0 ||
                transportMessage.connectionNonce == 0)
            {
                Report("MultiplayerQuestStateSnapshotRejected",
                    "quest Begin arrived without an established source/nonce");
                return true;
            }
            latchedHostActorId_ = transportMessage.sourceActorId;
            latchedForBegin = true;
        }
        if (!IsHostSource(transportMessage))
        {
            if (latchedForBegin) latchedHostActorId_ = 0;
            Report("MultiplayerQuestStateSnapshotRejected",
                "guest received quest state from a non-host source");
            return true;
        }
        if (message.operation == protocol::QuestStateSnapshotOperation::Begin)
        {
            return BeginInbound(transportMessage, message);
        }
        if (message.operation == protocol::QuestStateSnapshotOperation::Chunk)
        {
            return AppendInbound(transportMessage, message);
        }
        return CommitInbound(transportMessage, message);
    }

    bool QuestStateAuthorityService::BeginInbound(
        const TransportMessage& transportMessage,
        const protocol::QuestStateSnapshotMessage& message)
    {
        if (message.snapshotRevision < acceptedSnapshotRevision_ &&
            message.authorityEpoch == acceptedAuthorityEpoch_ &&
            message.sessionRevision == acceptedSessionRevision_)
        {
            return true;
        }
        if (message.authorityEpoch != acceptedAuthorityEpoch_ ||
            message.sessionRevision != acceptedSessionRevision_)
        {
            acceptedAuthorityEpoch_ = message.authorityEpoch;
            acceptedSessionRevision_ = message.sessionRevision;
            acceptedSnapshotRevision_ = 0;
            staged_ = {};
        }
        if (message.snapshotRevision <= acceptedSnapshotRevision_ ||
            transportMessage.connectionNonce == 0)
        {
            return true;
        }
        try
        {
            inbound_ = {};
            inbound_.authorityEpoch = message.authorityEpoch;
            inbound_.sessionRevision = message.sessionRevision;
            inbound_.snapshotRevision = message.snapshotRevision;
            inbound_.transferId = message.transferId;
            inbound_.connectionNonce = transportMessage.connectionNonce;
            inbound_.hash = message.hash;
            inbound_.bytes.resize(message.totalBytes);
            inbound_.active = true;
            return true;
        }
        catch (...)
        {
            ResetInbound();
            return true;
        }
    }

    bool QuestStateAuthorityService::AppendInbound(
        const TransportMessage& transportMessage,
        const protocol::QuestStateSnapshotMessage& message) noexcept
    {
        if (!inbound_.active || message.authorityEpoch != inbound_.authorityEpoch ||
            message.sessionRevision != inbound_.sessionRevision ||
            message.snapshotRevision != inbound_.snapshotRevision ||
            message.transferId != inbound_.transferId ||
            message.hash != inbound_.hash ||
            transportMessage.connectionNonce != inbound_.connectionNonce ||
            message.offset != inbound_.receivedBytes ||
            message.chunkSize > inbound_.bytes.size() - inbound_.receivedBytes)
        {
            ResetInbound();
            return true;
        }
        std::memcpy(inbound_.bytes.data() + inbound_.receivedBytes,
            message.chunk, message.chunkSize);
        inbound_.receivedBytes += static_cast<std::uint32_t>(message.chunkSize);
        return true;
    }

    bool QuestStateAuthorityService::CommitInbound(
        const TransportMessage& transportMessage,
        const protocol::QuestStateSnapshotMessage& message)
    {
        if (!inbound_.active || message.authorityEpoch != inbound_.authorityEpoch ||
            message.sessionRevision != inbound_.sessionRevision ||
            message.snapshotRevision != inbound_.snapshotRevision ||
            message.transferId != inbound_.transferId ||
            message.hash != inbound_.hash ||
            transportMessage.connectionNonce != inbound_.connectionNonce ||
            message.offset != inbound_.bytes.size() ||
            message.chunkSize != 0 ||
            inbound_.receivedBytes != inbound_.bytes.size() ||
            HashBytes(inbound_.bytes.data(), inbound_.bytes.size()) !=
                inbound_.hash)
        {
            ResetInbound();
            return true;
        }
        try
        {
            staged_.authorityEpoch = inbound_.authorityEpoch;
            staged_.sessionRevision = inbound_.sessionRevision;
            staged_.snapshotRevision = inbound_.snapshotRevision;
            staged_.hash = inbound_.hash;
            staged_.bytes = std::move(inbound_.bytes);
            staged_.present = true;
            staged_.applied = false;
            acceptedSnapshotRevision_ = staged_.snapshotRevision;
            ResetInbound();
            Report("MultiplayerQuestStateSnapshotAccepted",
                "complete host snapshot is staged for the pre-world gate");
            return true;
        }
        catch (...)
        {
            ResetInbound();
            return true;
        }
    }

    void QuestStateAuthorityService::ResetInbound() noexcept
    {
        inbound_ = {};
    }

    void QuestStateAuthorityService::Report(
        const char* event,
        const char* reason) const noexcept
    {
        char detail[320] = {};
        std::snprintf(detail, sizeof(detail),
            "role=%s local_actor_id=%llu reason=%s",
            role_ == PeerRole::Host ? "host" : "guest",
            static_cast<unsigned long long>(localActorId_),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event(event != nullptr
                ? event : "MultiplayerQuestStateSnapshot", detail);
    }
}
