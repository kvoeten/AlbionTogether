#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "UdpPeer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr std::uint32_t kProtocolMagic = 0x504D5446;
    constexpr std::uint16_t kProtocolVersion = 10;
    constexpr float kBoneScaleQuantization = 4'095.0f;
    constexpr ULONGLONG kMinimumSendIntervalMilliseconds = 50;
    constexpr ULONGLONG kKeepAliveIntervalMilliseconds = 1'000;
    constexpr ULONGLONG kPeerLeaseMilliseconds = 10'000;
    constexpr ULONGLONG kRetiredActorRetentionMilliseconds = 2'000;

#pragma pack(push, 1)
    struct WireHeroBoneScale final
    {
        std::uint16_t boneIndex = 0;
        std::uint16_t x = 4'095;
        std::uint16_t y = 4'095;
        std::uint16_t z = 4'095;
    };

    struct WirePlayerState final
    {
        std::uint32_t magic = kProtocolMagic;
        std::uint16_t version = kProtocolVersion;
        std::uint16_t size = 0;
        std::uint32_t sequence = 0;
        std::uint32_t acknowledgedSequence = 0;
        std::uint32_t changedProperties = 0;
        std::uint32_t authorityEpoch = 0;
        std::uint64_t actorId = 0;
        std::uint8_t role = 0;
        std::uint8_t moving = 0;
        std::uint16_t reserved = 0;
        float position[3] = {};
        float velocity[3] = {};
        float facing = 0.0f;
        float angularVelocity = 0.0f;
        char playerId[48] = {};
        char mapName[96] = {};
        char appearanceDefinition[96] = {};
        std::uint8_t appearanceValid = 0;
        std::uint8_t appearanceChild = 0;
        std::uint16_t appearanceReserved = 0;
        float heroMorph[8] = {};
        std::int32_t heroClothingDefinitionIndices[
            fable::game::hero_pawn::appearance::
                HeroClothingState::SlotCount] = {};
        std::uint16_t heroAppearanceModifierCount = 0;
        std::uint16_t heroAppearanceModifierReserved = 0;
        std::int32_t heroAppearanceModifierDefinitionIndices[
            fable::game::hero_pawn::appearance::
                HeroAppearanceModifierState::MaximumEntries] = {};
        std::uint16_t heroBoneScaleCount = 0;
        std::uint16_t heroBoneScaleReserved = 0;
        WireHeroBoneScale heroBoneScales[
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries] = {};
    };
