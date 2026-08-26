#include "ReliableSinkDescriptorRegistry.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma section(".ftrsink$A", read)
#pragma section(".ftrsink$Z", read)

extern "C"
{
    __declspec(allocate(".ftrsink$A"))
    extern const fable::multiplayer::ReliableSinkDescriptor* const
        g_fableReliableSinkDescriptorsBegin = nullptr;
    __declspec(allocate(".ftrsink$Z"))
    extern const fable::multiplayer::ReliableSinkDescriptor* const
        g_fableReliableSinkDescriptorsEnd = nullptr;
}

#if defined(_M_IX86)
#pragma comment(linker, "/include:_g_fableReliableSinkDescriptorsBegin")
#pragma comment(linker, "/include:_g_fableReliableSinkDescriptorsEnd")
#else
#pragma comment(linker, "/include:g_fableReliableSinkDescriptorsBegin")
#pragma comment(linker, "/include:g_fableReliableSinkDescriptorsEnd")
#endif

namespace
{
    constexpr std::size_t TransportOwnedPacketTypeCount = 5;
    constexpr std::size_t ExpectedDescriptorCount =
        static_cast<std::size_t>(
            fable::multiplayer::protocol::PacketType::Count) - 1u -
        TransportOwnedPacketTypeCount;

    bool IsApplicationReliableType(
        const fable::multiplayer::protocol::PacketType type) noexcept
    {
        using fable::multiplayer::protocol::PacketType;
        return type != PacketType::PlayerMovement &&
            type != PacketType::EntityMovement &&
            type != PacketType::Acknowledgement &&
            type != PacketType::PeerHello &&
            type != PacketType::ReliableFragment;
    }

    void ReportCountFailure(
        const fable::core::Diagnostics& diagnostics,
        const std::size_t actual)
    {
        char detail[128] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "multiplayer-reliable-sink-descriptor-count expected=%zu actual=%zu",
            ExpectedDescriptorCount,
            actual);
        diagnostics.Event("ClientFailed", detail);
    }
}

namespace fable::multiplayer
{
    bool ReliableSinkDescriptorRegistry::RegisterDiscovered(
        MultiplayerSessionContexts& contexts,
        ReliableMessageDispatcher& dispatcher,
        const core::Diagnostics& diagnostics)
    {
        std::vector<const ReliableSinkDescriptor*> descriptors;
        const ReliableSinkDescriptor* const* current =
            &g_fableReliableSinkDescriptorsBegin + 1;
        const ReliableSinkDescriptor* const* const end =
            &g_fableReliableSinkDescriptorsEnd;
        for (; current < end; ++current)
        {
            if (*current != nullptr)
            {
                descriptors.push_back(*current);
            }
        }
        if (descriptors.size() != ExpectedDescriptorCount)
        {
            ReportCountFailure(diagnostics, descriptors.size());
            return false;
        }

        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const ReliableSinkDescriptor* left,
               const ReliableSinkDescriptor* right)
            {
                if (left->order != right->order)
                {
                    return left->order < right->order;
                }
                return left->id < right->id;
            });

        constexpr std::size_t PacketSlotCount =
            static_cast<std::size_t>(protocol::PacketType::Count);
        std::array<const ReliableSinkDescriptor*, PacketSlotCount> owners = {};
        for (std::size_t index = 0; index < descriptors.size(); ++index)
        {
            const ReliableSinkDescriptor& descriptor = *descriptors[index];
            if (descriptor.id == 0 || descriptor.name == nullptr ||
                descriptor.name[0] == '\0' || descriptor.resolve == nullptr)
            {
                diagnostics.Event(
                    "ClientFailed",
                    "multiplayer-reliable-sink-descriptor-invalid");
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                const ReliableSinkDescriptor& other = *descriptors[previous];
                if (descriptor.id == other.id ||
                    std::strcmp(descriptor.name, other.name) == 0)
                {
                    diagnostics.Event(
                        "ClientFailed",
                        "multiplayer-reliable-sink-descriptor-duplicate");
                    return false;
                }
            }

            ReliableMessageSink* const sink = descriptor.resolve(contexts);
            if (sink == nullptr)
            {
                diagnostics.Event(
                    "ClientFailed",
                    "multiplayer-reliable-sink-resolution");
                return false;
            }
            const ReliableMessageTypeSet handled = sink->HandledPacketTypes();
            if (handled.values == nullptr || handled.count == 0)
            {
                diagnostics.Event(
                    "ClientFailed",
                    "multiplayer-reliable-sink-packet-set");
                return false;
            }
            for (std::size_t typeIndex = 0; typeIndex < handled.count;
                 ++typeIndex)
            {
                const protocol::PacketType type = handled.values[typeIndex];
                const std::size_t slot = static_cast<std::size_t>(type);
                if (slot >= owners.size() || !IsApplicationReliableType(type) ||
                    owners[slot] != nullptr)
                {
                    diagnostics.Event(
                        "ClientFailed",
                        "multiplayer-reliable-sink-packet-ownership");
                    return false;
                }
                owners[slot] = &descriptor;
            }
        }

        for (const ReliableSinkDescriptor* const descriptor : descriptors)
        {
            ReliableMessageSink* const sink = descriptor->resolve(contexts);
            const ReliableMessageSinkBinding binding = {
                sink, descriptor->failureDetail};
            if (!dispatcher.RegisterAll(&binding, 1))
            {
                return false;
            }
        }
        return true;
    }
}
