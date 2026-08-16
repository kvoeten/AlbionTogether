#include "ReliableMessageDispatcher.h"

#include "Multiplayer/Transport/UdpPeer.h"

namespace fable::multiplayer
{
    void ReliableMessageDispatcher::Initialize(
        UdpPeer& transport,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        transport_ = &transport;
        diagnostics_ = diagnostics;
    }

    bool ReliableMessageDispatcher::Register(
        protocol::PacketType type,
        ReliableMessageSink& sink) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(type);
        if (index >= sinks_.size() ||
            type == protocol::PacketType::PlayerState ||
            type == protocol::PacketType::EntityMovement ||
            type == protocol::PacketType::Acknowledgement ||
            sinks_[index] != nullptr)
        {
            return false;
        }
        sinks_[index] = &sink;
        return true;
    }

    bool ReliableMessageDispatcher::Pump()
    {
        if (transport_ == nullptr)
        {
            return false;
        }
        TransportMessage message;
        while (transport_->TryConsumeReliable(message))
        {
            const std::size_t index = static_cast<std::size_t>(message.type);
            ReliableMessageSink* const sink = index < sinks_.size()
                ? sinks_[index]
                : nullptr;
            if (sink == nullptr)
            {
                diagnostics_.Event(
                    "MultiplayerReliableMessageUnhandled",
                    "ordered message had no registered subsystem");
                continue;
            }
            if (!sink->HandleReliableMessage(message))
            {
                diagnostics_.Event(
                    "MultiplayerReliableMessageFailed",
                    "registered subsystem rejected an ordered message");
                return false;
            }
        }
        return true;
    }

    void ReliableMessageDispatcher::Shutdown() noexcept
    {
        sinks_.fill(nullptr);
        transport_ = nullptr;
        diagnostics_ = {};
    }
}