#pragma pack(pop)

    constexpr std::size_t kWirePlayerStateBaseSize =
        offsetof(WirePlayerState, heroBoneScales);
    static_assert(
        sizeof(WirePlayerState) <= 1'472,
        "The complete appearance baseline must fit one normal UDP payload.");

    template <std::size_t Size>
    void CopyText(char (&destination)[Size], const std::string& source)
    {
        static_assert(Size > 0);
        destination[0] = '\0';
        strncpy_s(destination, source.c_str(), _TRUNCATE);
    }

    template <std::size_t Size>
    bool IsTerminated(const char (&value)[Size])
    {
        return std::memchr(value, '\0', Size) != nullptr;
    }

    bool IsFinite(const WirePlayerState& state)
    {
        const bool morphFinite = std::all_of(
            std::begin(state.heroMorph),
            std::end(state.heroMorph),
            [](float value) { return std::isfinite(value); });
        return morphFinite && std::isfinite(state.position[0]) &&
            std::isfinite(state.position[1]) &&
            std::isfinite(state.position[2]) &&
            std::isfinite(state.velocity[0]) &&
            std::isfinite(state.velocity[1]) &&
            std::isfinite(state.velocity[2]) &&
            std::isfinite(state.facing) &&
            std::isfinite(state.angularVelocity);
    }

    bool IsSaneAppearance(const WirePlayerState& state)
    {
        const bool carriesAppearance =
            (state.changedProperties &
                fable::multiplayer::player_property::Appearance) != 0;
        if (!carriesAppearance)
        {
            return state.appearanceValid == 0 &&
                state.heroAppearanceModifierCount == 0 &&
                state.heroBoneScaleCount == 0;
        }
        if (state.appearanceValid != 1 || state.appearanceChild > 1 ||
            state.heroBoneScaleCount >
                fable::game::hero_pawn::appearance::HeroBoneScaleState::
                    MaximumEntries ||
            state.heroAppearanceModifierCount >
                fable::game::hero_pawn::appearance::
                    HeroAppearanceModifierState::MaximumEntries)
        {
            return false;
        }
        const auto unitValue = [](float value)
        {
            return value >= -0.001f && value <= 1.001f;
        };
        for (std::size_t index = 0; index < 7; ++index)
        {
            if (!unitValue(state.heroMorph[index]))
            {
                return false;
            }
        }
        for (const std::int32_t definitionIndex :
             state.heroClothingDefinitionIndices)
        {
            if (definitionIndex != -1 &&
                (definitionIndex <= 0 || definitionIndex >= 1'000'000))
            {
                return false;
            }
        }
        for (std::size_t index = 0;
             index < state.heroAppearanceModifierCount;
             ++index)
        {
            const std::int32_t definitionIndex =
                state.heroAppearanceModifierDefinitionIndices[index];
            if (definitionIndex <= 0 || definitionIndex >= 1'000'000)
            {
                return false;
            }
            for (std::size_t earlier = 0; earlier < index; ++earlier)
            {
                if (state.heroAppearanceModifierDefinitionIndices[earlier] ==
                    definitionIndex)
                {
                    return false;
                }
            }
        }
        for (std::size_t index = 0;
             index < state.heroBoneScaleCount;
             ++index)
        {
            const WireHeroBoneScale& scale = state.heroBoneScales[index];
            if (scale.boneIndex >= 1'024 ||
                scale.x > 16 * kBoneScaleQuantization ||
                scale.y > 16 * kBoneScaleQuantization ||
                scale.z > 16 * kBoneScaleQuantization)
            {
                return false;
            }
        }
        return state.heroMorph[7] >= -16.0f &&
            state.heroMorph[7] <= 16.0f;
    }

    std::size_t PacketSize(const WirePlayerState& packet)
    {
        return kWirePlayerStateBaseSize +
            static_cast<std::size_t>(packet.heroBoneScaleCount) *
                sizeof(WireHeroBoneScale);
    }

    bool IsNewerSequence(std::uint32_t candidate, std::uint32_t previous)
    {
        return previous == 0 ||
            static_cast<std::int32_t>(candidate - previous) > 0;
    }

    std::uint16_t QuantizeBoneScale(float value)
    {
        return static_cast<std::uint16_t>(
            std::lround(value * kBoneScaleQuantization));
    }

    float DequantizeBoneScale(std::uint16_t value)
    {
        return static_cast<float>(value) / kBoneScaleQuantization;
    }

    std::size_t Serialize(
        const fable::multiplayer::PlayerState& state,
        std::uint32_t acknowledgedSequence,
        WirePlayerState& packet)
    {
        packet.sequence = state.sequence;
        packet.acknowledgedSequence = acknowledgedSequence;
        packet.changedProperties = state.changedProperties;
        packet.authorityEpoch = state.authorityEpoch;
        packet.actorId = state.actorId;
        packet.role = static_cast<std::uint8_t>(state.role);
        packet.moving = state.moving ? 1 : 0;
        packet.position[0] = state.position.x;
        packet.position[1] = state.position.y;
        packet.position[2] = state.position.z;
        packet.velocity[0] = state.velocity.x;
        packet.velocity[1] = state.velocity.y;
        packet.velocity[2] = state.velocity.z;
        packet.facing = state.facing;
        packet.angularVelocity = state.angularVelocity;
        CopyText(packet.playerId, state.playerId);
        CopyText(packet.mapName, state.mapName);
        CopyText(packet.appearanceDefinition, state.appearanceDefinition);
        if ((state.changedProperties &
                fable::multiplayer::player_property::Appearance) != 0)
        {
            packet.appearanceValid = state.heroMorph.valid ? 1 : 0;
            packet.appearanceChild = state.heroMorph.child ? 1 : 0;
            packet.heroMorph[0] = state.heroMorph.strength;
            packet.heroMorph[1] = state.heroMorph.berserk;
            packet.heroMorph[2] = state.heroMorph.will;
            packet.heroMorph[3] = state.heroMorph.skill;
            packet.heroMorph[4] = state.heroMorph.age;
            packet.heroMorph[5] = state.heroMorph.alignment;
            packet.heroMorph[6] = state.heroMorph.fatness;
            packet.heroMorph[7] = state.heroMorph.auxiliary;
            for (std::size_t index = 0;
                 index < state.heroClothing.definitionIndices.size();
                 ++index)
            {
                packet.heroClothingDefinitionIndices[index] =
                    state.heroClothing.definitionIndices[index];
            }
            packet.heroAppearanceModifierCount =
                static_cast<std::uint16_t>(
                    state.heroAppearanceModifiers.count);
            for (std::size_t index = 0;
                 index < state.heroAppearanceModifiers.count;
                 ++index)
            {
                packet.heroAppearanceModifierDefinitionIndices[index] =
                    state.heroAppearanceModifiers.definitionIndices[index];
            }
            packet.heroBoneScaleCount = static_cast<std::uint16_t>(
                state.heroBoneScales.count);
            for (std::size_t index = 0;
                 index < state.heroBoneScales.count;
                 ++index)
            {
                const auto& source = state.heroBoneScales.entries[index];
                auto& destination = packet.heroBoneScales[index];
                destination.boneIndex = source.boneIndex;
                destination.x = QuantizeBoneScale(source.x);
                destination.y = QuantizeBoneScale(source.y);
                destination.z = QuantizeBoneScale(source.z);
            }
        }
        const std::size_t bytes = PacketSize(packet);
        packet.size = static_cast<std::uint16_t>(bytes);
        return bytes;
    }

    fable::multiplayer::PlayerState Deserialize(const WirePlayerState& packet)
    {
        fable::multiplayer::PlayerState state;
        state.sequence = packet.sequence;
        state.changedProperties = packet.changedProperties;
        state.authorityEpoch = packet.authorityEpoch;
        state.actorId = packet.actorId;
        state.role = static_cast<fable::multiplayer::PeerRole>(packet.role);
        state.moving = packet.moving != 0;
        state.position = {
            packet.position[0], packet.position[1], packet.position[2]};
        state.velocity = {
            packet.velocity[0], packet.velocity[1], packet.velocity[2]};
        state.facing = packet.facing;
        state.angularVelocity = packet.angularVelocity;
        state.playerId = packet.playerId;
        state.mapName = packet.mapName;
        state.appearanceDefinition = packet.appearanceDefinition;
        state.heroMorph.valid = packet.appearanceValid != 0;
        state.heroMorph.child = packet.appearanceChild != 0;
        state.heroMorph.strength = packet.heroMorph[0];
        state.heroMorph.berserk = packet.heroMorph[1];
        state.heroMorph.will = packet.heroMorph[2];
        state.heroMorph.skill = packet.heroMorph[3];
        state.heroMorph.age = packet.heroMorph[4];
        state.heroMorph.alignment = packet.heroMorph[5];
        state.heroMorph.fatness = packet.heroMorph[6];
        state.heroMorph.auxiliary = packet.heroMorph[7];
        state.heroClothing.valid = packet.appearanceValid != 0;
        for (std::size_t index = 0;
             index < state.heroClothing.definitionIndices.size();
             ++index)
        {
            state.heroClothing.definitionIndices[index] =
                packet.heroClothingDefinitionIndices[index];
        }
        state.heroAppearanceModifiers.valid = packet.appearanceValid != 0;
        state.heroAppearanceModifiers.count =
            packet.heroAppearanceModifierCount;
        for (std::size_t index = 0;
             index < packet.heroAppearanceModifierCount;
             ++index)
        {
            state.heroAppearanceModifiers.definitionIndices[index] =
                packet.heroAppearanceModifierDefinitionIndices[index];
        }
        state.heroBoneScales.valid = packet.appearanceValid != 0;
        state.heroBoneScales.count = packet.heroBoneScaleCount;
        for (std::size_t index = 0;
             index < packet.heroBoneScaleCount;
             ++index)
        {
            const auto& source = packet.heroBoneScales[index];
            auto& destination = state.heroBoneScales.entries[index];
            destination.boneIndex = source.boneIndex;
            destination.x = DequantizeBoneScale(source.x);
            destination.y = DequantizeBoneScale(source.y);
            destination.z = DequantizeBoneScale(source.z);
        }
        return state;
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
        std::unordered_map<std::uint64_t, std::uint32_t> lastRemoteSequence;
        std::unordered_map<EndpointKey, Peer, EndpointHash> peers;
        std::unordered_map<std::uint64_t, ActorRecord> actors;
        ULONGLONG lastSentAt = 0;
        std::atomic_bool started{false};
        std::atomic_bool stopping{false};
        std::atomic_bool failed{false};
        std::atomic_bool peerKnown{false};
        bool hasOutbound = false;
        bool winsockStarted = false;
        bool peerEventReported = false;
        bool sendEventReported = false;
        bool receiveEventReported = false;

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

        bool ReceiveAll()
        {
            for (;;)
            {
                WirePlayerState packet = {};
                sockaddr_in sender = {};
                int senderSize = sizeof(sender);
                const int received = recvfrom(
                    socket,
                    reinterpret_cast<char*>(&packet),
                    static_cast<int>(sizeof(packet)),
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
                if (received < static_cast<int>(kWirePlayerStateBaseSize) ||
                    packet.magic != kProtocolMagic ||
                    packet.version != kProtocolVersion ||
                    packet.size != static_cast<std::uint16_t>(received) ||
                    packet.heroBoneScaleCount >
                        game::hero_pawn::appearance::HeroBoneScaleState::
                            MaximumEntries ||
                    PacketSize(packet) != static_cast<std::size_t>(received) ||
                    (packet.changedProperties & ~player_property::All) != 0 ||
                    !IsFinite(packet) ||
                    !IsSaneAppearance(packet) ||
                    !IsTerminated(packet.playerId) ||
                    !IsTerminated(packet.mapName) ||
                    !IsTerminated(packet.appearanceDefinition))
                {
                    continue;
                }

                const PeerRole senderRole = static_cast<PeerRole>(packet.role);
                if (role == PeerRole::Host && senderRole != PeerRole::Guest)
                {
                    continue;
                }
                if (role == PeerRole::Guest &&
                    (sender.sin_addr.s_addr != peer.sin_addr.s_addr ||
                        sender.sin_port != peer.sin_port))
                {
                    continue;
                }
                if (role == PeerRole::Host)
                {
                    const EndpointKey endpoint = Key(sender);
                    std::lock_guard<std::mutex> lock(stateMutex);
                    Peer& connected = peers[endpoint];
                    connected.endpoint = sender;
                    connected.actorId = packet.actorId;
                    connected.lastReceivedAt = GetTickCount64();
                    peerKnown.store(!peers.empty(), std::memory_order_release);
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
                if (packet.changedProperties == 0)
                {
                    continue;
                }
                const PlayerState update = Deserialize(packet);
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
                return SendHostRoster(now);
            }
            PlayerState state;
            bool shouldSend = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!hasOutbound)
                {
                    return true;
                }
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
            if (!shouldSend)
            {
                return true;
            }

            WirePlayerState packet = {};
            const auto acknowledged = [&]
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                const auto iterator = lastRemoteSequence.find(
                    outbound.actorId);
                return iterator == lastRemoteSequence.end()
                    ? 0u
                    : iterator->second;
            }();
            const std::size_t packetSize = Serialize(
                state, acknowledged, packet);
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(&packet),
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
            return true;
        }

        bool SendPacket(
            const PlayerState& state,
            const sockaddr_in& endpoint)
        {
            WirePlayerState packet = {};
            const std::size_t packetSize = Serialize(state, 0, packet);
            const int sent = sendto(
                socket,
                reinterpret_cast<const char*>(&packet),
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
                        continue;
                    }
                    ++peerIterator;
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
                "socket IO, acknowledgements, retransmission, and keepalive are network-owned");
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
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        implementation_ = std::make_unique<Implementation>();
        Implementation& implementation = *implementation_;
        implementation.diagnostics = diagnostics;
        implementation.role = PeerRole::Host;

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
        char detail[96] = {};
        std::snprintf(detail, sizeof(detail), "role=host port=%u", port);
        diagnostics.Event("MultiplayerTransportReady", detail);
        return true;
    }

    bool UdpPeer::StartGuest(
        const std::string& address,
        std::uint16_t port,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        implementation_ = std::make_unique<Implementation>();
        Implementation& implementation = *implementation_;
        implementation.diagnostics = diagnostics;
        implementation.role = PeerRole::Guest;

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
            "role=guest address=%s port=%u",
            address.c_str(),
            port);
        diagnostics.Event("MultiplayerTransportReady", detail);
        return true;
    }

    bool UdpPeer::Submit(const PlayerState& localUpdate)
    {
        if (!IsStarted() || HasFailed() || localUpdate.actorId == 0 ||
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
}
