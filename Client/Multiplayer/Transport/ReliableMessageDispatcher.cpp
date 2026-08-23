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
            type == protocol::PacketType::PlayerMovement ||
            type == protocol::PacketType::EntityMovement ||
            type == protocol::PacketType::Acknowledgement ||
            sinks_[index] != nullptr)
        {
            return false;
        }
        sinks_[index] = &sink;
        return true;
    }

    bool ReliableMessageDispatcher::RegisterAll(
        const ReliableMessageSinkBinding* bindings,
        const std::size_t count) noexcept
    {
        if (bindings == nullptr)
        {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            const ReliableMessageSinkBinding& binding = bindings[i];
            if (binding.sink == nullptr)
            {
                sinks_.fill(nullptr);
                return false;
            }
            const ReliableMessageTypeSet types =
                binding.sink->HandledPacketTypes();
            if (types.values == nullptr || types.count == 0)
            {
                sinks_.fill(nullptr);
                if (binding.failureDetail != nullptr)
                {
                    diagnostics_.Event("ClientFailed", binding.failureDetail);
                }
                return false;
            }
            for (std::size_t typeIndex = 0; typeIndex < types.count;
                 ++typeIndex)
            {
                if (!Register(types.values[typeIndex], *binding.sink))
                {
                    sinks_.fill(nullptr);
                    if (binding.failureDetail != nullptr)
                    {
                        diagnostics_.Event(
                            "ClientFailed", binding.failureDetail);
                    }
                    return false;
                }
            }
        }
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
