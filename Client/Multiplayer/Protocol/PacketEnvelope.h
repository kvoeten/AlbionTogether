#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::protocol
{
    inline constexpr std::size_t MaximumDatagramBytes = 1'472;
    inline constexpr std::size_t PacketHeaderBytes = 24;

    enum class PacketType : std::uint8_t
    {
        PlayerState = 1,
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
    };

    namespace packet_flag
    {
        inline constexpr std::uint8_t Reliable = 1u << 0;
        inline constexpr std::uint8_t All = Reliable;
    }

    struct PacketEnvelope final
    {
        PacketType type = PacketType::PlayerState;
        std::uint8_t flags = 0;
        std::uint64_t sourceActorId = 0;
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
