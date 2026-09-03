#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Persistence/Hooks/WorldSectionPersistenceHook.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Protocol/WorldSectionSnapshotMessage.h"
#include "Multiplayer/Persistence/WorldSectionSnapshotTransfer.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::persistence
{
    // Fixed two-slot authority for opaque native REGIONS/FACTIONS payloads.
    // It retains only the newest immutable committed payload per section.
    class WorldSectionAuthorityService final : public ReliableMessageSink
    {
    public:
        using Section = protocol::WorldSection;
        using ImmutablePayload = WorldSectionSnapshotTransfer::ImmutablePayload;

        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::WorldSectionSnapshotPacketType};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        bool Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics,
            std::uint32_t authorityEpoch = 0) noexcept;
        void Shutdown() noexcept;
        void SetExpectedHostActor(std::uint64_t actorId) noexcept;

        bool PublishHostPayload(
            Section section,
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t snapshotRevision,
            const std::uint8_t* bytes,
            std::size_t byteCount);
        void CaptureHostPayload(
            Section section,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        bool Process();
        bool HandleReliableMessage(
            const TransportMessage& message) override;

        [[nodiscard]] bool IsGuestReady() const noexcept;
        [[nodiscard]] bool AcquireGuestPayload(
            Section section,
            ImmutablePayload& payload) const noexcept;
        void MarkGuestApplied(Section section, bool applied) noexcept;
        [[nodiscard]] bool HasCurrentSnapshot(Section section) const noexcept;
        [[nodiscard]] std::uint64_t CurrentSnapshotRevision(
            Section section) const noexcept;
        [[nodiscard]] std::uint64_t CurrentSnapshotFingerprint(
            Section section) const noexcept;
        [[nodiscard]] std::size_t CurrentSnapshotBytes(
            Section section) const noexcept;

        static void NativeCaptureSink(
            void* context,
            Section section,
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        static bool NativeSnapshotProvider(
            void* context,
            Section section,
            ImmutablePayload& payload) noexcept;
        static void NativeApplyResultSink(
            void* context,
            Section section,
            bool applied) noexcept;

    private:
        static constexpr std::size_t SectionCount = 2;

        [[nodiscard]] static std::size_t Index(Section section) noexcept;
        [[nodiscard]] bool IsHostSource(
            const TransportMessage& message) const noexcept;
        void ResetGuestAuthority() noexcept;
        void Report(const char* event, Section section,
            const char* reason) const noexcept;

        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t expectedHostActorId_ = 0;
        std::uint64_t latchedHostActorId_ = 0;
        std::uint32_t hostAuthorityEpoch_ = 0;
        std::uint64_t hostSessionRevision_ = 0;
        std::array<std::uint64_t, SectionCount> nextSnapshotRevision_ = {};
        WorldSectionSnapshotTransfer transfer_;
        game::persistence::WorldSectionPersistenceHook nativeHook_;
    };
}
