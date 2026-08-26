#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "UdpPeer.h"
#include "Multiplayer/Protocol/EntityMovementMessage.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerMovementCodec.h"
#include "Multiplayer/Transport/ConnectionNonceRegistry.h"
#include "Multiplayer/Transport/MovementTransport.h"
#include "Multiplayer/Transport/PeerDatagramCodec.h"
#include "Multiplayer/Transport/PeerSessionRegistry.h"
#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/ReliableStreamTransport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using fable::multiplayer::transport_codec::EncodeAcknowledgement;
using fable::multiplayer::transport_codec::DecodePeerHelloChallenge;
using fable::multiplayer::transport_codec::EncodePeerHello;
using fable::multiplayer::transport_codec::EncodePlayerPacket;
using fable::multiplayer::transport_codec::EncodeReliablePacket;
using fable::multiplayer::transport_codec::EncodeUnreliablePacket;
using fable::multiplayer::transport_codec::IsReliablePacketType;
using fable::multiplayer::transport_codec::IsUnreliablePacketType;

namespace
{
    constexpr ULONGLONG kMinimumSendIntervalMilliseconds = 50;
    constexpr ULONGLONG kKeepAliveIntervalMilliseconds = 1'000;
    constexpr ULONGLONG kPeerHelloIntervalMilliseconds = 250;
    constexpr ULONGLONG kPeerLeaseMilliseconds = 10'000;
    constexpr ULONGLONG kReliableResendMilliseconds = 100;
}

namespace fable::multiplayer
{
    struct UdpPeer::Implementation final
    {
        struct ActorRecord final
        {
            PlayerState state;
        };

        struct ReliableBroadcast final
        {
            struct PendingPeer final
            {
                PeerSessionRegistry::EndpointKey endpoint = {};
                std::uint64_t connectionNonce = 0;

                bool operator==(const PendingPeer& other) const noexcept
                {
                    return endpoint == other.endpoint &&
                        connectionNonce == other.connectionNonce;
                }
            };

            struct PendingPeerHash final
            {
                std::size_t operator()(
                    const PendingPeer& peer) const noexcept
                {
                    return PeerSessionRegistry::EndpointHash{}(
                               peer.endpoint) ^
                        (static_cast<std::size_t>(peer.connectionNonce) *
                            static_cast<std::size_t>(0x9E3779B1u));
                }
            };

            ReliableStreamId streamId = reliable_stream::Control;
            protocol::PacketType type = protocol::PacketType::Authority;
            std::uint64_t sourceActorId = 0;
            std::size_t payloadSize = 0;
            std::array<
                std::uint8_t,
                protocol::MaximumReliableMessageBytes>
                payload = {};
            std::unordered_set<PendingPeer, PendingPeerHash> pending;
        };

        static constexpr std::size_t ReliableBroadcastLimit = 512;
        static constexpr std::size_t ReliableBroadcastPerStreamLimit =
            ReliableStreamTransport::PerStreamQueueLimit;

        SOCKET socket = INVALID_SOCKET;
        sockaddr_in peer = {};
        core::Diagnostics diagnostics = {};
        PeerRole role = PeerRole::Guest;
        std::thread worker;
        std::mutex stateMutex;
        std::condition_variable wake;
        PlayerState outbound = {};
        ReliableStreamTransport reliable;
        MovementTransport movement;
        PeerSessionRegistry sessions;
        std::unordered_map<std::uint64_t, ActorRecord> actors;
        std::unordered_map<
            ReliableStreamId,
            std::deque<ReliableBroadcast>> reliableBroadcasts;
        std::deque<ReliableStreamId> reliableBroadcastOrder;
        std::size_t reliableBroadcastCount = 0;
        std::uint64_t localActorId = 0;
        std::uint64_t localConnectionNonce = 0;
        ULONGLONG lastSentAt = 0;
        ULONGLONG lastPeerHelloSentAt = 0;
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
        bool movingReceiveEventReported = false;
        bool entityMovementReceiveEventReported = false;
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

