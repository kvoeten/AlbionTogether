#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <array>
#include <cstddef>

namespace fable::multiplayer
{
    class UdpPeer;

    struct ReliableMessageTypeSet final
    {
        const protocol::PacketType* values = nullptr;
        std::size_t count = 0;
    };

    class ReliableMessageSink
    {
    public:
        virtual ~ReliableMessageSink() = default;
        [[nodiscard]] virtual ReliableMessageTypeSet
            HandledPacketTypes() const noexcept = 0;
        virtual bool HandleReliableMessage(
            const TransportMessage& message) = 0;
    };

    struct ReliableMessageSinkBinding final
    {
        ReliableMessageSink* sink = nullptr;
        const char* failureDetail = nullptr;
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
        bool RegisterAll(
            const ReliableMessageSinkBinding* bindings,
            std::size_t count) noexcept;
        bool Pump();
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t SinkCount =
            static_cast<std::size_t>(
                protocol::PacketType::Count);
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::array<ReliableMessageSink*, SinkCount> sinks_ = {};
    };
}
