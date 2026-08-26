#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    // Keep the complete IPv4 datagram below the common 1280-byte practical
    // floor.  Reliable messages larger than this are fragmented by the
    // ordered stream transport; movement remains one lossy snapshot.
    inline constexpr std::size_t MaximumDatagramBytes = 1'200;
    inline constexpr std::size_t PacketHeaderBytes = 52;
    // Reliable logical messages are bounded independently of the datagram
    // size. The current worst-case hero baseline is 1,160 bytes and is split
    // into two transport records when it exceeds the 1,148-byte payload.
    inline constexpr std::size_t MaximumReliableMessageBytes = 2'048;

    enum class PacketType : std::uint8_t
    {
        PlayerMovement = 1,
        Authority = 2,
        EntityLifecycle = 3,
        EntityAction = 4,
        EntityMovement = 5,
        PopulationState = 6,
        Acknowledgement = 7,
        SavedEntityMapBaseline = 8,
        // Transport-level discovery. This deliberately carries no world or
        // Hero state so peers can meet while native map construction is held.
        PeerHello = 9,
        EntityVitals = 10,
        EntityLowSimulation = 11,
        PlayerAction = 12,
        PlayerActorState = 13,
        CombatHit = 14,
        ReliableFragment = 15,
        Count,
    };

    namespace packet_flag
    {
        inline constexpr std::uint8_t Reliable = 1u << 0;
        inline constexpr std::uint8_t All = Reliable;
    }

    struct PacketEnvelope final
    {
        PacketType type = PacketType::PlayerMovement;
        std::uint8_t flags = 0;
        std::uint64_t sourceActorId = 0;
        // Transport connection fencing token. It changes for every local
        // transport session and prevents delayed datagrams from an older
        // endpoint incarnation from being accepted after reconnect.
        std::uint64_t connectionNonce = 1;
        std::uint64_t streamId = 0;
        std::uint8_t streamKind = 0;
        std::uint64_t streamIncarnation = 0;
        std::uint32_t sequence = 0;
    };

    struct PacketView final
    {
        PacketEnvelope envelope = {};
        const std::uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
    };

    [[nodiscard]] std::size_t MaximumPayloadBytes() noexcept;
    bool EncodePacket(
        const PacketEnvelope& envelope,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodePacket(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PacketView& packet) noexcept;
}
