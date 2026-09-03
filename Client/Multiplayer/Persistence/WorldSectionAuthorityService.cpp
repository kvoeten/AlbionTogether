#include "WorldSectionAuthorityService.h"

#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>

namespace
{
    bool IsSection(const fable::multiplayer::protocol::WorldSection section)
        noexcept
    {
        using fable::multiplayer::protocol::WorldSection;
        return section == WorldSection::Regions ||
            section == WorldSection::Factions;
    }

    fable::multiplayer::ReliableMessageSink* ResolveWorldSectionSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.world.worldSections;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkWorldSections,
    0x1011u,
    "world-section-snapshot",
    820u,
    "multiplayer-world-section-snapshot-dispatch",
    ResolveWorldSectionSink);

namespace fable::multiplayer::persistence
{
    std::size_t WorldSectionAuthorityService::Index(
        const Section section) noexcept
    {
        return section == Section::Factions ? 1u : 0u;
    }

    bool WorldSectionAuthorityService::Initialize(
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
        hostAuthorityEpoch_ = authorityEpoch == 0 ? 1 : authorityEpoch;
        hostSessionRevision_ = transport.ConnectionNonce();
        if (hostSessionRevision_ == 0) hostSessionRevision_ = 1;
        const HMODULE gameModule = GetModuleHandleW(nullptr);
        const bool hookReady = role_ == PeerRole::Host
            ? nativeHook_.InstallHostCapture(
                gameModule,
                &WorldSectionAuthorityService::NativeCaptureSink,
                this,
                diagnostics_)
            : nativeHook_.InstallGuestOverride(
                gameModule,
                &WorldSectionAuthorityService::NativeSnapshotProvider,
                &WorldSectionAuthorityService::NativeApplyResultSink,
                this,
                diagnostics_);
        if (!hookReady)
        {
            diagnostics_.Event(
                "MultiplayerWorldSectionNativeBoundaryBlocked",
                role_ == PeerRole::Host
                    ? "host REGIONS/FACTIONS load-save capture hooks were unavailable"
                    : "guest REGIONS/FACTIONS load override hooks were unavailable");
            return false;
        }
        return true;
    }

    void WorldSectionAuthorityService::Shutdown() noexcept
    {
        nativeHook_.Shutdown();
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        expectedHostActorId_ = 0;
        latchedHostActorId_ = 0;
        hostAuthorityEpoch_ = 0;
        hostSessionRevision_ = 0;
        nextSnapshotRevision_ = {};
        transfer_.Reset();
    }

    bool WorldSectionAuthorityService::PublishHostPayload(
        const Section section,
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t snapshotRevision,
        const std::uint8_t* const bytes,
        const std::size_t byteCount)
    {
        if (!IsSection(section) || role_ != PeerRole::Host ||
            transport_ == nullptr)
        {
            return false;
        }
        return transfer_.PublishHostPayload(section, authorityEpoch,
            sessionRevision, snapshotRevision, bytes, byteCount,
            transport_->PeerSetRevision());
    }

    void WorldSectionAuthorityService::CaptureHostPayload(
        const Section section,
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        if (!IsSection(section) || role_ != PeerRole::Host)
        {
            return;
        }
        auto& revision = nextSnapshotRevision_[Index(section)];
        ++revision;
        if (revision == 0) ++revision;
        if (PublishHostPayload(section, hostAuthorityEpoch_,
                hostSessionRevision_, revision, bytes, byteCount))
        {
            Report("MultiplayerWorldSectionSnapshotCaptured", section,
                "native load payload retained for reliable peer replay");
        }
        else
        {
            Report("MultiplayerWorldSectionCaptureBlocked", section,
                "native payload could not be bounded and queued");
        }
    }

    bool WorldSectionAuthorityService::Process()
    {
        if (transport_ == nullptr || transport_->HasFailed())
        {
            return transport_ != nullptr && !transport_->HasFailed();
        }
        return transfer_.Process(*transport_, role_ == PeerRole::Host);
    }

    bool WorldSectionAuthorityService::IsHostSource(
        const TransportMessage& message) const noexcept
    {
        const auto actor = expectedHostActorId_ != 0
            ? expectedHostActorId_ : latchedHostActorId_;
        return role_ == PeerRole::Guest && actor != 0 &&
            message.sourceActorId == actor;
    }

    void WorldSectionAuthorityService::SetExpectedHostActor(
        const std::uint64_t actorId) noexcept
    {
        if (actorId == 0) return;
        if (latchedHostActorId_ != 0 && latchedHostActorId_ != actorId)
        {
            expectedHostActorId_ = actorId;
            latchedHostActorId_ = actorId;
            ResetGuestAuthority();
            return;
        }
        expectedHostActorId_ = actorId;
        if (latchedHostActorId_ == 0) latchedHostActorId_ = actorId;
    }

    void WorldSectionAuthorityService::ResetGuestAuthority() noexcept
    {
        transfer_.ResetGuestAuthority();
    }

