#pragma once

#include "Multiplayer/Protocol/PacketEnvelope.h"

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    enum class WorldSection : std::uint8_t
    {
        Regions = 1,
        Factions = 2,
    };

    enum class WorldSectionSnapshotOperation : std::uint8_t
    {
        Begin = 1,
        Chunk = 2,
        Commit = 3,
    };

    struct WorldSectionSnapshotMessage final
    {
        WorldSectionSnapshotOperation operation =
            WorldSectionSnapshotOperation::Begin;
        WorldSection section = WorldSection::Regions;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t sessionRevision = 0;
        std::uint64_t snapshotRevision = 0;
        std::uint64_t transferId = 0;
        std::uint32_t totalBytes = 0;
        std::uint32_t offset = 0;
        std::uint64_t hash = 0;
        const std::uint8_t* chunk = nullptr;
        std::size_t chunkSize = 0;
    };

    inline constexpr std::size_t WorldSectionSnapshotHeaderBytes = 52;
    inline constexpr std::size_t MaximumWorldSectionSnapshotBytes =
        512 * 1024;

    inline constexpr PacketType WorldSectionSnapshotPacketType =
        PacketType::WorldSectionSnapshot;

    [[nodiscard]] std::size_t MaximumWorldSectionSnapshotChunkBytes() noexcept;
    bool EncodeWorldSectionSnapshotMessage(
        const WorldSectionSnapshotMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeWorldSectionSnapshotMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        WorldSectionSnapshotMessage& message) noexcept;
}
