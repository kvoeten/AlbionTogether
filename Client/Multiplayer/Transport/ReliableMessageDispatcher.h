#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <array>
#include <cstddef>

namespace fable::multiplayer
{
    class UdpPeer;

    class ReliableMessageSink
    {
    public:
        virtual ~ReliableMessageSink() = default;
        virtual bool HandleReliableMessage(
            const TransportMessage& message) = 0;
    };

    // Drains the ordered transport queue once and dispatches each message in
    // wire order. This preserves lease-before-action causality across modules.
    class ReliableMessageDispatcher final
    {
    public:
        void Initialize(
            UdpPeer& transport,
            const core::Diagnostics& diagnostics);
        bool Register(
            protocol::PacketType type,
            ReliableMessageSink& sink) noexcept;
        bool Pump();
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t SinkCount =
            static_cast<std::size_t>(
                protocol::PacketType::PlayerAction) + 1u;
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::array<ReliableMessageSink*, SinkCount> sinks_ = {};
    };
}