        static void MergeActor(ActorRecord& record, const PlayerState& update)
        {
            PlayerState& current = record.state;
            if (current.actorId != update.actorId ||
                current.authorityEpoch != update.authorityEpoch ||
                current.actorGeneration != update.actorGeneration ||
                current.mapEpoch != update.mapEpoch ||
                current.mapId != update.mapId)
            {
                current = {};
            }
            current.actorId = update.actorId;
            current.authorityEpoch = update.authorityEpoch;
            current.actorGeneration = update.actorGeneration;
            current.mapEpoch = update.mapEpoch;
            current.role = update.role;
            current.mapId = update.mapId;
            current.position = update.position;
            current.velocity = update.velocity;
            current.facing = update.facing;
            current.angularVelocity = update.angularVelocity;
            current.moving = update.moving;
            current.sequence = update.sequence;
            current.changedProperties = player_property::Movement;
        }


        bool SendPeerHello(
            const sockaddr_in& endpoint,
            const std::uint8_t* payload,
            const std::size_t payloadSize)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodePeerHello(
                    localActorId,
                    localConnectionNonce,
                    payload,
                    payloadSize,
                    packet,
                    packetSize))
            {
                return false;
            }
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packetSize),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            return sent == static_cast<int>(packetSize) ||
                (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK);
        }

        bool SendPeerHelloIfNeeded(ULONGLONG now)
        {
            if (role != PeerRole::Guest)
            {
                return true;
            }
            const bool established = sessions.HasGuestSession() &&
                !sessions.HasPendingGuestConfirmation();
            const ULONGLONG interval = established
                ? kKeepAliveIntervalMilliseconds
                : kPeerHelloIntervalMilliseconds;
            if (now - lastPeerHelloSentAt < interval)
            {
                return true;
            }

            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            std::array<std::uint8_t, PeerSessionRegistry::ChallengeBytes>
                confirmation = {};
            const std::uint8_t* payload = nullptr;
            std::size_t payloadSize = 0;
            if (sessions.HasPendingGuestConfirmation())
            {
                confirmation = sessions.GuestConfirmation();
                payload = confirmation.data();
                payloadSize = confirmation.size();
            }
            if (!EncodePeerHello(
                    localActorId,
                    localConnectionNonce,
                    payload,
                    payloadSize,
                    packet,
                    packetSize))
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
            ReliableStreamId streamId,
            std::uint64_t streamIncarnation,
            std::uint32_t sequence)
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> packet = {};
            std::size_t packetSize = 0;
            if (!EncodeAcknowledgement(
                    localActorId,
                    localConnectionNonce,
                    streamId,
                    streamIncarnation,
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
            if (role == PeerRole::Host)
            {
                PeerSessionRegistry::Peer* connected =
                    sessions.Find(sender);
                if (connected == nullptr ||
                    !sessions.ValidatePeer(
                        sender,
                        packet.envelope.sourceActorId,
                        packet.envelope.connectionNonce,
                        true))
                {
                    return true;
                }
                const ReliableStreamId streamId = {
                    static_cast<ReliableStreamKind>(
                        packet.envelope.streamKind),
                    packet.envelope.streamId};
                if (!connected->reliable.AcceptAcknowledgement(
                        streamId,
                        packet.envelope.streamIncarnation,
                        packet.envelope.sequence))
                {
                    return true;
                }
                connected->lastReceivedAt = GetTickCount64();
            }
            else if (sessions.IsHostEndpoint(sender))
            {
                if (sessions.RemoteNonce() != 0 &&
                    sessions.RemoteNonce() != packet.envelope.connectionNonce)
                {
                    return true;
                }
                const ReliableStreamId streamId = {
                    static_cast<ReliableStreamKind>(
                        packet.envelope.streamKind),
                    packet.envelope.streamId};
                if (!reliable.AcceptAcknowledgement(
                        streamId,
                        packet.envelope.streamIncarnation,
                        packet.envelope.sequence))
                {
                    return true;
                }
                sessions.TouchGuest();
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
            ReliableStreamId streamId = reliable_stream::Control;
            ReliableReceiveResult receiveResult =
                ReliableReceiveResult::Rejected;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                ReliableStreamTransport* receiver = &reliable;
                if (role == PeerRole::Host)
                {
                    PeerSessionRegistry::Peer* connected =
                        sessions.Find(sender);
                    if (!sessions.ValidatePeer(
                            sender,
                            packet.envelope.sourceActorId,
                            packet.envelope.connectionNonce,
                            true))
                    {
                        return true;
                    }
                    receiver = &connected->inboundReliable;
                }
                else
                {
                    if (!sessions.IsHostEndpoint(sender))
                    {
                        return true;
                    }
                    if (sessions.RemoteNonce() != 0 &&
                        sessions.RemoteNonce() !=
                            packet.envelope.connectionNonce)
                    {
                        return true;
                    }
                    sessions.TouchGuest();
                }

                TransportMessage message;
                message.type = packet.envelope.type;
                message.sourceActorId = packet.envelope.sourceActorId;
                message.connectionNonce = packet.envelope.connectionNonce;
                streamId = {
                    static_cast<ReliableStreamKind>(
                        packet.envelope.streamKind),
                    packet.envelope.streamId};
                message.streamIncarnation = packet.envelope.streamIncarnation;
                message.streamId = streamId;
                message.sequence = packet.envelope.sequence;
                message.payloadSize = packet.payloadSize;
                std::memcpy(
                    message.payload.data(), packet.payload, packet.payloadSize);
                receiveResult = receiver->AcceptIncoming(std::move(message));
                acknowledge = receiveResult == ReliableReceiveResult::Accepted ||
                    receiveResult == ReliableReceiveResult::Duplicate;
            }
            return !acknowledge ||
                (SendAcknowledgement(
                    sender,
                    streamId,
                    packet.envelope.streamIncarnation,
                    packet.envelope.sequence) &&
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
                    localConnectionNonce,
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
            if (!EncodeUnreliablePacket(
                    message,
                    localConnectionNonce,
                    packet,
                    packetSize))
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
                for (TransportMessage& message : movement.TakeEntityOutbound())
                {
                    if (role == PeerRole::Host)
                    {
                        for (const auto& entry : sessions.Peers())
                        {
                            const PeerSessionRegistry::Peer& connected =
                                entry.second;
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

        void DistributeReliableBroadcasts()
        {
            // Visit every logical stream once per dispatch pass. A stream
            // that is blocked for one peer rotates behind the other streams,
            // while later messages in that stream may still advance for
            // peers that have capacity.
            const std::size_t scheduledStreams =
                reliableBroadcastOrder.size();
            for (std::size_t attempt = 0;
                 attempt < scheduledStreams; ++attempt)
            {
                const ReliableStreamId streamId =
                    reliableBroadcastOrder.front();
                reliableBroadcastOrder.pop_front();
                const auto stream = reliableBroadcasts.find(streamId);
                if (stream == reliableBroadcasts.end())
                {
                    continue;
                }
                std::deque<ReliableBroadcast>& broadcasts = stream->second;
                for (auto broadcast = broadcasts.begin();
                     broadcast != broadcasts.end();)
                {
                    for (auto pending = broadcast->pending.begin();
                         pending != broadcast->pending.end();)
                    {
                        const auto peerIterator =
                            sessions.Peers().find(pending->endpoint);
                        if (peerIterator == sessions.Peers().end() ||
                            peerIterator->second.connectionNonce !=
                                pending->connectionNonce)
                        {
                            pending = broadcast->pending.erase(pending);
                            continue;
                        }
                        PeerSessionRegistry::Peer& connected =
                            peerIterator->second;
                        if (!connected.reliable.CanEnqueueMessage(
                                broadcast->streamId,
                                broadcast->type,
                                broadcast->payload.data(),
                                broadcast->payloadSize))
                        {
                            ++pending;
                            continue;
                        }
                        if (!connected.reliable.Enqueue(
                                broadcast->streamId,
                                broadcast->type,
                                broadcast->sourceActorId,
                                broadcast->payload.data(),
                                broadcast->payloadSize))
                        {
                            ++pending;
                            continue;
                        }
                        pending = broadcast->pending.erase(pending);
                    }
                    if (broadcast->pending.empty())
                    {
                        broadcast = broadcasts.erase(broadcast);
                        --reliableBroadcastCount;
                    }
                    else
                    {
                        ++broadcast;
                    }
                }
                if (broadcasts.empty())
                {
                    reliableBroadcasts.erase(stream);
                }
                else
                {
                    reliableBroadcastOrder.push_back(streamId);
                }
            }
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
                if (role == PeerRole::Host)
                {
                    DistributeReliableBroadcasts();
                    for (auto& [endpointKey, connected] : sessions.Peers())
                    {
                        (void)endpointKey;
                        for (TransportMessage& message : connected.reliable.Due(
                                 now, kReliableResendMilliseconds))
                        {
                            datagrams.push_back(
                                {std::move(message), connected.endpoint});
                        }
                    }
                }
                else
                {
                    for (TransportMessage& message : reliable.Due(
                             now, kReliableResendMilliseconds))
                    {
                        datagrams.push_back({std::move(message), peer});
                    }
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
                if (role == PeerRole::Guest && !sessions.IsHostEndpoint(sender))
                {
                    continue;
                }
                if (packet.envelope.type == protocol::PacketType::PeerHello)
                {
                    if (packet.envelope.flags != 0 ||
                        packet.envelope.sequence != 0 ||
                        (packet.payloadSize != 0 &&
                            packet.payloadSize !=
                                PeerSessionRegistry::ChallengeBytes))
                    {
                        continue;
                    }
                    if (role == PeerRole::Host)
                    {
                        if (packet.payloadSize == 0)
                        {
                            std::array<
                                std::uint8_t,
                                PeerSessionRegistry::ChallengeBytes> challenge = {};
                            bool active = false;
                            {
                                std::lock_guard<std::mutex> lock(stateMutex);
                                active = sessions.ValidatePeer(
                                    sender,
                                    packet.envelope.sourceActorId,
                                    packet.envelope.connectionNonce,
                                    true);
                                if (!active &&
                                    !sessions.IssueGuestChallenge(
                                        sender,
                                        packet.envelope.sourceActorId,
                                        packet.envelope.connectionNonce,
                                        challenge))
                                {
                                    continue;
                                }
                            }
                            if (!SendPeerHello(
                                    sender,
                                    active ? nullptr : challenge.data(),
                                    active ? 0 : challenge.size()))
                            {
                                continue;
                            }
                        }
                        else
                        {
                            std::array<
                                std::uint8_t,
                                PeerSessionRegistry::ChallengeBytes> challenge = {};
                            if (!DecodePeerHelloChallenge(
                                    packet.payload,
                                    packet.payloadSize,
                                    challenge))
                            {
                                continue;
                            }
                            std::vector<PeerSessionRegistry::RetiredSession>
                                retired;
                            std::lock_guard<std::mutex> lock(stateMutex);
                            if (!sessions.ConfirmGuest(
                                    sender,
                                    packet.envelope.sourceActorId,
                                    packet.envelope.connectionNonce,
                                    challenge,
                                    retired))
                            {
                                continue;
                            }
                            for (const auto& session : retired)
                            {
                                actors.erase(session.actorId);
                                movement.ForgetActor(session.actorId);
                            }
                            peerSetRevision.store(
                                sessions.Revision(),
                                std::memory_order_release);
                            peerKnown.store(
                                sessions.HasPeers(),
                                std::memory_order_release);
                        }
                            if (!SendPeerHello(sender, nullptr, 0))
                            {
                                return false;
                            }
                        }
                    else
                    {
                        if (packet.payloadSize == 0)
                        {
                            std::lock_guard<std::mutex> lock(stateMutex);
                            if (!sessions.CompleteGuestHandshake(
                                    packet.envelope.connectionNonce))
                            {
                                continue;
                            }
                            peerSetRevision.store(
                                sessions.Revision(),
                                std::memory_order_release);
                            peerKnown.store(true, std::memory_order_release);
                            continue;
                        }
                        if (packet.payloadSize !=
                            PeerSessionRegistry::ChallengeBytes)
                        {
                            continue;
                        }
                        std::array<
                            std::uint8_t,
                            PeerSessionRegistry::ChallengeBytes> challenge = {};
                        if (!DecodePeerHelloChallenge(
                                packet.payload,
                                packet.payloadSize,
                                challenge))
                        {
                            continue;
                        }
                        bool changed = false;
                        {
                            std::lock_guard<std::mutex> lock(stateMutex);
                            if (!sessions.AcceptHostChallenge(
                                    packet.envelope.connectionNonce,
                                    localConnectionNonce,
                                    challenge,
                                    changed))
                            {
                                continue;
                            }
                            if (changed)
                            {
                                reliable.Clear();
                                actors.clear();
                                movement.Clear();
                            }
                            peerSetRevision.store(
                                sessions.Revision(),
                                std::memory_order_release);
                            peerKnown.store(true, std::memory_order_release);
                        }
                        if (!SendPeerHello(
                                sender,
                                challenge.data(),
                                challenge.size()))
                        {
                            return false;
                        }
                    }
                    if (!peerHelloReceiveEventReported)
                    {
                        peerHelloReceiveEventReported = true;
                        diagnostics.Event(
                            "MultiplayerPeerDiscovered",
                            role == PeerRole::Host
                                ? "guest challenge completed before its Hero channel opened"
                                : "host challenge accepted before its Hero channel opened");
                    }
                    continue;
                }
                if (role == PeerRole::Guest && !sessions.HasGuestSession())
                {
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
                        if (!sessions.ValidatePeer(
                                sender,
                                packet.envelope.sourceActorId,
                                packet.envelope.connectionNonce,
                                true))
                        {
                            continue;
                        }
                    }
                    else if (sessions.RemoteNonce() != 0 &&
                        sessions.RemoteNonce() != packet.envelope.connectionNonce)
                    {
                        continue;
                    }
                    else
                    {
                        sessions.TouchGuest();
                    }
                    TransportMessage message;
                    message.type = packet.envelope.type;
                    message.sourceActorId = packet.envelope.sourceActorId;
                    message.connectionNonce = packet.envelope.connectionNonce;
                    message.payloadSize = packet.payloadSize;
                    std::memcpy(
                        message.payload.data(),
                        packet.payload,
                        packet.payloadSize);
                    const bool accepted = movement.AcceptEntity(
                        std::move(message));
                    if (accepted && !entityMovementReceiveEventReported)
                    {
                        entityMovementReceiveEventReported = true;
                        char detail[256] = {};
                        std::snprintf(
                            detail,
                            sizeof(detail),
                            "source_actor_id=%llu payload_bytes=%zu role=%s",
                            static_cast<unsigned long long>(
                                packet.envelope.sourceActorId),
                            packet.payloadSize,
                            role == PeerRole::Host ? "host" : "guest");
                        diagnostics.Event(
                            "MultiplayerEntityMovementDatagramAccepted",
                            detail);
                    }
                    continue;
                }

                protocol::PlayerMovementMessage playerMovement;
                if (packet.envelope.type !=
                        protocol::PacketType::PlayerMovement ||
                    packet.envelope.flags != 0 ||
                    packet.envelope.sequence != 0 ||
                    packet.envelope.sourceActorId == 0 ||
                    !protocol::DecodePlayerMovementMessage(
                        packet.payload,
                        packet.payloadSize,
                        playerMovement) ||
                    playerMovement.actorId != packet.envelope.sourceActorId)
                {
                    continue;
                }
                    if (role == PeerRole::Host)
                    {
                        const std::uint64_t actorId =
                            packet.envelope.sourceActorId;
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (!sessions.ValidatePeer(
                            sender,
                            actorId,
                            packet.envelope.connectionNonce,
                            true))
                    {
                        continue;
                    }
                }
                else
                {
                    if (sessions.RemoteNonce() != 0 &&
                        sessions.RemoteNonce() !=
                            packet.envelope.connectionNonce)
                    {
                        continue;
                    }
                }

                PlayerState update;
                update.actorId = playerMovement.actorId;
                update.authorityEpoch = playerMovement.authorityEpoch;
                update.actorGeneration = playerMovement.actorGeneration;
                update.mapEpoch = playerMovement.mapEpoch;
                update.sequence = playerMovement.sequence;
                update.mapId = playerMovement.mapId;
                update.role = role == PeerRole::Host
                    ? PeerRole::Guest
                    : PeerRole::Host;
                update.moving = playerMovement.moving;
                update.position = playerMovement.position;
                update.velocity = playerMovement.velocity;
                update.facing = playerMovement.facing;
                update.angularVelocity = playerMovement.angularVelocity;
                update.changedProperties = player_property::Movement;

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
                    if (role == PeerRole::Guest)
                    {
                        sessions.TouchGuest();
                    }
                    if (!movement.AcceptPlayer(update))
                    {
                        continue;
                    }
                    if (update.moving && !movingReceiveEventReported)
                    {
                        movingReceiveEventReported = true;
                        char detail[320] = {};
                        std::snprintf(
                            detail,
                            sizeof(detail),
                            "actor_id=%llu generation=%u map_epoch=%u sequence=%u position=(%.3f,%.3f,%.3f) velocity=(%.3f,%.3f,%.3f)",
                            static_cast<unsigned long long>(update.actorId),
                            update.actorGeneration,
                            update.mapEpoch,
                            update.sequence,
                            update.position.x,
                            update.position.y,
                            update.position.z,
                            update.velocity.x,
                            update.velocity.y,
                            update.velocity.z);
                        diagnostics.Event(
                            "MultiplayerPlayerMovementAccepted", detail);
                    }
                    ActorRecord& actor = actors[update.actorId];
                    const bool newRelayIncarnation =
                        actor.state.actorId != 0 &&
                        (actor.state.authorityEpoch != update.authorityEpoch ||
                            actor.state.actorGeneration !=
                                update.actorGeneration ||
                            actor.state.mapEpoch != update.mapEpoch ||
                            actor.state.mapId != update.mapId);
                    MergeActor(actor, update);
                    if (newRelayIncarnation && role == PeerRole::Host)
                    {
                        for (auto& [endpoint, connected] : sessions.Peers())
                        {
                            (void)endpoint;
                            connected.lastSentSequence.erase(update.actorId);
                            connected.lastSentAt.erase(update.actorId);
                        }
                    }
                }
                if (!peerEventReported)
                {
                    peerEventReported = true;
                    diagnostics.Event(
                        "MultiplayerPeerConnected",
                        role == PeerRole::Host
                            ? "guest movement actor"
                            : "host movement actor");
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
            if (role == PeerRole::Guest)
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (sessions.GuestLeaseExpired(
                        now, kPeerLeaseMilliseconds))
                {
                    sessions.ResetGuestSession();
                    localConnectionNonce =
                        ConnectionNonceRegistry::GenerateLocal();
                    sessions.SetLocalGuestNonce(localConnectionNonce);
                    reliable.Clear();
                    actors.clear();
                    movement.Clear();
                    lastPeerHelloSentAt = 0;
                    peerSetRevision.store(
                        sessions.Revision(),
                        std::memory_order_release);
                }
            }
            if (role == PeerRole::Guest && !sessions.HasGuestSession())
            {
                return SendPeerHelloIfNeeded(now);
            }
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
                        // A keepalive reuses the latest transform snapshot.
                        // Dirty tracking remains local bookkeeping; every
                        // player datagram on the wire is a Movement message.
                        state.changedProperties = player_property::Movement;
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
                    localConnectionNonce,
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
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (hasOutbound && outbound.actorId == state.actorId &&
                    outbound.authorityEpoch == state.authorityEpoch &&
                    outbound.actorGeneration == state.actorGeneration &&
                    outbound.mapEpoch == state.mapEpoch &&
                    outbound.sequence == state.sequence)
                {
                    outbound.changedProperties &=
                        ~state.changedProperties;
                }
            }
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
                    state.actorId,
                    localConnectionNonce,
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
            std::vector<sockaddr_in> helloEndpoints;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (hasOutbound)
                {
                    ActorRecord& local = actors[outbound.actorId];
                    const bool newRelayIncarnation =
                        local.state.actorId != 0 &&
                        (local.state.authorityEpoch != outbound.authorityEpoch ||
                            local.state.actorGeneration !=
                                outbound.actorGeneration ||
                            local.state.mapEpoch != outbound.mapEpoch ||
                            local.state.mapId != outbound.mapId);
                    MergeActor(local, outbound);
                    if (newRelayIncarnation)
                    {
                        for (auto& [endpoint, connected] : sessions.Peers())
                        {
                            (void)endpoint;
                            connected.lastSentSequence.erase(outbound.actorId);
                            connected.lastSentAt.erase(outbound.actorId);
                        }
                    }
                }
                for (const auto& session : sessions.Expire(
                         now, kPeerLeaseMilliseconds))
                {
                    actors.erase(session.actorId);
                    movement.ForgetActor(session.actorId);
                    for (auto& [endpoint, connected] : sessions.Peers())
                    {
                        (void)endpoint;
                        connected.lastSentSequence.erase(session.actorId);
                        connected.lastSentAt.erase(session.actorId);
                    }
                }
                peerSetRevision.store(
                    sessions.Revision(), std::memory_order_release);
                peerKnown.store(
                    sessions.HasPeers(), std::memory_order_release);
                for (auto& [endpointKey, connected] : sessions.Peers())
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
                        relay.changedProperties = player_property::Movement;
                        datagrams.push_back({relay, connected.endpoint});
                        connected.lastSentSequence[actorId] =
                            actor.state.sequence;
                        connected.lastSentAt[actorId] = now;
                    }
                    if (now - connected.lastHelloSentAt >=
                        kKeepAliveIntervalMilliseconds)
                    {
                        connected.lastHelloSentAt = now;
                        helloEndpoints.push_back(connected.endpoint);
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
            for (const sockaddr_in& endpoint : helloEndpoints)
            {
                if (!SendPeerHello(endpoint, nullptr, 0))
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
        implementation.localConnectionNonce =
            ConnectionNonceRegistry::GenerateLocal();

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
        implementation.localConnectionNonce =
            ConnectionNonceRegistry::GenerateLocal();
        implementation.sessions.SetLocalGuestNonce(
            implementation.localConnectionNonce);

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
        implementation.sessions.SetGuestEndpoint(implementation.peer);
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
            localUpdate.actorGeneration == 0 || localUpdate.mapEpoch == 0 ||
            localUpdate.mapId == 0 || localUpdate.sequence == 0 ||
            localUpdate.changedProperties != player_property::Movement)
        {
            return false;
        }
        Implementation& implementation = *implementation_;
        {
            std::lock_guard<std::mutex> lock(implementation.stateMutex);
            const bool sameChannel = implementation.hasOutbound &&
                implementation.outbound.actorId == localUpdate.actorId &&
                implementation.outbound.authorityEpoch == localUpdate.authorityEpoch &&
                implementation.outbound.actorGeneration ==
                    localUpdate.actorGeneration &&
                implementation.outbound.mapEpoch == localUpdate.mapEpoch &&
                implementation.outbound.mapId == localUpdate.mapId;
            const std::uint32_t pending = sameChannel
                ? implementation.outbound.changedProperties
                : 0;
            implementation.outbound = localUpdate;
            implementation.outbound.changedProperties =
                pending | player_property::Movement;
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
        return implementation_->movement.TryConsumePlayer(remoteUpdate);
    }

    bool UdpPeer::SubmitReliable(
        const ReliableStreamId streamId,
        protocol::PacketType type,
        const std::uint8_t* payload,
        std::size_t payloadSize)
    {
        if (!IsStarted() || HasFailed() || !IsReliablePacketType(type) ||
            payload == nullptr || payloadSize == 0 ||
            payloadSize > protocol::MaximumReliableMessageBytes)
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
            if (implementation.role == PeerRole::Host)
            {
                if (!implementation.sessions.HasPeers())
                {
                    return true;
                }
                if (implementation.reliableBroadcastCount >=
                    Implementation::ReliableBroadcastLimit)
                {
                    return false;
                }
                if (!ReliableStreamTransport::IsMessageValid(
                        streamId, type, payload, payloadSize))
                {
                    return false;
                }
                auto broadcasts =
                    implementation.reliableBroadcasts.find(streamId);
                if (broadcasts !=
                        implementation.reliableBroadcasts.end() &&
                    broadcasts->second.size() >=
                        Implementation::ReliableBroadcastPerStreamLimit)
                {
                    return false;
                }
                if (broadcasts ==
                    implementation.reliableBroadcasts.end())
                {
                    if (implementation.reliableBroadcasts.size() >=
                        ReliableStreamTransport::StreamMetadataLimit)
                    {
                        return false;
                    }
                    broadcasts = implementation.reliableBroadcasts.emplace(
                        streamId,
                        std::deque<Implementation::ReliableBroadcast>{}).first;
                    implementation.reliableBroadcastOrder.push_back(streamId);
                }
                Implementation::ReliableBroadcast broadcast;
                broadcast.streamId = streamId;
                broadcast.type = type;
                broadcast.sourceActorId = implementation.localActorId;
                broadcast.payloadSize = payloadSize;
                std::memcpy(
                    broadcast.payload.data(), payload, payloadSize);
                for (const auto& [endpoint, peer] :
                     implementation.sessions.Peers())
                {
                    broadcast.pending.insert({
                        endpoint,
                        peer.connectionNonce});
                }
                broadcasts->second.push_back(std::move(broadcast));
                ++implementation.reliableBroadcastCount;
            }
            else
            {
                if (!implementation.reliable.Enqueue(
                        streamId,
                        type,
                        implementation.localActorId,
                        payload,
                        payloadSize))
                {
                    return false;
                }
            }
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
        if (implementation_->role == PeerRole::Host)
        {
            for (auto& [endpoint, peer] : implementation_->sessions.Peers())
            {
                (void)endpoint;
                if (peer.inboundReliable.TryConsume(message))
                {
                    return true;
                }
            }
            return false;
        }
        return implementation_->reliable.TryConsume(message);
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
        if (!implementation.movement.QueueEntity(
                sourceActorId, type, payload, payloadSize))
        {
            return false;
        }
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
        return implementation_->movement.TryConsumeEntity(message);
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
            ? implementation_->sessions.Peers().size()
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

    std::uint64_t UdpPeer::ConnectionNonce() const noexcept
    {
        return implementation_ != nullptr
            ? implementation_->localConnectionNonce
            : 0;
    }

    std::vector<std::uint64_t> UdpPeer::ConnectedActorIds() const
    {
        std::vector<std::uint64_t> result;
        if (implementation_ == nullptr)
        {
            return result;
        }
        std::lock_guard<std::mutex> lock(implementation_->stateMutex);
        if (implementation_->role == PeerRole::Host)
        {
            result.reserve(implementation_->sessions.Peers().size());
            for (const auto& [endpoint, peer] : implementation_->sessions.Peers())
            {
                (void)endpoint;
                if (peer.actorId != 0)
                {
                    result.push_back(peer.actorId);
                }
            }
        }
        return result;
    }
}
