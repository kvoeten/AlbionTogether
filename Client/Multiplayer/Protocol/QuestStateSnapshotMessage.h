#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    enum class QuestStateSnapshotOperation : std::uint8_t
    {
        Begin = 1,
        Chunk = 2,
        Commit = 3,
    };

    // The payload is the native CQuestManager state captured by the host.
    // It is intentionally opaque here: no guest-authored Hero fields may be
    // included and no parser ABI is assumed by the transport layer.
    struct QuestStateSnapshotMessage final
    {
        QuestStateSnapshotOperation operation =
            QuestStateSnapshotOperation::Begin;
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

    inline constexpr std::size_t QuestStateSnapshotHeaderBytes = 52;
    // Real AutoSave.qs files are 307,200 bytes; leave headroom for larger
    // manager state while retaining a hard allocation/transfer bound.
    inline constexpr std::size_t MaximumQuestStateSnapshotBytes = 1 * 1024 * 1024;

    [[nodiscard]] std::size_t MaximumQuestStateSnapshotChunkBytes() noexcept;
    bool EncodeQuestStateSnapshotMessage(
        const QuestStateSnapshotMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeQuestStateSnapshotMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        QuestStateSnapshotMessage& message) noexcept;
}
