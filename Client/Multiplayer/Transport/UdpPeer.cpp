#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "UdpPeer.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerStateCodec.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr ULONGLONG kMinimumSendIntervalMilliseconds = 50;
    constexpr ULONGLONG kKeepAliveIntervalMilliseconds = 1'000;
    constexpr ULONGLONG kPeerHelloIntervalMilliseconds = 250;
    constexpr ULONGLONG kPeerLeaseMilliseconds = 10'000;
    constexpr ULONGLONG kRetiredActorRetentionMilliseconds = 2'000;
    constexpr ULONGLONG kReliableResendMilliseconds = 100;
    constexpr std::size_t kReliableQueueLimit = 512;
    constexpr std::size_t kUnreliableQueueLimit = 4096;

    bool IsNewerSequence(std::uint32_t candidate, std::uint32_t previous)
    {
        return previous == 0 ||
            static_cast<std::int32_t>(candidate - previous) > 0;
    }

    std::uint32_t NextReliableSequence(std::uint32_t previous) noexcept
    {
        return previous == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : previous + 1u;
    }

    bool EncodePlayerPacket(
        const fable::multiplayer::PlayerState& state,
        std::uint64_t sourceActorId,
        std::array<
            std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        using namespace fable::multiplayer;
        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        if (!protocol::EncodePlayerState(
                state,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize))
        {
            return false;
        }
        protocol::PacketEnvelope envelope;
        envelope.type = protocol::PacketType::PlayerState;
        envelope.sourceActorId = sourceActorId;
        return protocol::EncodePacket(
            envelope,
            payload.data(),
            payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool IsReliablePacketType(
        fable::multiplayer::protocol::PacketType type) noexcept
    {
        using fable::multiplayer::protocol::PacketType;
        return type == PacketType::Authority ||
            type == PacketType::EntityLifecycle ||
            type == PacketType::EntityAction ||
            type == PacketType::EntityVitals ||
            type == PacketType::EntityLowSimulation ||
            type == PacketType::PopulationState ||
            type == PacketType::SavedEntityMapBaseline;
    }

    bool IsUnreliablePacketType(
        fable::multiplayer::protocol::PacketType type) noexcept
    {
        return type ==
            fable::multiplayer::protocol::PacketType::EntityMovement;
    }

    bool EncodeUnreliablePacket(
        const fable::multiplayer::TransportMessage& message,
        std::array<
            std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        fable::multiplayer::protocol::PacketEnvelope envelope;
        envelope.type = message.type;
        envelope.sourceActorId = message.sourceActorId;
        return fable::multiplayer::protocol::EncodePacket(
            envelope,
            message.payload.data(),
            message.payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodeReliablePacket(
        const fable::multiplayer::TransportMessage& message,
        std::uint64_t sourceActorId,
        std::array<
            std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        fable::multiplayer::protocol::PacketEnvelope envelope;
        envelope.type = message.type;
        envelope.flags = fable::multiplayer::protocol::packet_flag::Reliable;
        envelope.sourceActorId = sourceActorId;
        envelope.sequence = message.sequence;
        return fable::multiplayer::protocol::EncodePacket(
            envelope,
            message.payload.data(),
            message.payloadSize,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodeAcknowledgement(
        std::uint64_t sourceActorId,
        std::uint32_t acknowledgedSequence,
        std::array<
            std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        fable::multiplayer::protocol::PacketEnvelope envelope;
        envelope.type =
            fable::multiplayer::protocol::PacketType::Acknowledgement;
        envelope.sourceActorId = sourceActorId;
        envelope.sequence = acknowledgedSequence;
        return fable::multiplayer::protocol::EncodePacket(
            envelope,
            nullptr,
            0,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }

    bool EncodePeerHello(
        std::uint64_t sourceActorId,
        std::array<
            std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        std::size_t& datagramSize) noexcept
    {
        fable::multiplayer::protocol::PacketEnvelope envelope;
        envelope.type =
            fable::multiplayer::protocol::PacketType::PeerHello;
        envelope.sourceActorId = sourceActorId;
        return fable::multiplayer::protocol::EncodePacket(
            envelope,
            nullptr,
            0,
            datagram.data(),
            datagram.size(),
            datagramSize);
    }
}

namespace fable::multiplayer
{
    struct UdpPeer::Implementation final
    {
        struct EndpointKey final
        {
            std::uint32_t address = 0;
            std::uint16_t port = 0;

            bool operator==(const EndpointKey& other) const noexcept
            {
                return address == other.address && port == other.port;
            }
        };

        struct EndpointHash final
        {
            std::size_t operator()(const EndpointKey& endpoint) const noexcept
            {
                return static_cast<std::size_t>(endpoint.address) ^
                    (static_cast<std::size_t>(endpoint.port) << 1);
            }
        };

        struct Peer final
        {
            sockaddr_in endpoint = {};
            std::uint64_t actorId = 0;
            ULONGLONG lastReceivedAt = 0;
            std::unordered_map<std::uint64_t, std::uint32_t> lastSentSequence;
            std::unordered_map<std::uint64_t, ULONGLONG> lastSentAt;
            std::uint32_t lastReceivedReliableSequence = 0;
            std::uint32_t lastAcknowledgedReliableSequence = 0;
            std::uint32_t lastReliableSequenceSent = 0;
            ULONGLONG lastReliableSentAt = 0;
        };

        struct ActorRecord final
        {
            PlayerState state;
            bool retired = false;
            ULONGLONG retiredAt = 0;
        };

        SOCKET socket = INVALID_SOCKET;
        sockaddr_in peer = {};
        core::Diagnostics diagnostics = {};
        PeerRole role = PeerRole::Guest;
        std::thread worker;
        std::mutex stateMutex;
        std::condition_variable wake;
        PlayerState outbound = {};
        std::deque<PlayerState> inbound;
        std::deque<TransportMessage> reliableOutbound;
        std::deque<TransportMessage> reliableInbound;
        std::deque<TransportMessage> unreliableOutbound;
        std::deque<TransportMessage> unreliableInbound;
        std::unordered_map<std::uint64_t, std::uint32_t> lastRemoteSequence;
        std::unordered_map<EndpointKey, Peer, EndpointHash> peers;
        std::unordered_map<std::uint64_t, ActorRecord> actors;
        std::uint64_t localActorId = 0;
        ULONGLONG lastSentAt = 0;
        ULONGLONG lastPeerHelloSentAt = 0;
        ULONGLONG lastReliableSentAt = 0;
        std::uint32_t nextReliableSequence = 1;
        std::uint32_t lastReceivedReliableSequence = 0;
        std::uint32_t lastAcknowledgedReliableSequence = 0;
        std::atomic_bool started{false};
        std::atomic_bool stopping{false};
        std::atomic_bool failed{false};
        std::atomic_bool peerKnown{false};
        std::atomic_uint64_t peerSetRevision{0};
        bool hasOutbound = false;
        bool winsockStarted = false;
        bool peerEventReported = false;
        bool sendEventReported = false;
        bool receiveEventReported = false;
        bool reliableSendEventReported = false;
        bool reliableReceiveEventReported = false;
        bool peerHelloSendEventReported = false;
        bool peerHelloReceiveEventReported = false;

        void Fail(const char* message)
        {
            if (!failed.exchange(true, std::memory_order_acq_rel))
            {
                diagnostics.Log(message);
                diagnostics.Event("MultiplayerTransportFailed", message);
            }
        }

        static EndpointKey Key(const sockaddr_in& endpoint) noexcept
        {
            return {endpoint.sin_addr.s_addr, endpoint.sin_port};
        }

        static void MergeActor(ActorRecord& record, const PlayerState& update)
        {
            PlayerState& current = record.state;
            if (current.actorId != update.actorId ||
                current.authorityEpoch != update.authorityEpoch)
            {
                current = {};
            }
            if ((update.changedProperties & player_property::Identity) != 0)
            {
                current.actorId = update.actorId;
                current.authorityEpoch = update.authorityEpoch;
                current.role = update.role;
                current.playerId = update.playerId;
                current.appearanceDefinition = update.appearanceDefinition;
            }
            if ((update.changedProperties & player_property::Map) != 0)
            {
                current.mapName = update.mapName;
                current.mapId = update.mapId;
            }
            if ((update.changedProperties & player_property::Appearance) != 0)
            {
                current.heroMorph = update.heroMorph;
                current.heroClothing = update.heroClothing;
                current.heroBoneScales = update.heroBoneScales;
                current.heroAppearanceModifiers =
                    update.heroAppearanceModifiers;
            }
            if ((update.changedProperties & player_property::Movement) != 0)
            {
                current.position = update.position;
                current.velocity = update.velocity;
                current.facing = update.facing;
                current.angularVelocity = update.angularVelocity;
                current.moving = update.moving;
            }
            current.sequence = update.sequence;
            current.changedProperties = update.changedProperties;
            record.retired =
                (update.changedProperties & player_property::Retired) != 0;
        }

        bool IsHostEndpoint(const sockaddr_in& sender) const noexcept
        {
            return sender.sin_addr.s_addr == peer.sin_addr.s_addr &&
                sender.sin_port == peer.sin_port;
        }

        bool RegisterGuestEndpoint(
            const sockaddr_in& sender,
            std::uint64_t actorId)
        {
            if (role != PeerRole::Host || actorId == 0 ||
                actorId == localActorId)
            {
                return false;
            }

            const EndpointKey endpoint = Key(sender);
            std::lock_guard<std::mutex> lock(stateMutex);
            bool peerSetChanged = false;

            auto endpointPeer = peers.find(endpoint);
            if (endpointPeer != peers.end() &&
                endpointPeer->second.actorId != 0 &&
                endpointPeer->second.actorId != actorId)
            {
                const std::uint64_t retiredActorId =
                    endpointPeer->second.actorId;
                auto actor = actors.find(retiredActorId);
                if (actor != actors.end() && !actor->second.retired)
                {
                    actor->second.retired = true;
                    actor->second.retiredAt = GetTickCount64();
                    ++actor->second.state.sequence;
                    actor->second.state.changedProperties =
                        player_property::Retired;
                    inbound.push_back(actor->second.state);
                }
                peers.erase(endpointPeer);
                peerSetChanged = true;
            }

            // A restarted client may keep its actor identity while obtaining
            // a new source port. Reliable sequencing is endpoint-scoped, so
            // retain only the current endpoint for that actor.
            for (auto iterator = peers.begin(); iterator != peers.end();)
            {
                if (!(iterator->first == endpoint) &&
                    iterator->second.actorId == actorId)
                {
                    iterator = peers.erase(iterator);
                    peerSetChanged = true;
                    continue;
                }
                ++iterator;
            }

            const auto [connectedIterator, inserted] =
                peers.try_emplace(endpoint);
            peerSetChanged = peerSetChanged || inserted;
            Peer& connected = connectedIterator->second;
            connected.endpoint = sender;
            connected.actorId = actorId;
            connected.lastReceivedAt = GetTickCount64();
            if (peerSetChanged)
            {
                peerSetRevision.fetch_add(1, std::memory_order_acq_rel);
            }
            peerKnown.store(!peers.empty(), std::memory_order_release);
            return true;
        }

        bool SendPeerHelloIfNeeded(ULONGLONG now)
        {
            if (role != PeerRole::Guest ||
                now - lastPeerHelloSentAt < kPeerHelloIntervalMilliseconds)
            {
                return true;
            }

            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodePeerHello(localActorId, packet, packetSize))
            {
                Fail("Multiplayer: peer discovery datagram could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&peer),
                sizeof(peer));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: peer discovery send failed.");
                return false;
            }
            if (sent != static_cast<int>(packetSize))
            {
                Fail("Multiplayer: peer discovery datagram was truncated.");
                return false;
            }
            lastPeerHelloSentAt = now;
            if (!peerHelloSendEventReported)
            {
                peerHelloSendEventReported = true;
                diagnostics.Event(
                    "MultiplayerPeerDiscoverySent",
                    "guest endpoint announced before native world readiness");
            }
            return true;
        }

        bool SendAcknowledgement(
            const sockaddr_in& endpoint,
            std::uint32_t sequence)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodeAcknowledgement(
                    localActorId,
                    sequence,
                    packet,
                    packetSize))
            {
                Fail("Multiplayer: reliable acknowledgement could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: reliable acknowledgement send failed.");
                return false;
            }
            return sent == static_cast<int>(packetSize);
        }

        bool HandleAcknowledgement(
            const protocol::PacketView& packet,
            const sockaddr_in& sender)
        {
            if (packet.envelope.flags != 0 || packet.payloadSize != 0 ||
                packet.envelope.sequence == 0)
            {
                return true;
            }
            std::lock_guard<std::mutex> lock(stateMutex);
            if (reliableOutbound.empty() ||
                reliableOutbound.front().sequence !=
                    packet.envelope.sequence)
            {
                return true;
            }
            if (role == PeerRole::Host)
            {
                const auto connected = peers.find(Key(sender));
                if (connected == peers.end() ||
                    connected->second.actorId !=
                        packet.envelope.sourceActorId)
                {
                    return true;
                }
                connected->second.lastReceivedAt = GetTickCount64();
                connected->second.lastAcknowledgedReliableSequence =
                    packet.envelope.sequence;
            }
            else if (IsHostEndpoint(sender))
            {
                lastAcknowledgedReliableSequence = packet.envelope.sequence;
            }
            return true;
        }

        bool HandleReliable(
            const protocol::PacketView& packet,
            const sockaddr_in& sender)
        {
            if (!IsReliablePacketType(packet.envelope.type) ||
                packet.envelope.flags != protocol::packet_flag::Reliable ||
                packet.envelope.sequence == 0 || packet.payloadSize == 0)
            {
                return true;
            }

            bool acknowledge = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                std::uint32_t* previous = nullptr;
                if (role == PeerRole::Host)
                {
                    const auto connected = peers.find(Key(sender));
                    if (connected == peers.end() ||
                        connected->second.actorId !=
                            packet.envelope.sourceActorId)
                    {
                        return true;
                    }
                    connected->second.lastReceivedAt = GetTickCount64();
                    previous =
                        &connected->second.lastReceivedReliableSequence;
                }
                else
                {
                    if (!IsHostEndpoint(sender))
                    {
                        return true;
                    }
                    previous = &lastReceivedReliableSequence;
                }

                if (packet.envelope.sequence == *previous)
                {
                    acknowledge = true;
                }
                else if (
                    (*previous == 0 ||
                        packet.envelope.sequence ==
                            NextReliableSequence(*previous)) &&
                    reliableInbound.size() < kReliableQueueLimit)
                {
                    TransportMessage message;
                    message.type = packet.envelope.type;
                    message.sourceActorId =
                        packet.envelope.sourceActorId;
                    message.sequence = packet.envelope.sequence;
                    message.payloadSize = packet.payloadSize;
                    std::memcpy(
                        message.payload.data(),
                        packet.payload,
                        packet.payloadSize);
                    reliableInbound.push_back(std::move(message));
                    *previous = packet.envelope.sequence;
                    acknowledge = true;
                }
            }
            return !acknowledge ||
                (SendAcknowledgement(sender, packet.envelope.sequence) &&
                    ReportReliableReceive());
        }

        bool ReportReliableReceive()
        {
            if (!reliableReceiveEventReported)
            {
                reliableReceiveEventReported = true;
                diagnostics.Event(
                    "MultiplayerReliableMessageReceived",
                    "first ordered authority or entity-action message accepted");
            }
            return true;
        }

        bool SendReliableDatagram(
            const TransportMessage& message,
            const sockaddr_in& endpoint)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodeReliablePacket(
                    message,
                    localActorId,
                    packet,
                    packetSize))
            {
                Fail("Multiplayer: reliable authority/action message could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: reliable authority/action send failed.");
                return false;
            }
            if (sent != static_cast<int>(packetSize))
            {
                Fail("Multiplayer: reliable authority/action datagram was truncated.");
                return false;
            }
            if (!reliableSendEventReported)
            {
                reliableSendEventReported = true;
                diagnostics.Event(
                    "MultiplayerReliableMessageSent",
                    "first ordered authority or entity-action message sent");
            }
            return true;
        }

        bool SendUnreliableDatagram(
            const TransportMessage& message,
            const sockaddr_in& endpoint)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodeUnreliablePacket(message, packet, packetSize))
            {
                Fail("Multiplayer: entity movement message could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: entity movement send failed.");
                return false;
            }
            return sent == static_cast<int>(packetSize);
        }

        bool SendUnreliableIfNeeded()
        {
            struct Datagram final
            {
                TransportMessage message;
                sockaddr_in endpoint = {};
            };
            std::vector<Datagram> datagrams;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                while (!unreliableOutbound.empty())
                {
                    TransportMessage message = std::move(
                        unreliableOutbound.front());
                    unreliableOutbound.pop_front();
                    if (role == PeerRole::Host)
                    {
                        for (const auto& entry : peers)
                        {
                            const Peer& connected = entry.second;
                            if (connected.actorId == message.sourceActorId)
                            {
                                continue;
                            }
                            datagrams.push_back(
                                {message, connected.endpoint});
                        }
                    }
                    else
                    {
                        datagrams.push_back({message, peer});
                    }
                }
            }
            for (const Datagram& datagram : datagrams)
            {
                if (!SendUnreliableDatagram(
                        datagram.message,
                        datagram.endpoint))
                {
                    return false;
                }
            }
            return true;
        }

        bool SendReliableIfNeeded(ULONGLONG now)
        {
            struct Datagram final
            {
                TransportMessage message;
                sockaddr_in endpoint = {};
            };
            std::vector<Datagram> datagrams;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                while (!reliableOutbound.empty())
                {
                    const TransportMessage& message = reliableOutbound.front();
                    if (role == PeerRole::Host)
                    {
                        if (peers.empty())
                        {
                            reliableOutbound.pop_front();
                            continue;
                        }
                        bool allAcknowledged = true;
                        for (auto& [endpointKey, connected] : peers)
                        {
                            (void)endpointKey;
                            if (connected.lastAcknowledgedReliableSequence ==
                                message.sequence)
                            {
                                continue;
                            }
                            allAcknowledged = false;
                            const bool due =
                                connected.lastReliableSequenceSent !=
                                    message.sequence ||
                                now - connected.lastReliableSentAt >=
                                    kReliableResendMilliseconds;
                            if (!due)
                            {
                                continue;
                            }
                            datagrams.push_back(
                                {message, connected.endpoint});
                            connected.lastReliableSequenceSent =
                                message.sequence;
                            connected.lastReliableSentAt = now;
                        }
                        if (allAcknowledged)
                        {
                            reliableOutbound.pop_front();
                            continue;
                        }
                        break;
                    }

                    if (lastAcknowledgedReliableSequence == message.sequence)
                    {
                        reliableOutbound.pop_front();
                        lastReliableSentAt = 0;
                        continue;
                    }
                    if (lastReliableSentAt == 0 ||
                        now - lastReliableSentAt >=
                            kReliableResendMilliseconds)
                    {
                        datagrams.push_back({message, peer});
                        lastReliableSentAt = now;
                    }
                    break;
                }
            }
            for (const Datagram& datagram : datagrams)
            {
                if (!SendReliableDatagram(
                        datagram.message,
                        datagram.endpoint))
                {
                    return false;
                }
            }
            return true;
        }

        bool ReceiveAll()
        {
            for (;;)
            {
                std::array<std::uint8_t, protocol::MaximumDatagramBytes>
                    datagram = {};
                sockaddr_in sender = {};
                int senderSize = sizeof(sender);
                const int received = recvfrom(
                    socket,
                    reinterpret_cast<char*>(datagram.data()),
                    static_cast<int>(datagram.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&sender),
                    &senderSize);
                if (received == SOCKET_ERROR)
                {
                    const int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                    {
                        return true;
                    }
                    // Windows reports an ICMP port-unreachable for UDP as a
                    // one-shot connection reset. That is peer liveness input,
                    // not a failure of this socket or its worker.
                    if (error == WSAECONNRESET)
                    {
                        continue;
                    }
                    Fail("Multiplayer: UDP receive failed in the network worker.");
                    return false;
                }
                protocol::PacketView packet;
                if (received <= 0 ||
                    !protocol::DecodePacket(
                        datagram.data(),
                        static_cast<std::size_t>(received),
                        packet))
                {
                    continue;
                }
                if (role == PeerRole::Guest && !IsHostEndpoint(sender))
                {
                    continue;
                }
                if (packet.envelope.type == protocol::PacketType::PeerHello)
                {
                    if (packet.envelope.flags != 0 ||
                        packet.envelope.sequence != 0 ||
                        packet.payloadSize != 0 || role != PeerRole::Host ||
                        !RegisterGuestEndpoint(
                            sender,
                            packet.envelope.sourceActorId))
                    {
                        continue;
                    }
                    if (!peerHelloReceiveEventReported)
                    {
                        peerHelloReceiveEventReported = true;
                        diagnostics.Event(
                            "MultiplayerPeerDiscovered",
                            "guest endpoint registered before its Hero channel opened");
                    }
                    continue;
                }
                if (packet.envelope.type ==
                    protocol::PacketType::Acknowledgement)
                {
                    if (!HandleAcknowledgement(packet, sender))
                    {
                        return false;
                    }
                    continue;
                }
                if ((packet.envelope.flags &
                        protocol::packet_flag::Reliable) != 0)
                {
                    if (!HandleReliable(packet, sender))
                    {
                        return false;
                    }
                    continue;
                }
                if (packet.envelope.type ==
                        protocol::PacketType::EntityMovement &&
                    packet.envelope.flags == 0 &&
                    packet.envelope.sequence == 0 &&
                    packet.envelope.sourceActorId != 0 &&
                    packet.payloadSize != 0)
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (role == PeerRole::Host)
                    {
                        const auto connected = peers.find(Key(sender));
                        if (connected == peers.end() ||
                            connected->second.actorId !=
                                packet.envelope.sourceActorId)
                        {
                            continue;
                        }
                        connected->second.lastReceivedAt = GetTickCount64();
                    }
                    if (unreliableInbound.size() < kUnreliableQueueLimit)
                    {
                        TransportMessage message;
                        message.type = packet.envelope.type;
                        message.sourceActorId =
                            packet.envelope.sourceActorId;
                        message.payloadSize = packet.payloadSize;
                        std::memcpy(
                            message.payload.data(),
                            packet.payload,
                            packet.payloadSize);
                        unreliableInbound.push_back(std::move(message));
                    }
                    continue;
                }

                PlayerState update;
                if (packet.envelope.type !=
                        protocol::PacketType::PlayerState ||
                    packet.envelope.flags != 0 ||
                    !protocol::DecodePlayerState(
                        packet.payload,
                        packet.payloadSize,
                        update))
                {
                    continue;
                }

                const PeerRole senderRole = update.role;
                if (role == PeerRole::Host &&
                    (senderRole != PeerRole::Guest ||
                        packet.envelope.sourceActorId != update.actorId))
                {
                    continue;
                }
                if (role == PeerRole::Host)
                {
                    const std::uint64_t actorId =
                        packet.envelope.sourceActorId;
                    if (!RegisterGuestEndpoint(sender, actorId))
                    {
                        continue;
                    }
                }

                if (!receiveEventReported)
                {
                    receiveEventReported = true;
                    diagnostics.Event(
                        "MultiplayerPacketReceived",
                        role == PeerRole::Host
                            ? "first valid guest actor-channel datagram"
                            : "first valid host actor-channel datagram");
                }
                if (update.changedProperties == 0)
                {
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    std::uint32_t& previous = lastRemoteSequence[update.actorId];
                    if (!IsNewerSequence(update.sequence, previous))
                    {
                        continue;
                    }
                    previous = update.sequence;
                    ActorRecord& actor = actors[update.actorId];
                    MergeActor(actor, update);
                    actor.retiredAt = 0;
                    if (role == PeerRole::Host ||
                        update.actorId != outbound.actorId)
                    {
                        inbound.push_back(update);
                    }
                }
                if (!peerEventReported)
                {
                    peerEventReported = true;
                    diagnostics.Event(
                        "MultiplayerPeerConnected",
                        update.playerId.c_str());
                }
            }
        }

        bool SendIfNeeded()
        {
            if (!peerKnown.load(std::memory_order_acquire))
            {
                return true;
            }
            const ULONGLONG now = GetTickCount64();
            if (role == PeerRole::Host)
            {
                return SendHostRoster(now) && SendReliableIfNeeded(now) &&
                    SendUnreliableIfNeeded();
            }
            if (!SendPeerHelloIfNeeded(now))
            {
                return false;
            }
            PlayerState state;
            bool shouldSend = false;
            bool hasOutboundState = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                hasOutboundState = hasOutbound;
                if (hasOutboundState)
                {
                    const bool dirtyDue = outbound.changedProperties != 0 &&
                        now - lastSentAt >= kMinimumSendIntervalMilliseconds;
                    const bool keepAliveDue =
                        now - lastSentAt >= kKeepAliveIntervalMilliseconds;
                    shouldSend = dirtyDue || keepAliveDue;
                    if (shouldSend)
                    {
                        state = outbound;
                    }
                }
            }
            if (!hasOutboundState || !shouldSend)
            {
                return SendReliableIfNeeded(now) &&
                    SendUnreliableIfNeeded();
            }

            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodePlayerPacket(
                    state,
                    state.actorId,
                    packet,
                    packetSize))
            {
                Fail("Multiplayer: local player state could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&peer),
                sizeof(peer));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: UDP send failed in the network worker.");
                return false;
            }
            if (sent != static_cast<int>(packetSize))
            {
                Fail("Multiplayer: UDP actor-channel datagram was truncated.");
                return false;
            }
            lastSentAt = now;
            if (!sendEventReported)
            {
                sendEventReported = true;
                diagnostics.Event(
                    "MultiplayerPacketSent",
                    role == PeerRole::Host
                        ? "first host actor-channel datagram"
                        : "first guest actor-channel datagram");
            }
            return SendReliableIfNeeded(now) && SendUnreliableIfNeeded();
        }

        bool SendPacket(
            const PlayerState& state,
            const sockaddr_in& endpoint)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodePlayerPacket(
                    state,
                    localActorId,
                    packet,
                    packetSize))
            {
                Fail("Multiplayer: host player state could not be encoded.");
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            if (sent == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK)
                {
                    return true;
                }
                Fail("Multiplayer: UDP host relay failed in the network worker.");
                return false;
            }
            return sent == static_cast<int>(packetSize);
        }

        bool SendHostRoster(ULONGLONG now)
        {
            struct Datagram final
            {
                PlayerState state;
                sockaddr_in endpoint = {};
            };
            std::vector<Datagram> datagrams;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (hasOutbound)
                {
                    ActorRecord& local = actors[outbound.actorId];
                    MergeActor(local, outbound);
                }
                bool peerSetChanged = false;
                for (auto peerIterator = peers.begin();
                     peerIterator != peers.end();)
                {
                    if (now - peerIterator->second.lastReceivedAt >
                        kPeerLeaseMilliseconds)
                    {
                        const std::uint64_t retiredActorId =
                            peerIterator->second.actorId;
                        auto actor = actors.find(retiredActorId);
                        if (actor != actors.end() && !actor->second.retired)
                        {
                            actor->second.retired = true;
                            actor->second.retiredAt = now;
                            ++actor->second.state.sequence;
                            actor->second.state.changedProperties =
                                player_property::Retired;
                            inbound.push_back(actor->second.state);
                        }
                        peerIterator = peers.erase(peerIterator);
                        peerSetChanged = true;
                        continue;
                    }
                    ++peerIterator;
                }
                if (peerSetChanged)
                {
                    peerSetRevision.fetch_add(1, std::memory_order_acq_rel);
                }
                peerKnown.store(!peers.empty(), std::memory_order_release);
                for (auto& [endpointKey, connected] : peers)
                {
                    (void)endpointKey;
                    for (const auto& [actorId, actor] : actors)
                    {
                        if (actorId == connected.actorId)
                        {
                            continue;
                        }
                        const auto sentSequence =
                            connected.lastSentSequence.find(actorId);
                        const auto sentAt = connected.lastSentAt.find(actorId);
                        const bool changed = sentSequence ==
                                connected.lastSentSequence.end() ||
                            sentSequence->second != actor.state.sequence;
                        const bool keepAlive = sentAt == connected.lastSentAt.end() ||
                            now - sentAt->second >=
                                kKeepAliveIntervalMilliseconds;
                        if (!changed && !keepAlive)
                        {
                            continue;
                        }
                        PlayerState relay = actor.state;
                        relay.changedProperties = actor.retired
                            ? player_property::Retired
                            : player_property::Identity |
                                player_property::Map |
                                player_property::Movement |
                                (relay.heroMorph.IsSane() &&
                                        relay.heroClothing.IsSane() &&
                                        relay.heroBoneScales.IsSane() &&
                                        relay.heroAppearanceModifiers.IsSane()
                                    ? player_property::Appearance
                                    : 0u);
                        datagrams.push_back({relay, connected.endpoint});
                        connected.lastSentSequence[actorId] =
                            actor.state.sequence;
                        connected.lastSentAt[actorId] = now;
                    }
                }
                for (auto actor = actors.begin(); actor != actors.end();)
                {
                    if (actor->second.retired &&
                        now - actor->second.retiredAt >
                            kRetiredActorRetentionMilliseconds)
                    {
                        actor = actors.erase(actor);
                    }
                    else
                    {
                        ++actor;
                    }
                }
                if (hasOutbound)
                {
                    outbound.changedProperties = 0;
                }
            }
            for (const Datagram& datagram : datagrams)
            {
                if (!SendPacket(datagram.state, datagram.endpoint))
                {
                    return false;
                }
            }
            if (!datagrams.empty() && !sendEventReported)
            {
                sendEventReported = true;
                diagnostics.Event(
                    "MultiplayerPacketSent",
                    "first host-routed actor-channel datagram");
            }
            return true;
        }

        void Run()
        {
            diagnostics.Event(
                "MultiplayerNetworkWorkerStarted",
                "socket IO, peer leases, host relaying, and keepalive are network-owned");
            while (!stopping.load(std::memory_order_acquire))
            {
                if (!ReceiveAll() || !SendIfNeeded())
                {
                    break;
                }
                std::unique_lock<std::mutex> lock(stateMutex);
                wake.wait_for(
                    lock,
                    std::chrono::milliseconds(5),
                    [this]
                    {
                        return stopping.load(std::memory_order_acquire);
                    });
            }
            started.store(false, std::memory_order_release);
        }
    };

    UdpPeer::UdpPeer()
        : implementation_(std::make_unique<Implementation>())
    {
    }

    UdpPeer::~UdpPeer()
    {
        Shutdown();
    }

    bool UdpPeer::StartHost(
        std::uint16_t port,
        std::uint64_t localActorId,
        const core::Diagnostics& diagnostics)
    {
        if (localActorId == 0)
        {
            return false;
        }
        Shutdown();
        implementation_ = std::make_unique<Implementation>();
        Implementation& implementation = *implementation_;
        implementation.diagnostics = diagnostics;
        implementation.role = PeerRole::Host;
        implementation.localActorId = localActorId;

        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            diagnostics.Log("Multiplayer: WSAStartup failed for host transport.");
            return false;
        }
        implementation.winsockStarted = true;
        implementation.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (implementation.socket == INVALID_SOCKET)
        {
            diagnostics.Log("Multiplayer: host UDP socket creation failed.");
            Shutdown();
            return false;
        }
        u_long nonBlocking = 1;
        if (ioctlsocket(implementation.socket, FIONBIO, &nonBlocking) != 0)
        {
            diagnostics.Log("Multiplayer: host UDP nonblocking mode failed.");
            Shutdown();
            return false;
        }

        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        if (bind(
                implementation.socket,
                reinterpret_cast<const sockaddr*>(&local),
                sizeof(local)) == SOCKET_ERROR)
        {
            diagnostics.Log("Multiplayer: host UDP bind failed.");
            Shutdown();
            return false;
        }

        implementation.started.store(true, std::memory_order_release);
        implementation.worker = std::thread([&implementation]
        {
            implementation.Run();
        });
        char detail[128] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=host port=%u actor_id=%llu",
            port,
            static_cast<unsigned long long>(localActorId));
        diagnostics.Event("MultiplayerTransportReady", detail);
        return true;
    }

    bool UdpPeer::StartGuest(
        const std::string& address,
        std::uint16_t port,
        std::uint64_t localActorId,
        const core::Diagnostics& diagnostics)
    {
        if (localActorId == 0)
        {
            return false;
        }
        Shutdown();
        implementation_ = std::make_unique<Implementation>();
        Implementation& implementation = *implementation_;
        implementation.diagnostics = diagnostics;
        implementation.role = PeerRole::Guest;
        implementation.localActorId = localActorId;

        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            diagnostics.Log("Multiplayer: WSAStartup failed for guest transport.");
            return false;
        }
        implementation.winsockStarted = true;
        implementation.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (implementation.socket == INVALID_SOCKET)
        {
            diagnostics.Log("Multiplayer: guest UDP socket creation failed.");
            Shutdown();
            return false;
        }
        u_long nonBlocking = 1;
        if (ioctlsocket(implementation.socket, FIONBIO, &nonBlocking) != 0)
        {
            diagnostics.Log("Multiplayer: guest UDP nonblocking mode failed.");
            Shutdown();
            return false;
        }

        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0;
        if (bind(
                implementation.socket,
                reinterpret_cast<const sockaddr*>(&local),
                sizeof(local)) == SOCKET_ERROR)
        {
            diagnostics.Log("Multiplayer: guest UDP bind failed.");
            Shutdown();
            return false;
        }

        implementation.peer.sin_family = AF_INET;
        implementation.peer.sin_port = htons(port);
        if (InetPtonA(
                AF_INET,
                address.c_str(),
                &implementation.peer.sin_addr) != 1)
        {
            diagnostics.Log("Multiplayer: guest address must be an IPv4 address.");
            Shutdown();
            return false;
        }
        implementation.peerKnown.store(true, std::memory_order_release);
        implementation.started.store(true, std::memory_order_release);
        implementation.worker = std::thread([&implementation]
        {
            implementation.Run();
        });

        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=guest address=%s port=%u actor_id=%llu",
            address.c_str(),
            port,
            static_cast<unsigned long long>(localActorId));
        diagnostics.Event("MultiplayerTransportReady", detail);
        return true;
    }

    bool UdpPeer::Submit(const PlayerState& localUpdate)
    {
        if (!IsStarted() || HasFailed() || localUpdate.actorId == 0 ||
            localUpdate.actorId != implementation_->localActorId ||
            localUpdate.authorityEpoch == 0 ||
            (localUpdate.changedProperties & ~player_property::All) != 0 ||
            (((localUpdate.changedProperties & player_property::Appearance) != 0) &&
                (!localUpdate.heroMorph.IsSane() ||
                    !localUpdate.heroClothing.IsSane() ||
                    !localUpdate.heroBoneScales.IsSane() ||
                    !localUpdate.heroAppearanceModifiers.IsSane())))
        {
            return false;
        }
        Implementation& implementation = *implementation_;
        {
            std::lock_guard<std::mutex> lock(implementation.stateMutex);
            const bool sameChannel = implementation.hasOutbound &&
                implementation.outbound.actorId == localUpdate.actorId &&
                implementation.outbound.authorityEpoch == localUpdate.authorityEpoch;
            const std::uint32_t pending = sameChannel
                ? implementation.outbound.changedProperties
                : 0;
            implementation.outbound = localUpdate;
            implementation.outbound.changedProperties |= pending;
            implementation.hasOutbound = true;
        }
        implementation.wake.notify_one();
        return true;
    }

    bool UdpPeer::TryConsume(PlayerState& remoteUpdate)
    {
        if (implementation_ == nullptr)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(implementation_->stateMutex);
        if (implementation_->inbound.empty())
        {
            return false;
        }
        remoteUpdate = std::move(implementation_->inbound.front());
        implementation_->inbound.pop_front();
        return true;
    }

    bool UdpPeer::SubmitReliable(
        protocol::PacketType type,
        const std::uint8_t* payload,
        std::size_t payloadSize)
    {
        if (!IsStarted() || HasFailed() || !IsReliablePacketType(type) ||
            payload == nullptr || payloadSize == 0 ||
            payloadSize > protocol::MaximumPayloadBytes())
        {
            return false;
        }
        Implementation& implementation = *implementation_;
        if (implementation.role == PeerRole::Host && !HasPeer())
        {
            // Persistent authority lives above transport. A later peer gets a
            // current baseline rather than an unbounded historical backlog.
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(implementation.stateMutex);
            if (implementation.reliableOutbound.size() >= kReliableQueueLimit)
            {
                return false;
            }
            TransportMessage message;
            message.type = type;
            message.sourceActorId = implementation.localActorId;
            message.sequence = implementation.nextReliableSequence;
            implementation.nextReliableSequence = NextReliableSequence(
                implementation.nextReliableSequence);
            message.payloadSize = payloadSize;
            std::memcpy(message.payload.data(), payload, payloadSize);
            implementation.reliableOutbound.push_back(std::move(message));
        }
        implementation.wake.notify_one();
        return true;
    }

    bool UdpPeer::TryConsumeReliable(TransportMessage& message)
    {
        if (implementation_ == nullptr)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(implementation_->stateMutex);
        if (implementation_->reliableInbound.empty())
        {
            return false;
        }
        message = std::move(implementation_->reliableInbound.front());
        implementation_->reliableInbound.pop_front();
        return true;
    }

    bool UdpPeer::SubmitUnreliable(
        protocol::PacketType type,
        const std::uint8_t* payload,
        std::size_t payloadSize)
    {
        return RelayUnreliable(
            implementation_ != nullptr ? implementation_->localActorId : 0,
            type,
            payload,
            payloadSize);
    }

    bool UdpPeer::RelayUnreliable(
        std::uint64_t sourceActorId,
        protocol::PacketType type,
        const std::uint8_t* payload,
        std::size_t payloadSize)
    {
        if (!IsStarted() || HasFailed() ||
            !IsUnreliablePacketType(type) || sourceActorId == 0 ||
            payload == nullptr || payloadSize == 0 ||
            payloadSize > protocol::MaximumPayloadBytes())
        {
            return false;
        }
        Implementation& implementation = *implementation_;
        if (sourceActorId != implementation.localActorId &&
            implementation.role != PeerRole::Host)
        {
            return false;
        }
        if (implementation.role == PeerRole::Host && !HasPeer())
        {
            return true;
        }
        std::lock_guard<std::mutex> lock(implementation.stateMutex);
        if (implementation.unreliableOutbound.size() >=
            kUnreliableQueueLimit)
        {
            // This lane carries current movement only. Prefer the newest
            // sample over preserving an obsolete datagram under congestion.
            implementation.unreliableOutbound.pop_front();
        }
        TransportMessage message;
        message.type = type;
        message.sourceActorId = sourceActorId;
        message.payloadSize = payloadSize;
        std::memcpy(message.payload.data(), payload, payloadSize);
        implementation.unreliableOutbound.push_back(std::move(message));
        implementation.wake.notify_one();
        return true;
    }

    bool UdpPeer::TryConsumeUnreliable(TransportMessage& message)
    {
        if (implementation_ == nullptr)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(implementation_->stateMutex);
        if (implementation_->unreliableInbound.empty())
        {
            return false;
        }
        message = std::move(implementation_->unreliableInbound.front());
        implementation_->unreliableInbound.pop_front();
        return true;
    }

    void UdpPeer::Shutdown() noexcept
    {
        if (implementation_ == nullptr)
        {
            return;
        }
        Implementation& implementation = *implementation_;
        implementation.stopping.store(true, std::memory_order_release);
        implementation.wake.notify_all();
        if (implementation.worker.joinable())
        {
            implementation.worker.join();
        }
        if (implementation.socket != INVALID_SOCKET)
        {
            closesocket(implementation.socket);
            implementation.socket = INVALID_SOCKET;
        }
        if (implementation.winsockStarted)
        {
            WSACleanup();
            implementation.winsockStarted = false;
        }
        implementation.peerKnown.store(false, std::memory_order_release);
        implementation.started.store(false, std::memory_order_release);
    }

    bool UdpPeer::IsStarted() const noexcept
    {
        return implementation_ != nullptr &&
            implementation_->started.load(std::memory_order_acquire);
    }

    bool UdpPeer::HasPeer() const noexcept
    {
        return IsStarted() &&
            implementation_->peerKnown.load(std::memory_order_acquire);
    }

    bool UdpPeer::HasFailed() const noexcept
    {
        return implementation_ != nullptr &&
            implementation_->failed.load(std::memory_order_acquire);
    }

    std::size_t UdpPeer::ConnectedPeerCount() const noexcept
    {
        if (implementation_ == nullptr)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(implementation_->stateMutex);
        return implementation_->role == PeerRole::Host
            ? implementation_->peers.size()
            : (implementation_->peerKnown.load(std::memory_order_acquire)
                ? 1u
                : 0u);
    }

    std::uint64_t UdpPeer::PeerSetRevision() const noexcept
    {
        return implementation_ != nullptr
            ? implementation_->peerSetRevision.load(std::memory_order_acquire)
            : 0;
    }
}