    bool WorldSectionAuthorityService::HandleReliableMessage(
        const TransportMessage& transport)
    {
        if (transport.type != protocol::WorldSectionSnapshotPacketType)
            return false;
        protocol::WorldSectionSnapshotMessage message;
        if (!protocol::DecodeWorldSectionSnapshotMessage(
                transport.payload.data(), transport.payloadSize, message))
        {
            transfer_.ResetInbound(protocol::WorldSection::Regions);
            transfer_.ResetInbound(protocol::WorldSection::Factions);
            return true;
        }
        bool provisional = false;
        if (message.operation ==
                protocol::WorldSectionSnapshotOperation::Begin &&
            expectedHostActorId_ == 0 && latchedHostActorId_ == 0 &&
            transport.sourceActorId != 0 && transport.connectionNonce != 0)
        {
            latchedHostActorId_ = transport.sourceActorId;
            provisional = true;
        }
        if (!IsHostSource(transport))
        {
            if (provisional) latchedHostActorId_ = 0;
            return true;
        }
        // The message revision identifies the sender's authoritative session;
        // transport.connectionNonce independently fences this receiver's
        // current peer connection. They are deliberately not equal.
        if (transport.connectionNonce == 0)
        {
            if (provisional) latchedHostActorId_ = 0;
            transfer_.ResetInbound(message.section);
            return true;
        }
        if (transfer_.HandleInbound(transport, message) ==
            WorldSectionSnapshotTransfer::ReceiveResult::Accepted)
        {
            Report("MultiplayerWorldSectionSnapshotAccepted",
                message.section, "complete immutable payload staged");
        }
        return true;
    }

    bool WorldSectionAuthorityService::IsGuestReady() const noexcept
    {
        return role_ == PeerRole::Guest && expectedHostActorId_ != 0 &&
            expectedHostActorId_ == latchedHostActorId_ &&
            transfer_.IsGuestReady();
    }

    bool WorldSectionAuthorityService::AcquireGuestPayload(
        const Section section,
        ImmutablePayload& payload) const noexcept
    {
        payload.reset();
        if (!IsSection(section) || !IsGuestReady()) return false;
        return transfer_.AcquireGuestPayload(section, payload);
    }

    void WorldSectionAuthorityService::MarkGuestApplied(
        const Section section,
        const bool applied) noexcept
    {
        transfer_.MarkGuestApplied(section, applied);
        Report(applied ? "MultiplayerWorldSectionSnapshotApplied"
                       : "MultiplayerWorldSectionSnapshotApplyBlocked",
            section, applied ? "native manager consumed authoritative payload"
                             : "native load boundary rejected authoritative payload");
        if (!applied)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-host-world-section-native-load-override");
            PostQuitMessage(EXIT_FAILURE);
        }
    }

    bool WorldSectionAuthorityService::HasCurrentSnapshot(
        const Section section) const noexcept
    {
        return transfer_.HasCurrentSnapshot(section,
            role_ == PeerRole::Host);
    }

    std::uint64_t WorldSectionAuthorityService::CurrentSnapshotRevision(
        const Section section) const noexcept
    {
        return transfer_.CurrentSnapshotRevision(section,
            role_ == PeerRole::Host);
    }

    std::uint64_t WorldSectionAuthorityService::CurrentSnapshotFingerprint(
        const Section section) const noexcept
    {
        return transfer_.CurrentSnapshotFingerprint(section,
            role_ == PeerRole::Host);
    }

    std::size_t WorldSectionAuthorityService::CurrentSnapshotBytes(
        const Section section) const noexcept
    {
        return transfer_.CurrentSnapshotBytes(section,
            role_ == PeerRole::Host);
    }

    void WorldSectionAuthorityService::NativeCaptureSink(
        void* const context,
        const Section section,
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        auto* service = static_cast<WorldSectionAuthorityService*>(context);
        if (service) service->CaptureHostPayload(section, bytes, byteCount);
    }

    bool WorldSectionAuthorityService::NativeSnapshotProvider(
        void* const context,
        const Section section,
        ImmutablePayload& payload) noexcept
    {
        auto* service = static_cast<WorldSectionAuthorityService*>(context);
        return service != nullptr &&
            service->AcquireGuestPayload(section, payload);
    }

    void WorldSectionAuthorityService::NativeApplyResultSink(
        void* const context,
        const Section section,
        const bool applied) noexcept
    {
        auto* service = static_cast<WorldSectionAuthorityService*>(context);
        if (service) service->MarkGuestApplied(section, applied);
    }

    void WorldSectionAuthorityService::Report(
        const char* const event,
        const Section section,
        const char* const reason) const noexcept
    {
        char detail[320] = {};
        std::snprintf(detail, sizeof(detail),
            "role=%s local_actor_id=%llu section=%s reason=%s",
            role_ == PeerRole::Host ? "host" : "guest",
            static_cast<unsigned long long>(localActorId_),
            section == Section::Regions ? "regions" : "factions",
            reason ? reason : "unknown");
        diagnostics_.Event(event ? event :
            "MultiplayerWorldSectionSnapshot", detail);
    }
}
