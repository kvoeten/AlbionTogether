#include <WinSock2.h>

#include "Multiplayer/Protocol/AuthorityMessage.h"
#include "Multiplayer/Protocol/PlayerActorStateCodec.h"
#include "Multiplayer/Protocol/EntityMovementMessage.h"
#include "Multiplayer/Protocol/EntityLifecycleMessage.h"
#include "Multiplayer/Protocol/EntityVitalsMessageCodec.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerMovementCodec.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Combat/PlayerDeathPolicy.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/EntityVitalsReplication.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"
#include "Multiplayer/Replication/PlayerActionEventQueue.h"
#include "Multiplayer/Replication/PlayerActorLifecycleReducer.h"
#include "Multiplayer/Replication/PlayerActorLifecycleLimits.h"
#include "Multiplayer/Replication/PlayerActorStatePublicationQueue.h"
#include "Multiplayer/Replication/PlayerActorStateReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Runtime/RemotePlayerLifecycleInvalidation.h"
#include "Multiplayer/Transport/MovementTransport.h"
#include "Multiplayer/Transport/PeerDatagramCodec.h"
#include "Multiplayer/Transport/PeerSessionRegistry.h"
#include "Multiplayer/Transport/ReliableStreamTransport.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>

namespace fable::multiplayer::replication::testing
{
    void SetLocalHeroState(const PlayerState* state) noexcept;
}

extern "C" void ConfigureRemoteActionPresentationForTest(
    bool worldReady,
    const char* mapName,
    bool lifecycleReady) noexcept;
extern "C" std::uint32_t PerformedAbilityCountForTest() noexcept;
extern "C" std::uint32_t LastPerformedAnimationIdForTest() noexcept;

int RunCombatHitReplicationTests();
int RunCodePatchTests();
int RunSessionClockTests();
int RunReliableStreamWindowTests();
int RunEquipmentTransitionTimingTests();

namespace
{
    using fable::game::Vector3;
    using fable::multiplayer::PeerRole;
    using fable::multiplayer::PlayerState;
    using fable::multiplayer::player_property::Movement;
    using fable::multiplayer::protocol::EncodePlayerActorStateMessage;
    using fable::multiplayer::protocol::EncodePlayerMovementMessage;
    using fable::multiplayer::protocol::DecodePlayerActorStateMessage;
    using fable::multiplayer::protocol::DecodePlayerMovementMessage;
    using fable::multiplayer::protocol::PlayerActorStateMessage;
    using fable::multiplayer::protocol::PlayerActorStateOperation;
    using fable::multiplayer::protocol::PlayerActionKind;
    using fable::multiplayer::protocol::PlayerActionMessage;
    using fable::multiplayer::protocol::PlayerActionPhase;
    using fable::multiplayer::protocol::SessionTimeUnset;
    using fable::multiplayer::protocol::PlayerMovementMessage;
    using fable::multiplayer::protocol::player_actor_state_flag::AppearanceChanged;
    using fable::multiplayer::protocol::player_actor_state_flag::AppearancePresent;
    using fable::multiplayer::protocol::player_actor_state_flag::EquipmentChanged;
    using fable::multiplayer::protocol::player_actor_state_flag::EquipmentPresent;
    using fable::multiplayer::replication::RemotePlayerChannels;
    using fable::multiplayer::replication::PlayerActorLifecycleReducer;
    using fable::multiplayer::replication::PlayerActorLifecycleReduction;
    using fable::multiplayer::TransportMessage;
    using fable::multiplayer::UdpPeer;
    using fable::multiplayer::authority::MapPreparationRetryState;

    namespace actor_state_testing =
        fable::multiplayer::replication::testing;

    constexpr std::uint64_t kActorId = 42;
    constexpr std::uint32_t kAuthorityEpoch = 7;
    constexpr std::uint32_t kGeneration = 1;
    constexpr std::uint32_t kMapEpoch = 1;
    constexpr std::uint16_t kMapId = 100;

    int failures = 0;
    int staleVitalsEvents = 0;

    void CaptureTestEvent(const char* state, const char*)
    {
        if (state != nullptr &&
            std::strcmp(state, "MultiplayerEntityVitalsRejected") == 0)
        {
            ++staleVitalsEvents;
        }
    }

    void Check(bool condition, const char* expression, const char* test)
    {
        if (!condition)
        {
            std::cerr << test << ": failed: " << expression << '\n';
            ++failures;
        }
    }

#define CHECK(test, expression) Check((expression), #expression, (test))

    PlayerActorStateMessage Construct(
        std::uint32_t authorityEpoch = kAuthorityEpoch,
        std::uint32_t actorGeneration = kGeneration,
        std::uint32_t mapEpoch = kMapEpoch,
        std::uint32_t structuralRevision = 1,
        std::uint16_t mapId = kMapId,
        const char* mapName = "Albion",
        std::uint64_t actorId = kActorId)
    {
        PlayerActorStateMessage message;
        message.operation = PlayerActorStateOperation::Construct;
        message.componentFlags = AppearanceChanged | EquipmentChanged |
            AppearancePresent | EquipmentPresent;
        message.actorId = actorId;
        message.authorityEpoch = authorityEpoch;
        message.actorGeneration = actorGeneration;
        message.mapEpoch = mapEpoch;
        message.structuralRevision = structuralRevision;
        message.role = PeerRole::Guest;
        message.mapId = mapId;
        message.initialPosition = {1.0f, 2.0f, 3.0f};
        message.initialFacing = 0.25f;
        message.playerId = "player-test";
        message.mapName = mapName;
        message.appearanceDefinition = "hero.default";

        message.heroMorph.valid = true;
        message.heroMorph.strength = 0.2f;
        message.heroMorph.berserk = 0.3f;
        message.heroMorph.will = 0.4f;
        message.heroMorph.skill = 0.5f;
        message.heroMorph.age = 0.6f;
        message.heroMorph.alignment = 0.7f;
        message.heroMorph.fatness = 0.8f;
        message.heroMorph.auxiliary = 1.0f;
        message.heroClothing.valid = true;
        message.heroClothing.definitionIndices[0] = 1001;
        message.heroBoneScales.valid = true;
        message.heroBoneScales.count = 1;
        message.heroBoneScales.entries[0] = {3, 1.0f, 1.25f, 0.75f};
        message.heroAppearanceModifiers.valid = true;
        message.heroAppearanceModifiers.count = 1;
        message.heroAppearanceModifiers.definitionIndices[0] = 2001;

        message.heroEquipment.valid = true;
        message.heroEquipment.meleeDefinitionIndex = 3001;
        message.heroEquipment.meleeAttachmentSlot = 2;
        message.heroEquipment.activeFamily =
            fable::game::creature::equipment::CreatureWeaponFamily::Melee;
        message.heroEquipment.transitionActionId = 4001;
        return message;
    }

    PlayerState StateFromConstruct(const PlayerActorStateMessage& source)
    {
        PlayerState state;
        state.changedProperties = fable::multiplayer::player_property::All;
        state.authorityEpoch = source.authorityEpoch;
        state.actorGeneration = source.actorGeneration;
        state.mapEpoch = source.mapEpoch;
        state.actorId = source.actorId;
        state.role = source.role;
        state.position = source.initialPosition;
        state.facing = source.initialFacing;
        state.mapId = source.mapId;
        state.playerId = source.playerId;
        state.mapName = source.mapName;
        state.appearanceDefinition = source.appearanceDefinition;
        state.heroMorph = source.heroMorph;
        state.heroClothing = source.heroClothing;
        state.heroBoneScales = source.heroBoneScales;
        state.heroAppearanceModifiers = source.heroAppearanceModifiers;
        state.heroEquipment = source.heroEquipment;
        return state;
    }

    void PopulateAllBoneScales(PlayerActorStateMessage& message)
    {
        message.heroBoneScales.count =
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries;
        for (std::size_t index = 0;
             index < message.heroBoneScales.count; ++index)
        {
            message.heroBoneScales.entries[index] = {
                static_cast<std::uint16_t>(index), 1.0f, 1.25f, 0.75f};
        }
    }

    PlayerState MovementState(const PlayerMovementMessage& movement)
    {
        PlayerState state;
        state.actorId = movement.actorId;
        state.authorityEpoch = movement.authorityEpoch;
        state.actorGeneration = movement.actorGeneration;
        state.mapEpoch = movement.mapEpoch;
        state.mapId = movement.mapId;
        state.mapName = movement.mapEpoch == 2 ? "NewMap" : "Albion";
        state.position = movement.position;
        state.velocity = movement.velocity;
        state.facing = movement.facing;
        state.angularVelocity = movement.angularVelocity;
        state.moving = movement.moving;
        state.sequence = movement.sequence;
        state.changedProperties = fable::multiplayer::player_property::Movement;
        return state;
    }

    PlayerMovementMessage MakeMovement(
        std::uint32_t sequence = 1,
        std::uint32_t authorityEpoch = kAuthorityEpoch,
        std::uint32_t actorGeneration = kGeneration,
        std::uint32_t mapEpoch = kMapEpoch,
        std::uint16_t mapId = kMapId)
    {
        PlayerMovementMessage message;
        message.actorId = kActorId;
        message.authorityEpoch = authorityEpoch;
        message.actorGeneration = actorGeneration;
        message.mapEpoch = mapEpoch;
        message.sequence = sequence;
        message.mapId = mapId;
        message.moving = true;
        message.position = {4.0f, 5.0f, 6.0f};
        message.velocity = {1.0f, 0.0f, -1.0f};
        message.facing = 0.5f;
        message.angularVelocity = 0.25f;
        return message;
    }

    fable::multiplayer::protocol::EntityMovementMessage MakeEntityMovement(
        const std::uint64_t entityUid,
        const std::uint32_t sequence,
        const float x)
    {
        fable::multiplayer::protocol::EntityMovementMessage message;
        message.entityUid = entityUid;
        message.entityGeneration = 4;
        message.ownerActorId = 2002;
        message.mapEpoch = 3;
        message.sequence = sequence;
        message.mapName = "LookoutPoint";
        message.position = {x, 2.0f, 3.0f};
        message.velocity = {1.0f, 0.0f, 0.0f};
        message.facing = 0.25f;
        message.moving = true;
        return message;
    }

    TransportMessage EntityMovementTransportMessage(
        const fable::multiplayer::protocol::EntityMovementMessage& source,
        const std::uint64_t connectionNonce = 99)
    {
        TransportMessage message;
        message.type =
            fable::multiplayer::protocol::PacketType::EntityMovement;
        message.sourceActorId = source.ownerActorId;
        message.connectionNonce = connectionNonce;
        std::size_t payloadSize = 0;
        if (!fable::multiplayer::protocol::EncodeEntityMovementMessage(
                source,
                message.payload.data(),
                message.payload.size(),
                payloadSize))
        {
            return {};
        }
        message.payloadSize = payloadSize;
        return message;
    }

    bool RoundTripActor(
        const PlayerActorStateMessage& source,
        PlayerActorStateMessage& decoded)
    {
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        return EncodePlayerActorStateMessage(
                   source, bytes.data(), bytes.size(), encodedSize) &&
            DecodePlayerActorStateMessage(bytes.data(), encodedSize, decoded);
    }

    bool RoundTripMovement(
        const PlayerMovementMessage& source,
        PlayerMovementMessage& decoded)
    {
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        return EncodePlayerMovementMessage(
                   source, bytes.data(), bytes.size(), encodedSize) &&
            DecodePlayerMovementMessage(bytes.data(), encodedSize, decoded);
    }

    bool EncodeActorPayload(
        const PlayerActorStateMessage& source,
        std::array<std::uint8_t, 1472>& bytes,
        std::size_t& encodedSize)
    {
        return EncodePlayerActorStateMessage(
            source, bytes.data(), bytes.size(), encodedSize);
    }

    bool WaitFor(
        const std::function<bool()>& predicate,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return predicate();
    }

    fable::core::Diagnostics TestDiagnostics()
    {
        return {};
    }

    fable::core::Diagnostics CapturingDiagnostics()
    {
        fable::core::Diagnostics diagnostics;
        diagnostics.event = &CaptureTestEvent;
        return diagnostics;
    }

    bool StartTransportPair(UdpPeer& host, UdpPeer& guest, std::uint16_t port)
    {
        return host.StartHost(port, 1001, TestDiagnostics()) &&
            guest.StartGuest("127.0.0.1", port, 2002, TestDiagnostics()) &&
            WaitFor([&host] { return host.HasPeer(); });
    }

    sockaddr_in LoopbackEndpoint(const std::uint16_t port)
    {
        sockaddr_in endpoint = {};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return endpoint;
    }

    bool SendDatagram(
        const SOCKET socketHandle,
        const sockaddr_in& destination,
        const std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& bytes,
        const std::size_t size)
    {
        return sendto(
            socketHandle,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(size),
            0,
            reinterpret_cast<const sockaddr*>(&destination),
            sizeof(destination)) == static_cast<int>(size);
    }

    SOCKET OpenRawUdpSocket(
        const std::uint16_t port = 0,
        const DWORD receiveTimeoutMilliseconds = 100)
    {
        const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }
        const sockaddr_in endpoint = LoopbackEndpoint(port);
        if (bind(
                socketHandle,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint)) != 0 ||
            setsockopt(
                socketHandle,
                SOL_SOCKET,
                SO_RCVTIMEO,
                reinterpret_cast<const char*>(&receiveTimeoutMilliseconds),
                sizeof(receiveTimeoutMilliseconds)) != 0)
        {
            closesocket(socketHandle);
            return INVALID_SOCKET;
        }
        return socketHandle;
    }

    class UdpBlackholeProxy final
    {
    public:
        ~UdpBlackholeProxy()
        {
            Shutdown();
        }

        bool Start(
            const std::uint16_t proxyPort,
            const std::uint16_t hostPort)
        {
            Shutdown();
            WSADATA winsock = {};
            if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
            {
                return false;
            }
            winsockStarted_ = true;
            socket_ = OpenRawUdpSocket(proxyPort, 50);
            if (socket_ == INVALID_SOCKET)
            {
                Shutdown();
                return false;
            }
            host_ = LoopbackEndpoint(hostPort);
            forwarding_.store(true, std::memory_order_release);
            running_.store(true, std::memory_order_release);
            worker_ = std::thread([this] { Run(); });
            return true;
        }

        void SetForwarding(const bool forwarding) noexcept
        {
            forwarding_.store(forwarding, std::memory_order_release);
        }

        void Shutdown() noexcept
        {
            running_.store(false, std::memory_order_release);
            if (worker_.joinable())
            {
                worker_.join();
            }
            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
            }
            if (winsockStarted_)
            {
                WSACleanup();
                winsockStarted_ = false;
            }
        }

    private:
        void Run() noexcept
        {
            sockaddr_in guest = {};
            bool guestKnown = false;
            std::array<std::uint8_t,
                fable::multiplayer::protocol::MaximumDatagramBytes>
                datagram = {};
            while (running_.load(std::memory_order_acquire))
            {
                sockaddr_in sender = {};
                int senderSize = sizeof(sender);
                const int byteCount = recvfrom(
                    socket_,
                    reinterpret_cast<char*>(datagram.data()),
                    static_cast<int>(datagram.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&sender),
                    &senderSize);
                if (byteCount <= 0)
                {
                    continue;
                }
                const bool fromHost =
                    sender.sin_addr.s_addr == host_.sin_addr.s_addr &&
                    sender.sin_port == host_.sin_port;
                sockaddr_in destination = {};
                if (fromHost)
                {
                    if (!guestKnown)
                    {
                        continue;
                    }
                    destination = guest;
                }
                else
                {
                    guest = sender;
                    guestKnown = true;
                    destination = host_;
                }
                if (!forwarding_.load(std::memory_order_acquire))
                {
                    continue;
                }
                (void)sendto(
                    socket_,
                    reinterpret_cast<const char*>(datagram.data()),
                    byteCount,
                    0,
                    reinterpret_cast<const sockaddr*>(&destination),
                    sizeof(destination));
            }
        }

        SOCKET socket_ = INVALID_SOCKET;
        sockaddr_in host_ = {};
        std::thread worker_;
        std::atomic_bool running_{false};
        std::atomic_bool forwarding_{true};
        bool winsockStarted_ = false;
    };

    bool ReceiveRawPacket(
        const SOCKET socketHandle,
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>& datagram,
        sockaddr_in& sender,
        fable::multiplayer::protocol::PacketView& packet,
        const std::chrono::milliseconds timeout =
            std::chrono::milliseconds(2'000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            int senderSize = sizeof(sender);
            const int byteCount = recvfrom(
                socketHandle,
                reinterpret_cast<char*>(datagram.data()),
                static_cast<int>(datagram.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderSize);
            if (byteCount > 0 &&
                fable::multiplayer::protocol::DecodePacket(
                    datagram.data(),
                    static_cast<std::size_t>(byteCount),
                    packet))
            {
                return true;
            }
            if (byteCount == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK &&
                    error != WSAECONNRESET)
                {
                    return false;
                }
            }
        }
        return false;
    }

    bool CompleteRawGuestHandshake(
        const SOCKET socketHandle,
        const sockaddr_in& hostEndpoint,
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        std::uint64_t& hostConnectionNonce)
    {
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> datagram = {};
        std::size_t datagramSize = 0;
        if (!fable::multiplayer::transport_codec::EncodePeerHello(
                actorId,
                connectionNonce,
                nullptr,
                0,
                datagram,
                datagramSize) ||
            !SendDatagram(
                socketHandle, hostEndpoint, datagram, datagramSize))
        {
            return false;
        }

        sockaddr_in sender = {};
        fable::multiplayer::protocol::PacketView packet;
        if (!ReceiveRawPacket(socketHandle, datagram, sender, packet) ||
            packet.envelope.type !=
                fable::multiplayer::protocol::PacketType::PeerHello ||
            packet.payloadSize !=
                fable::multiplayer::PeerSessionRegistry::ChallengeBytes ||
            packet.envelope.sourceActorId == 0 ||
            packet.envelope.connectionNonce == 0)
        {
            return false;
        }
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            challenge = {};
        std::memcpy(
            challenge.data(), packet.payload, challenge.size());
        std::uint64_t echoedNonce = 0;
        std::memcpy(
            &echoedNonce, challenge.data(), sizeof(echoedNonce));
        if (echoedNonce != connectionNonce)
        {
            return false;
        }
        hostConnectionNonce = packet.envelope.connectionNonce;
        return fable::multiplayer::transport_codec::EncodePeerHello(
                   actorId,
                   connectionNonce,
                   challenge.data(),
                   challenge.size(),
                   datagram,
                   datagramSize) &&
            SendDatagram(
                socketHandle, hostEndpoint, datagram, datagramSize);
    }

    bool ReceiveAndAcknowledgeRawActorStreamThrough(
        const SOCKET socketHandle,
        const std::uint64_t rawActorId,
        const std::uint64_t rawConnectionNonce,
        const std::uint64_t hostSourceActorId,
        const std::uint64_t streamActorId,
        const std::uint64_t hostConnectionNonce,
        const std::uint32_t targetSequence,
        std::uint32_t& highestSequence,
        const std::chrono::milliseconds timeout =
            std::chrono::milliseconds(5'000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> datagram = {};
        while (highestSequence < targetSequence &&
            std::chrono::steady_clock::now() < deadline)
        {
            sockaddr_in sender = {};
            fable::multiplayer::protocol::PacketView packet;
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
            if (!ReceiveRawPacket(
                    socketHandle, datagram, sender, packet, remaining))
            {
                break;
            }
            const bool actorStateDatagram =
                packet.envelope.type ==
                    fable::multiplayer::protocol::PacketType::
                        PlayerActorState ||
                packet.envelope.type ==
                    fable::multiplayer::protocol::PacketType::
                        ReliableFragment;
            if (!actorStateDatagram ||
                packet.envelope.flags !=
                    fable::multiplayer::protocol::packet_flag::Reliable ||
                packet.envelope.sourceActorId != hostSourceActorId ||
                packet.envelope.connectionNonce != hostConnectionNonce ||
                packet.envelope.streamKind != static_cast<std::uint8_t>(
                    fable::multiplayer::ReliableStreamKind::Actor) ||
                packet.envelope.streamId != streamActorId ||
                packet.envelope.streamIncarnation == 0 ||
                packet.envelope.sequence == 0)
            {
                continue;
            }

            std::array<std::uint8_t,
                fable::multiplayer::protocol::MaximumDatagramBytes>
                acknowledgement = {};
            std::size_t acknowledgementSize = 0;
            if (!fable::multiplayer::transport_codec::EncodeAcknowledgement(
                    rawActorId,
                    rawConnectionNonce,
                    fable::multiplayer::reliable_stream::Actor(streamActorId),
                    packet.envelope.streamIncarnation,
                    packet.envelope.sequence,
                    acknowledgement,
                    acknowledgementSize) ||
                !SendDatagram(
                    socketHandle,
                    sender,
                    acknowledgement,
                    acknowledgementSize))
            {
                return false;
            }
            if (packet.envelope.sequence == highestSequence + 1)
            {
                highestSequence = packet.envelope.sequence;
            }
            else if (packet.envelope.sequence > highestSequence + 1)
            {
                return false;
            }
        }
        return highestSequence >= targetSequence;
    }

    bool ReceiveAndAcknowledgeRawActorThrough(
        const SOCKET socketHandle,
        const std::uint64_t rawActorId,
        const std::uint64_t rawConnectionNonce,
        const std::uint64_t hostActorId,
        const std::uint64_t hostConnectionNonce,
        const std::uint32_t targetSequence,
        std::uint32_t& highestSequence,
        const std::chrono::milliseconds timeout =
            std::chrono::milliseconds(5'000))
    {
        return ReceiveAndAcknowledgeRawActorStreamThrough(
            socketHandle,
            rawActorId,
            rawConnectionNonce,
            hostActorId,
            hostActorId,
            hostConnectionNonce,
            targetSequence,
            highestSequence,
            timeout);
    }

    void TestReliableActorTransportLifecycle()
    {
        constexpr const char* test = "reliable actor transport lifecycle";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39173))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        const auto construct = Construct();
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(construct, bytes, payloadSize));
        CHECK(test, guest.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(kActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            bytes.data(), payloadSize));

        // Leave the receive queue untouched across at least one resend period.
        // The transport must acknowledge/deduplicate at the network boundary,
        // so the application still receives one ordered baseline.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        TransportMessage received;
        CHECK(test, WaitFor([&host, &received]
        {
            return host.TryConsumeReliable(received);
        }));
        CHECK(test, received.type ==
            fable::multiplayer::protocol::PacketType::PlayerActorState);
        CHECK(test, received.streamId ==
            fable::multiplayer::reliable_stream::Actor(kActorId));
        PlayerActorStateMessage decoded;
        CHECK(test, DecodePlayerActorStateMessage(
            received.payload.data(), received.payloadSize, decoded));
        CHECK(test, decoded.actorId == kActorId);
        CHECK(test, received.sequence == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        CHECK(test, !host.TryConsumeReliable(received));

        // The reverse direction uses its own sequence space and must remain
        // ordered after the guest baseline has been acknowledged.
        CHECK(test, host.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(kActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            bytes.data(), payloadSize));
        CHECK(test, WaitFor([&guest, &received]
        {
            return guest.TryConsumeReliable(received);
        }));
        CHECK(test, received.sourceActorId == 1001);
        CHECK(test, received.streamId ==
            fable::multiplayer::reliable_stream::Actor(kActorId));
        CHECK(test, received.sequence == 1);

        host.Shutdown();
        guest.Shutdown();
    }

    void TestGuestMovementKeepaliveUsesLastValidSnapshot()
    {
        constexpr const char* test = "guest movement keepalive";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39207))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        PlayerState movement = MovementState(MakeMovement());
        movement.actorId = 2002;
        CHECK(test, guest.Submit(movement));
        PlayerState received;
        CHECK(test, WaitFor([&host, &received]
        {
            return host.TryConsume(received);
        }));
        CHECK(test, received.actorId == movement.actorId);
        CHECK(test, received.sequence == movement.sequence);

        // Dirty movement is cleared after the first send. The one-second
        // lease keepalive must still encode the retained transform as a valid
        // Movement packet instead of failing the network worker.
        std::this_thread::sleep_for(std::chrono::milliseconds(1'250));
        CHECK(test, !guest.HasFailed());
        CHECK(test, !host.HasFailed());
        CHECK(test, host.HasPeer());

        ++movement.sequence;
        movement.position.x += 1.0f;
        CHECK(test, guest.Submit(movement));
        CHECK(test, WaitFor([&host, &received, &movement]
        {
            return host.TryConsume(received) &&
                received.sequence == movement.sequence;
        }));

        guest.Shutdown();
        host.Shutdown();
    }

    void TestReliableControlIncarnationAndAcknowledgementRoundTrip()
    {
        constexpr const char* test = "reliable control incarnation";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39206))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        fable::multiplayer::protocol::AuthorityMessage authority;
        authority.operation =
            fable::multiplayer::protocol::AuthorityOperation::Request;
        authority.scope =
            fable::multiplayer::protocol::AuthorityScope::MapSimulation;
        authority.ownerActorId = 2002;
        authority.mapId = kMapId;
        authority.mapName = "Albion";
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        CHECK(test,
            fable::multiplayer::protocol::EncodeAuthorityMessage(
                authority,
                payload.data(),
                payload.size(),
                payloadSize));

        CHECK(test, guest.SubmitReliable(
            fable::multiplayer::reliable_stream::Control,
            fable::multiplayer::protocol::PacketType::Authority,
            payload.data(),
            payloadSize));
        TransportMessage first;
        CHECK(test, WaitFor([&host, &first]
        {
            return host.TryConsumeReliable(first);
        }));
        CHECK(test, first.type ==
            fable::multiplayer::protocol::PacketType::Authority);
        CHECK(test, first.streamId ==
            fable::multiplayer::reliable_stream::Control);
        CHECK(test, first.streamIncarnation != 0);
        CHECK(test, first.sequence == 1);
        CHECK(test, first.connectionNonce == guest.ConnectionNonce());
        fable::multiplayer::protocol::AuthorityMessage decoded;
        CHECK(test,
            fable::multiplayer::protocol::DecodeAuthorityMessage(
                first.payload.data(),
                first.payloadSize,
                decoded));
        CHECK(test, decoded.operation == authority.operation);
        CHECK(test, decoded.ownerActorId == authority.ownerActorId);
        CHECK(test, decoded.mapName == authority.mapName);

        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes>
            acknowledgement = {};
        std::size_t acknowledgementSize = 0;
        CHECK(test,
            fable::multiplayer::transport_codec::EncodeAcknowledgement(
                1001,
                host.ConnectionNonce(),
                fable::multiplayer::reliable_stream::Control,
                first.streamIncarnation,
                first.sequence,
                acknowledgement,
                acknowledgementSize));
        fable::multiplayer::protocol::PacketView decodedAcknowledgement;
        CHECK(test, fable::multiplayer::protocol::DecodePacket(
            acknowledgement.data(),
            acknowledgementSize,
            decodedAcknowledgement));
        CHECK(test, decodedAcknowledgement.envelope.type ==
            fable::multiplayer::protocol::PacketType::Acknowledgement);
        CHECK(test, decodedAcknowledgement.envelope.streamKind ==
            static_cast<std::uint8_t>(
                fable::multiplayer::ReliableStreamKind::Control));
        CHECK(test, decodedAcknowledgement.envelope.streamId == 0);
        CHECK(test, decodedAcknowledgement.envelope.streamIncarnation ==
            first.streamIncarnation);
        CHECK(test, decodedAcknowledgement.envelope.streamIncarnation != 0);
        CHECK(test, decodedAcknowledgement.envelope.sequence ==
            first.sequence);

        // The next packet cannot reach the host until its transport has
        // accepted the ACK for sequence one. Depending on whether cleanup ran
        // before this submission, it is either sequence two in the active
        // incarnation or sequence one in the next monotonic incarnation.
        CHECK(test, guest.SubmitReliable(
            fable::multiplayer::reliable_stream::Control,
            fable::multiplayer::protocol::PacketType::Authority,
            payload.data(),
            payloadSize));
        TransportMessage second;
        CHECK(test, WaitFor([&host, &second]
        {
            return host.TryConsumeReliable(second);
        }));
        CHECK(test, second.streamId ==
            fable::multiplayer::reliable_stream::Control);
        CHECK(test, second.streamIncarnation != 0);
        CHECK(test,
            (second.streamIncarnation == first.streamIncarnation &&
                second.sequence == 2) ||
            (second.streamIncarnation > first.streamIncarnation &&
                second.sequence == 1));

        guest.Shutdown();
        host.Shutdown();
    }

    TransportMessage MakeReliableTransportMessage(
        const PlayerActorStateMessage& source,
        std::uint64_t connectionNonce,
        std::uint32_t sequence);

    void TestReliableReconnectRequiresFreshBaseline()
    {
        constexpr const char* test = "reliable reconnect baseline";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39174))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        const auto first = Construct(kAuthorityEpoch, 1, 1, 1);
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(first, bytes, payloadSize));
        CHECK(test, guest.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(kActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            bytes.data(), payloadSize));
        TransportMessage received;
        CHECK(test, WaitFor([&host, &received]
        {
            return host.TryConsumeReliable(received);
        }));
        const std::uint64_t firstPeerRevision = host.PeerSetRevision();
        (void)firstPeerRevision;
        const std::uint64_t firstConnectionNonce = guest.ConnectionNonce();
        guest.Shutdown();
        UdpPeer replacement;
        CHECK(test, replacement.StartGuest(
            "127.0.0.1", 39174, 2002, TestDiagnostics()));
        const std::uint64_t replacementConnectionNonce =
            replacement.ConnectionNonce();
        CHECK(test, replacementConnectionNonce != 0);
        CHECK(test, replacementConnectionNonce != firstConnectionNonce);

        const auto replacementBaseline = Construct(
            kAuthorityEpoch, 2, 2, 1, 200, "NewMap");
        received = MakeReliableTransportMessage(
            replacementBaseline, replacementConnectionNonce, 1);
        PlayerActorStateMessage decoded;
        CHECK(test, DecodePlayerActorStateMessage(
            received.payload.data(), received.payloadSize, decoded));
        CHECK(test, decoded.actorGeneration == 2);
        CHECK(test, decoded.mapEpoch == 2);
        CHECK(test, decoded.mapName == "NewMap");
        CHECK(test, received.sequence == 1);

        replacement.Shutdown();
        host.Shutdown();
    }

    bool SubmitActor(
        UdpPeer& peer,
        const PlayerActorStateMessage& message)
    {
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t payloadSize = 0;
        return EncodeActorPayload(message, bytes, payloadSize) &&
            peer.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(message.actorId),
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                bytes.data(), payloadSize);
    }

    bool ReceiveReliable(
        UdpPeer& peer,
        TransportMessage& message)
    {
        return WaitFor([&peer, &message]
        {
            return peer.TryConsumeReliable(message);
        });
    }

    TransportMessage MakeReliableTransportMessage(
        const PlayerActorStateMessage& source,
        const std::uint64_t connectionNonce,
        const std::uint32_t sequence)
    {
        TransportMessage result;
        result.type =
            fable::multiplayer::protocol::PacketType::PlayerActorState;
        result.sourceActorId = source.actorId;
        result.connectionNonce = connectionNonce;
        result.streamId =
            fable::multiplayer::reliable_stream::Actor(source.actorId);
        result.streamIncarnation = connectionNonce != 0
            ? connectionNonce
            : 1;
        result.sequence = sequence;
        std::array<std::uint8_t, 1472> encodedBytes = {};
        std::size_t payloadSize = 0;
        const bool encoded = EncodeActorPayload(
            source, encodedBytes, payloadSize);
        if (encoded)
        {
            std::memcpy(
                result.payload.data(), encodedBytes.data(), payloadSize);
        }
        result.payloadSize = encoded ? payloadSize : 0;
        return result;
    }

    bool MakeFirstActorReliableDatagram(
        const PlayerActorStateMessage& source,
        TransportMessage& result)
    {
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        fable::multiplayer::ReliableStreamTransport transport;
        if (!EncodeActorPayload(source, payload, payloadSize) ||
            !transport.Enqueue(
                fable::multiplayer::reliable_stream::Actor(source.actorId),
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                source.actorId,
                payload.data(),
                payloadSize))
        {
            return false;
        }
        const auto due = transport.Due(1, 100);
        if (due.size() != 1)
        {
            return false;
        }
        result = due.front();
        return result.type ==
                fable::multiplayer::protocol::PacketType::ReliableFragment ||
            result.type ==
                fable::multiplayer::protocol::PacketType::PlayerActorState;
    }

    TransportMessage MakePlayerActionTransportMessage(
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        const std::uint64_t actionId,
        const bool intent = false,
        const std::uint32_t actorGeneration = 1,
        const std::uint32_t mapEpoch = 1,
        const char* const mapName = "Albion")
    {
        fable::multiplayer::protocol::PlayerActionMessage action;
        action.phase = intent
            ? fable::multiplayer::protocol::PlayerActionPhase::Intent
            : fable::multiplayer::protocol::PlayerActionPhase::Perform;
        action.kind =
            fable::multiplayer::protocol::PlayerActionKind::AbilityRequest;
        action.ownerActorId = actorId;
        action.actionId = actionId;
        action.authorityEpoch = 700;
        action.actorGeneration = actorGeneration;
        action.mapEpoch = mapEpoch;
        action.abilityId = 1;
        action.resolvedAnimationId = static_cast<std::uint32_t>(actionId);
        action.mapName = mapName;
        action.semanticName = "Attack";

        TransportMessage message;
        message.type =
            fable::multiplayer::protocol::PacketType::PlayerAction;
        message.sourceActorId = intent ? actorId : 1001;
        message.connectionNonce = connectionNonce;
        message.streamId =
            fable::multiplayer::reliable_stream::Actor(actorId);
        message.sequence = static_cast<std::uint32_t>(actionId);
        std::size_t payloadSize = 0;
        if (!fable::multiplayer::protocol::EncodePlayerActionMessage(
                action,
                message.payload.data(),
                message.payload.size(),
                payloadSize))
        {
            return {};
        }
        message.payloadSize = payloadSize;
        return message;
    }

    void TestConstructionRetransmissionOrdersBaselineBeforeAction()
    {
        constexpr const char* test =
            "construct retransmission precedes action";
        constexpr std::uint64_t actorId = 3000;
        const auto construct = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorId);
        std::array<std::uint8_t, 1472> constructPayload = {};
        std::size_t constructPayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            construct, constructPayload, constructPayloadSize));
        const TransportMessage action = MakePlayerActionTransportMessage(
            actorId, 999, 1, true);

        fable::multiplayer::ReliableStreamTransport sender;
        fable::multiplayer::ReliableStreamTransport receiver;
        const auto stream =
            fable::multiplayer::reliable_stream::Actor(actorId);
        CHECK(test, sender.Enqueue(
            stream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            actorId,
            constructPayload.data(),
            constructPayloadSize));
        CHECK(test, sender.Enqueue(
            stream,
            action.type,
            actorId,
            action.payload.data(),
            action.payloadSize));

        // Drop the first window completely. The bounded sender may pipeline
        // both baseline fragments and the following action, but receiver-side
        // stream order must still materialize the complete Construct first.
        const auto dropped = sender.Due(100, 100);
        CHECK(test, dropped.size() >= 2);
        CHECK(test, dropped.size() <=
            fable::multiplayer::ReliableStreamTransport::ReliableWindowSize);
        CHECK(test, !dropped.empty() &&
            (dropped.front().type ==
                fable::multiplayer::protocol::PacketType::PlayerActorState ||
                dropped.front().type ==
                fable::multiplayer::protocol::PacketType::ReliableFragment));
        CHECK(test, sender.Due(199, 100).empty());
        const auto retry = sender.Due(200, 100);
        CHECK(test, retry.size() == dropped.size());
        if (retry.empty())
        {
            return;
        }
        for (const auto& message : retry)
        {
            CHECK(test, receiver.AcceptIncoming(message) ==
                fable::multiplayer::ReliableReceiveResult::Accepted);
            CHECK(test, sender.AcceptAcknowledgement(
                stream, message.streamIncarnation, message.sequence));
        }
        CHECK(test, sender.Due(201, 100).empty());
        TransportMessage consumed;
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.type ==
            fable::multiplayer::protocol::PacketType::PlayerActorState);
        PlayerActorStateMessage receivedConstruct;
        CHECK(test, DecodePlayerActorStateMessage(
            consumed.payload.data(),
            consumed.payloadSize,
            receivedConstruct));
        CHECK(test, receivedConstruct.EquipmentPresent());
        CHECK(test, receivedConstruct.heroEquipment.Equals(
            construct.heroEquipment));
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.type ==
            fable::multiplayer::protocol::PacketType::PlayerAction);
    }

    void TestLargeActorBaselineFragmentsReliably()
    {
        constexpr const char* test = "large actor baseline fragmentation";
        auto construct = Construct();
        PopulateAllBoneScales(construct);
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(construct, payload, payloadSize));
        CHECK(test, payloadSize >
            fable::multiplayer::protocol::MaximumPayloadBytes());

        fable::multiplayer::ReliableStreamTransport sender;
        fable::multiplayer::ReliableStreamTransport receiver;
        const auto stream = fable::multiplayer::reliable_stream::Actor(kActorId);
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumReliableMessageBytes + 1>
            oversized = {};
        CHECK(test, !sender.Enqueue(
            stream,
            fable::multiplayer::protocol::PacketType::PlayerAction,
            kActorId,
            oversized.data(),
            oversized.size()));
        CHECK(test, sender.Enqueue(
            stream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            kActorId,
            payload.data(),
            payloadSize));

        const auto first = sender.Due(100, 100);
        CHECK(test, first.size() ==
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        CHECK(test, !first.empty() && first.front().type ==
            fable::multiplayer::protocol::PacketType::ReliableFragment);
        if (first.size() < 2)
        {
            return;
        }
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> datagram = {};
        std::size_t datagramSize = 0;
        CHECK(test, fable::multiplayer::transport_codec::EncodeReliablePacket(
            first.front(), kActorId, 1, datagram, datagramSize));
        CHECK(test, datagramSize <=
            fable::multiplayer::protocol::MaximumDatagramBytes);

        fable::multiplayer::ReliableStreamTransport malformedReceiver;
        TransportMessage malformed = first.front();
        malformed.payload[0] ^= 0xFFu;
        CHECK(test, malformedReceiver.AcceptIncoming(std::move(malformed)) ==
            fable::multiplayer::ReliableReceiveResult::Rejected);
        // A rejected fragment must not advance the ordered stream cursor.
        CHECK(test, malformedReceiver.AcceptIncoming(first.front()) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        // A duplicate is harmless and re-acknowledgeable after a lost ACK.
        CHECK(test, receiver.AcceptIncoming(first.front()) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        CHECK(test, receiver.AcceptIncoming(first.front()) ==
            fable::multiplayer::ReliableReceiveResult::Duplicate);
        const auto retry = sender.Due(200, 100);
        CHECK(test, retry.size() == first.size());
        CHECK(test, !retry.empty() && receiver.AcceptIncoming(retry.front()) ==
            fable::multiplayer::ReliableReceiveResult::Duplicate);
        fable::multiplayer::ReliableStreamTransport reordered;
        CHECK(test, reordered.AcceptIncoming(first[1]) ==
            fable::multiplayer::ReliableReceiveResult::Rejected);
        CHECK(test, reordered.AcceptIncoming(first[0]) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        CHECK(test, reordered.AcceptIncoming(first[1]) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        CHECK(test, receiver.AcceptIncoming(first[1]) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        for (const auto& fragment : first)
        {
            CHECK(test, sender.AcceptAcknowledgement(
                stream, fragment.streamIncarnation, fragment.sequence));
        }
        CHECK(test, sender.Due(201, 100).empty());

        TransportMessage complete;
        CHECK(test, receiver.TryConsume(complete));
        CHECK(test, complete.type ==
            fable::multiplayer::protocol::PacketType::PlayerActorState);
        CHECK(test, complete.payloadSize == payloadSize);
        CHECK(test, std::memcmp(
            complete.payload.data(), payload.data(), payloadSize) == 0);
        PlayerActorStateMessage decoded;
        CHECK(test, DecodePlayerActorStateMessage(
            complete.payload.data(), complete.payloadSize, decoded));
        CHECK(test, decoded.actorId == kActorId);
    }

    TransportMessage MakePlayerVitalsTransportMessage(
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        const std::uint32_t revision,
        const float health,
        const bool authoredByPlayer = false)
    {
        fable::multiplayer::protocol::EntityVitalsMessage vitals;
        vitals.subject =
            fable::multiplayer::protocol::EntityVitalsSubject::Player;
        vitals.playerActorId = actorId;
        vitals.ownerActorId = actorId;
        vitals.playerAuthorityEpoch = 700;
        vitals.playerActorGeneration = 1;
        vitals.playerMapEpoch = 1;
        vitals.revision = revision;
        vitals.currentHealth = health;
        vitals.maximumHealth = 100.0f;

        TransportMessage message;
        message.type =
            fable::multiplayer::protocol::PacketType::EntityVitals;
        message.sourceActorId = authoredByPlayer ? actorId : 1001;
        message.connectionNonce = connectionNonce;
        message.streamId =
            fable::multiplayer::reliable_stream::Actor(actorId);
        message.sequence = revision;
        std::size_t payloadSize = 0;
        if (!fable::multiplayer::protocol::EncodeEntityVitalsMessage(
                vitals,
                message.payload.data(),
                message.payload.size(),
                payloadSize))
        {
            return {};
        }
        message.payloadSize = payloadSize;
        return message;
    }

    void TestActorStateServiceFencingAndReadiness()
    {
        constexpr const char* test = "actor state service fencing/readiness";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39175))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        fable::multiplayer::replication::LocalHeroReplication hostHero;
        fable::multiplayer::replication::LocalHeroReplication guestHero;
        RemotePlayerChannels hostChannels;
        RemotePlayerChannels guestChannels;
        fable::multiplayer::replication::PlayerActorStateReplication hostService;
        fable::multiplayer::replication::PlayerActorStateReplication guestService;
        hostService.Initialize(
            fable::multiplayer::PeerRole::Host,
            1001,
            host,
            700,
            hostHero,
            hostChannels,
            TestDiagnostics());
        guestService.Initialize(
            fable::multiplayer::PeerRole::Guest,
            2002,
            guest,
            700,
            guestHero,
            guestChannels,
            TestDiagnostics());

        auto first = Construct(700, 1, 1, 1);
        first.actorId = 2002;
        PlayerState localState = StateFromConstruct(first);
        actor_state_testing::SetLocalHeroState(&localState);
        CHECK(test, guestService.Process());
        TransportMessage incoming;
        CHECK(test, ReceiveReliable(host, incoming));
        const std::uint64_t firstConnectionNonce = incoming.connectionNonce;
        CHECK(test, firstConnectionNonce != 0);
        CHECK(test, hostService.HandleReliableMessage(incoming));
        CHECK(test, hostService.IsLifecycleActive(2002, 1, 1));
        CHECK(test, hostChannels.IsLifecycleActive(2002, 1, 1));
        CHECK(test, hostChannels.IsAppearanceReady(2002, 1, 1));
        CHECK(test, hostChannels.IsEquipmentReady(2002, 1, 1));

        TransportMessage authoritative;
        CHECK(test, ReceiveReliable(guest, authoritative));
        CHECK(test, guestService.HandleReliableMessage(authoritative));
        CHECK(test, guestService.IsLifecycleActive(2002, 1, 1));

        // A structurally incomplete construct is rejected before it can
        // become an actor channel, which is the readiness prerequisite for
        // replaying actions or applying player vitals.
        auto incomplete = Construct(700, 9, 1, 1);
        incomplete.actorId = 2010;
        incomplete.componentFlags &= ~(
            fable::multiplayer::protocol::player_actor_state_flag::
                AppearancePresent |
            fable::multiplayer::protocol::player_actor_state_flag::
                EquipmentPresent);
        std::array<std::uint8_t, 1472> incompleteBytes = {};
        std::size_t incompleteSize = 0;
        CHECK(test, !EncodeActorPayload(
            incomplete, incompleteBytes, incompleteSize));
        CHECK(test, incompleteSize == 0);
        CHECK(test, !hostService.IsLifecycleActive(2010));
        CHECK(test, !hostChannels.IsLifecycleActive(2010, 9, 1));

        // A reconnect with the same stable actor ID gets a new transport
        // session and must publish a complete replacement Construct.
        const std::uint64_t firstHostPeerRevision = host.PeerSetRevision();
        guest.Shutdown();
        localState.heroMorph.strength = 0.91f;
        CHECK(test, guest.StartGuest(
            "127.0.0.1", 39175, 2002, TestDiagnostics()));
        CHECK(test, WaitFor([&host, firstHostPeerRevision]
        {
            return host.PeerSetRevision() != firstHostPeerRevision &&
                host.ConnectedPeerCount() != 0;
        }, std::chrono::milliseconds(15'000)));
        const std::uint64_t secondConnectionNonce = guest.ConnectionNonce();
        CHECK(test, secondConnectionNonce != 0);
        CHECK(test, secondConnectionNonce != firstConnectionNonce);
        CHECK(test, guestService.Process());
        CHECK(test, ReceiveReliable(host, incoming));
        CHECK(test, incoming.connectionNonce == secondConnectionNonce);
        PlayerActorStateMessage second;
        CHECK(test, DecodePlayerActorStateMessage(
            incoming.payload.data(), incoming.payloadSize, second));
        CHECK(test, second.operation == PlayerActorStateOperation::Construct);
        CHECK(test, second.actorGeneration == 1);
        CHECK(test, second.mapEpoch == 1);
        CHECK(test, (second.componentFlags & (
            AppearanceChanged | EquipmentChanged |
            AppearancePresent | EquipmentPresent)) == (
                AppearanceChanged | EquipmentChanged |
                AppearancePresent | EquipmentPresent));
        CHECK(test, second.heroMorph.strength == localState.heroMorph.strength);
        CHECK(test, hostService.HandleReliableMessage(incoming));
        CHECK(test, hostService.IsLifecycleActive(2002, 1, 1));
        CHECK(test, hostChannels.IsLifecycleActive(2002, 1, 1));
        CHECK(test, hostChannels.FindLifecycle(2002)->connectionNonce ==
            secondConnectionNonce);
        CHECK(test, hostChannels.Find(2002)->heroMorph.strength ==
            localState.heroMorph.strength);

        // A delayed packet from the retired transport session cannot replace
        // the new incarnation, even if it arrives after the replacement.
        auto stale = second;
        stale.operation =
            fable::multiplayer::protocol::PlayerActorStateOperation::ComponentDelta;
        stale.componentFlags =
            fable::multiplayer::protocol::player_actor_state_flag::
                AppearanceChanged |
            fable::multiplayer::protocol::player_actor_state_flag::
                AppearancePresent;
        stale.structuralRevision = 99;
        stale.constructionSnapshotTimeMs =
            fable::multiplayer::protocol::SessionTimeUnset;
        stale.componentPatchEffectiveTimeMs =
            fable::multiplayer::protocol::ToSessionTime(99);
        std::array<std::uint8_t, 1472> staleBytes = {};
        std::size_t staleSize = 0;
        CHECK(test, EncodeActorPayload(stale, staleBytes, staleSize));
        TransportMessage staleTransport;
        staleTransport.type =
            fable::multiplayer::protocol::PacketType::PlayerActorState;
        staleTransport.sourceActorId = 2002;
        staleTransport.connectionNonce = firstConnectionNonce;
        staleTransport.payloadSize = staleSize;
        std::memcpy(staleTransport.payload.data(), staleBytes.data(), staleSize);
        CHECK(test, hostService.HandleReliableMessage(staleTransport));
        CHECK(test, hostService.IsLifecycleActive(2002, 1, 1));
        CHECK(test, hostChannels.FindLifecycle(2002)->connectionNonce ==
            secondConnectionNonce);

        // Map transitions are accepted only when the map epoch advances.
        auto transition = second;
        transition.operation =
            fable::multiplayer::protocol::PlayerActorStateOperation::MapTransition;
        transition.componentFlags = 0;
        PlayerActorLifecycleReducer::ClearStructuralTiming(transition);
        transition.mapEpoch = 3;
        transition.mapId = 300;
        transition.mapName = "Oakvale";
        incoming = MakeReliableTransportMessage(
            transition, secondConnectionNonce, 2);
        CHECK(test, hostService.HandleReliableMessage(incoming));
        CHECK(test, hostService.IsLifecycleActive(2002, 1, 3));
        CHECK(test, hostChannels.IsLifecycleActive(2002, 1, 3));

        auto oldTransition = transition;
        oldTransition.mapEpoch = 2;
        oldTransition.mapId = 200;
        oldTransition.mapName = "NewMap";
        incoming = MakeReliableTransportMessage(
            oldTransition, secondConnectionNonce, 3);
        CHECK(test, hostService.HandleReliableMessage(incoming));
        CHECK(test, hostService.IsLifecycleActive(2002, 1, 3));
        CHECK(test, hostChannels.Find(2002)->mapName == "Oakvale");

        actor_state_testing::SetLocalHeroState(nullptr);
        hostService.Shutdown();
        guestService.Shutdown();
        host.Shutdown();
    }

    void TestLateJoinReceivesCompleteActorBaseline()
    {
        constexpr const char* test = "late join receives actor baseline";
        constexpr std::uint16_t port = 39209;
        constexpr std::uint64_t hostActorId = 1001;

        UdpPeer host;
        CHECK(test, host.StartHost(port, hostActorId, TestDiagnostics()));
        fable::multiplayer::replication::LocalHeroReplication hostHero;
        RemotePlayerChannels hostChannels;
        fable::multiplayer::replication::PlayerActorStateReplication service;
        service.Initialize(
            PeerRole::Host,
            hostActorId,
            host,
            700,
            hostHero,
            hostChannels,
            TestDiagnostics());

        const auto construct = Construct(
            700, 1, 1, 1, kMapId, "Albion", hostActorId);
        PlayerState localState = StateFromConstruct(construct);
        actor_state_testing::SetLocalHeroState(&localState);
        CHECK(test, service.Process());
        CHECK(test, service.IsLifecycleActive(hostActorId, 1, 1));

        UdpPeer firstGuest;
        CHECK(test, firstGuest.StartGuest(
            "127.0.0.1", port, 2002, TestDiagnostics()));
        CHECK(test, WaitFor([&host]
        {
            return host.ConnectedPeerCount() == 1;
        }));
        CHECK(test, service.Process());
        TransportMessage firstBaseline;
        CHECK(test, ReceiveReliable(firstGuest, firstBaseline));

        PlayerActorStateMessage decoded;
        CHECK(test, DecodePlayerActorStateMessage(
            firstBaseline.payload.data(),
            firstBaseline.payloadSize,
            decoded));
        CHECK(test, decoded.operation ==
            PlayerActorStateOperation::Construct);
        CHECK(test, decoded.actorId == hostActorId);
        CHECK(test, decoded.actorGeneration == 1);
        CHECK(test, decoded.mapEpoch == 1);
        CHECK(test, decoded.AppearancePresent());
        CHECK(test, decoded.EquipmentPresent());

        UdpPeer lateGuest;
        CHECK(test, lateGuest.StartGuest(
            "127.0.0.1", port, 2003, TestDiagnostics()));
        CHECK(test, WaitFor([&host]
        {
            return host.ConnectedPeerCount() == 2;
        }));
        CHECK(test, service.Process());
        TransportMessage lateBaseline;
        CHECK(test, ReceiveReliable(lateGuest, lateBaseline));
        decoded = {};
        CHECK(test, DecodePlayerActorStateMessage(
            lateBaseline.payload.data(),
            lateBaseline.payloadSize,
            decoded));
        CHECK(test, decoded.operation ==
            PlayerActorStateOperation::Construct);
        CHECK(test, decoded.actorId == hostActorId);
        CHECK(test, decoded.actorGeneration == 1);
        CHECK(test, decoded.mapEpoch == 1);
        CHECK(test, decoded.AppearancePresent());
        CHECK(test, decoded.EquipmentPresent());

        actor_state_testing::SetLocalHeroState(nullptr);
        service.Shutdown();
        lateGuest.Shutdown();
        firstGuest.Shutdown();
        host.Shutdown();
    }

    void TestSameMapHeroRebindReopensActorLifecycle()
    {
        constexpr const char* test = "same-map Hero rebind lifecycle";
        constexpr std::uint16_t port = 39210;
        constexpr std::uint64_t hostActorId = 1001;

        UdpPeer host;
        CHECK(test, host.StartHost(port, hostActorId, TestDiagnostics()));
        fable::multiplayer::replication::LocalHeroReplication localHero;
        RemotePlayerChannels remoteChannels;
        fable::multiplayer::replication::PlayerActorStateReplication service;
        service.Initialize(
            PeerRole::Host,
            hostActorId,
            host,
            700,
            localHero,
            remoteChannels,
            TestDiagnostics());

        PlayerState first = StateFromConstruct(Construct(
            700, 1, 1, 1, kMapId, "Albion", hostActorId));
        actor_state_testing::SetLocalHeroState(&first);
        CHECK(test, service.Process());
        CHECK(test, service.IsLifecycleActive(hostActorId, 1, 1));

        PlayerState rebound = first;
        rebound.actorGeneration = 2;
        rebound.mapEpoch = 2;
        rebound.position.x += 5.0f;
        actor_state_testing::SetLocalHeroState(&rebound);
        CHECK(test, service.Process());
        CHECK(test, !service.IsLifecycleActive(hostActorId, 1, 1));
        CHECK(test, service.IsLifecycleActive(hostActorId, 2, 2));
        const PlayerActorStateMessage* const current =
            service.Lifecycle(hostActorId, 2, 2);
        CHECK(test, current != nullptr);
        CHECK(test, current != nullptr && current->mapName == "Albion");
        CHECK(test, current != nullptr && current->mapId == kMapId);
        CHECK(test, current != nullptr &&
            current->initialPosition.x == rebound.position.x);

        actor_state_testing::SetLocalHeroState(nullptr);
        service.Shutdown();
        host.Shutdown();
    }

    void TestDelayedRetireCannotEraseReplacementSession()
    {
        constexpr const char* test = "delayed Retire session fence";
        constexpr std::uint64_t oldNonce = 111;
        constexpr std::uint64_t replacementNonce = 222;
        RemotePlayerChannels channels;

        auto first = Construct(700, 1, 1, 10);
        CHECK(test, channels.ApplyActorState(first, 1, oldNonce));
        auto replacement = first;
        replacement.structuralRevision = 20;
        replacement.heroMorph.strength = 0.9f;
        CHECK(test, channels.ApplyActorState(
            replacement, 2, replacementNonce));

        auto delayed = first;
        delayed.operation = PlayerActorStateOperation::Retire;
        delayed.componentFlags = 0;
        delayed.structuralRevision = 11;
        CHECK(test, !channels.ApplyActorState(delayed, 3, oldNonce));
        CHECK(test, channels.Find(kActorId) != nullptr);
        CHECK(test, channels.FindLifecycle(kActorId)->connectionNonce ==
            replacementNonce);

        delayed.structuralRevision = 19;
        CHECK(test, !channels.ApplyActorState(
            delayed, 4, replacementNonce));
        CHECK(test, channels.Find(kActorId) != nullptr);

        delayed.structuralRevision = 21;
        CHECK(test, channels.ApplyActorState(
            delayed, 5, replacementNonce));
        CHECK(test, channels.Find(kActorId) == nullptr);
    }

    void TestMandatoryComponentRemovalIsRejected()
    {
        constexpr const char* test = "mandatory component removal";
        RemotePlayerChannels channels;
        const auto construct = Construct();
        CHECK(test, channels.ApplyActorState(construct, 1, 999));

        auto removal = construct;
        removal.operation = PlayerActorStateOperation::ComponentDelta;
        removal.componentFlags = EquipmentChanged;
        removal.structuralRevision = 2;
        removal.heroEquipment = {};
        CHECK(test, !channels.ApplyActorState(removal, 2, 999));
        CHECK(test, channels.IsLifecycleActive(
            kActorId, kGeneration, kMapEpoch));
        CHECK(test, channels.Find(kActorId)->heroEquipment.Equals(
            construct.heroEquipment));

        removal.componentFlags = AppearanceChanged;
        removal.structuralRevision = 3;
        removal.heroMorph = {};
        CHECK(test, !channels.ApplyActorState(removal, 3, 999));
        CHECK(test, channels.IsLifecycleActive(
            kActorId, kGeneration, kMapEpoch));
    }

    void TestQueuedActionAndVitalsAreFencedByReplacementSession()
    {
        constexpr const char* test =
            "action and vitals replacement-session fence";
        constexpr std::uint64_t actorId = 3001;
        constexpr std::uint64_t oldConnectionNonce = 111;
        constexpr std::uint64_t newConnectionNonce = 222;
        const auto lifecycle = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorId);

        RemotePlayerChannels channels;
        CHECK(test, channels.ApplyActorState(
            lifecycle, 1, oldConnectionNonce));
        UdpPeer transport;
        fable::multiplayer::replication::LocalHeroReplication localHero;
        fable::multiplayer::presentation::RemotePlayerRegistry remotePlayers;
        fable::multiplayer::entities::EntityNetworkIdentityRegistry identities;
        fable::multiplayer::entities::EntityPresenceReplication presence;
        fable::multiplayer::combat::PlayerCombatantDirectory combatants;
        fable::multiplayer::combat::CombatActionLedger combatLedger;
        fable::game::creature::combat::CreatureCombatService combat;
        fable::game::hero_pawn::abilities::HeroWillAbilityService abilities;
        fable::multiplayer::replication::PlayerActionReplication actions;
        actions.Initialize(
            PeerRole::Guest,
            2002,
            transport,
            localHero,
            channels,
            remotePlayers,
            identities,
            presence,
            combatants,
            combatLedger,
            combat,
            abilities,
            TestDiagnostics());

        fable::multiplayer::authority::AuthorityReplication authority;
        fable::multiplayer::entities::EntityLifecycleReplication
            entityLifecycle;
        fable::multiplayer::replication::EntityVitalsReplication vitals;
        vitals.Initialize(
            PeerRole::Guest,
            2002,
            transport,
            authority,
            entityLifecycle,
            identities,
            channels,
            combat,
            CapturingDiagnostics());

        constexpr std::size_t perActorReplayCapacity = 64;
        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId, oldConnectionNonce, index + 1)));
        }
        staleVitalsEvents = 0;
        CHECK(test, vitals.HandleReliableMessage(
            MakePlayerVitalsTransportMessage(
                actorId, oldConnectionNonce, 5, 70.0f)));
        CHECK(test, staleVitalsEvents == 0);

        CHECK(test, channels.ApplyActorState(
            lifecycle, 2, newConnectionNonce));
        fable::multiplayer::RemotePlayerLifecycleInvalidation::Apply(
            channels, actions, vitals);
        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId, newConnectionNonce, 100 + index)));
        }
        // Presentation is latest-current per actor, not a 64-entry replay
        // history. A newer action supersedes the retained slot instead of
        // overflowing or replaying stale animation work.
        CHECK(test, actions.HandleReliableMessage(
            MakePlayerActionTransportMessage(
                actorId,
                newConnectionNonce,
                100 + perActorReplayCapacity)));
        CHECK(test, vitals.HandleReliableMessage(
            MakePlayerVitalsTransportMessage(
                actorId, oldConnectionNonce, 6, 60.0f)));
        CHECK(test, staleVitalsEvents == 1);
        CHECK(test, vitals.HandleReliableMessage(
            MakePlayerVitalsTransportMessage(
                actorId, newConnectionNonce, 1, 95.0f)));
        CHECK(test, staleVitalsEvents == 1);
        vitals.Shutdown();
        actions.Shutdown();
    }

    void TestActionQueueClearsAcrossRetirementAndMapHandoff()
    {
        constexpr const char* test =
            "action queue lifecycle invalidation";
        constexpr std::uint64_t actorId = 3003;
        constexpr std::uint64_t connectionNonce = 333;
        constexpr std::size_t perActorReplayCapacity = 64;

        RemotePlayerChannels channels;
        auto first = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorId);
        CHECK(test, channels.ApplyActorState(
            first, 1, connectionNonce));
        UdpPeer transport;
        fable::multiplayer::replication::LocalHeroReplication localHero;
        fable::multiplayer::presentation::RemotePlayerRegistry remotePlayers;
        fable::multiplayer::entities::EntityNetworkIdentityRegistry identities;
        fable::multiplayer::entities::EntityPresenceReplication presence;
        fable::multiplayer::combat::PlayerCombatantDirectory combatants;
        fable::multiplayer::combat::CombatActionLedger combatLedger;
        fable::game::creature::combat::CreatureCombatService combat;
        fable::game::hero_pawn::abilities::HeroWillAbilityService abilities;
        fable::multiplayer::replication::PlayerActionReplication actions;
        actions.Initialize(
            PeerRole::Guest,
            2002,
            transport,
            localHero,
            channels,
            remotePlayers,
            identities,
            presence,
            combatants,
            combatLedger,
            combat,
            abilities,
            TestDiagnostics());

        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId, connectionNonce, index + 1)));
        }

        auto retire = first;
        retire.operation = PlayerActorStateOperation::Retire;
        retire.componentFlags = 0;
        retire.structuralRevision = 2;
        CHECK(test, channels.ApplyActorState(
            retire, 2, connectionNonce));
        fable::multiplayer::replication::EntityVitalsReplication vitals;
        fable::multiplayer::RemotePlayerLifecycleInvalidation::Apply(
            channels, actions, vitals);

        auto second = Construct(
            700, 2, 2, 3, 200, "NewMap", actorId);
        CHECK(test, channels.ApplyActorState(
            second, 3, connectionNonce));
        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId,
                    connectionNonce,
                    100 + index,
                    false,
                    2,
                    2,
                    "NewMap")));
        }

        auto transition = second;
        transition.operation = PlayerActorStateOperation::MapTransition;
        transition.componentFlags = 0;
        transition.structuralRevision = 4;
        transition.mapEpoch = 3;
        transition.mapId = 300;
        transition.mapName = "Oakvale";
        CHECK(test, channels.ApplyActorState(
            transition, 4, connectionNonce));
        fable::multiplayer::RemotePlayerLifecycleInvalidation::Apply(
            channels, actions, vitals);
        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId,
                    connectionNonce,
                    200 + index,
                    false,
                    2,
                    3,
                    "Oakvale")));
        }
        CHECK(test, actions.HandleReliableMessage(
            MakePlayerActionTransportMessage(
                actorId,
                connectionNonce,
                200 + perActorReplayCapacity,
                false,
                2,
                3,
                "Oakvale")));

        auto replacement = Construct(
            700, 3, 4, 5, 300, "Oakvale", actorId);
        CHECK(test, channels.ApplyActorState(
            replacement, 5, connectionNonce));
        fable::multiplayer::RemotePlayerLifecycleInvalidation::Apply(
            channels, actions, vitals);
        for (std::size_t index = 0; index < perActorReplayCapacity; ++index)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId,
                    connectionNonce,
                    300 + index,
                    false,
                    3,
                    4,
                    "Oakvale")));
        }
        CHECK(test, actions.HandleReliableMessage(
            MakePlayerActionTransportMessage(
                actorId,
                connectionNonce,
                300 + perActorReplayCapacity,
                false,
                3,
                4,
                "Oakvale")));
        actions.Shutdown();
    }

    void TestRapidActionsKeepOnlyLatestCurrentPresentation()
    {
        constexpr const char* test =
            "rapid actions keep latest current presentation";
        constexpr std::uint64_t actorId = 3004;
        constexpr std::uint64_t connectionNonce = 444;

        RemotePlayerChannels channels;
        const auto lifecycle = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorId);
        CHECK(test, channels.ApplyActorState(
            lifecycle, 1, connectionNonce));

        UdpPeer transport;
        fable::multiplayer::replication::LocalHeroReplication localHero;
        fable::multiplayer::presentation::RemotePlayerRegistry remotePlayers;
        fable::multiplayer::entities::EntityNetworkIdentityRegistry identities;
        fable::multiplayer::entities::EntityPresenceReplication presence;
        fable::multiplayer::combat::PlayerCombatantDirectory combatants;
        fable::multiplayer::combat::CombatActionLedger combatLedger;
        fable::game::creature::combat::CreatureCombatService combat;
        fable::game::hero_pawn::abilities::HeroWillAbilityService abilities;
        fable::multiplayer::replication::PlayerActionReplication actions;
        actions.Initialize(
            PeerRole::Guest,
            2002,
            transport,
            localHero,
            channels,
            remotePlayers,
            identities,
            presence,
            combatants,
            combatLedger,
            combat,
            abilities,
            TestDiagnostics());
        ConfigureRemoteActionPresentationForTest(
            true, "Albion", true);

        for (std::uint64_t actionId = 1; actionId <= 128; ++actionId)
        {
            CHECK(test, actions.HandleReliableMessage(
                MakePlayerActionTransportMessage(
                    actorId, connectionNonce, actionId)));
        }
        CHECK(test, actions.ReplayRemotePending());
        CHECK(test, PerformedAbilityCountForTest() == 1);
        CHECK(test,
            LastPerformedAnimationIdForTest() == 128);

        // A delayed reliable semantic event remains ordered, but an action
        // whose presentation window has already elapsed must not restart a
        // stale animation after the latest current action has played.
        TransportMessage expired = MakePlayerActionTransportMessage(
            actorId, connectionNonce, 500);
        PlayerActionMessage expiredAction;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            expired.payload.data(), expired.payloadSize, expiredAction));
        const auto sessionNow = fable::multiplayer::protocol::ToSessionTime(
            transport.SessionTimeMilliseconds());
        expiredAction.startedAtSessionTimeMs = sessionNow - 1'000;
        expiredAction.expectedDurationMs = 100;
        expiredAction.presentationRevision = 500;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            expiredAction,
            expired.payload.data(),
            expired.payload.size(),
            expired.payloadSize));
        CHECK(test, actions.HandleReliableMessage(expired));
        CHECK(test, actions.ReplayRemotePending());
        CHECK(test, PerformedAbilityCountForTest() == 1);

        // Still inside the authored duration, but too late to restart a
        // finite native action from frame zero after the reliable-path grace.
        TransportMessage late = MakePlayerActionTransportMessage(
            actorId, connectionNonce, 500);
        PlayerActionMessage lateAction;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            late.payload.data(), late.payloadSize, lateAction));
        lateAction.startedAtSessionTimeMs = sessionNow - 500;
        lateAction.expectedDurationMs = 1'200;
        lateAction.presentationRevision = 500;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            lateAction,
            late.payload.data(),
            late.payload.size(),
            late.payloadSize));
        CHECK(test, actions.HandleReliableMessage(late));
        CHECK(test, actions.ReplayRemotePending());
        CHECK(test, PerformedAbilityCountForTest() == 1);

        TransportMessage current = MakePlayerActionTransportMessage(
            actorId, connectionNonce, 501);
        PlayerActionMessage currentAction;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            current.payload.data(), current.payloadSize, currentAction));
        currentAction.startedAtSessionTimeMs = sessionNow;
        currentAction.expectedDurationMs = 1'200;
        currentAction.presentationRevision = 501;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            currentAction,
            current.payload.data(),
            current.payload.size(),
            current.payloadSize));
        CHECK(test, actions.HandleReliableMessage(current));
        CHECK(test, actions.ReplayRemotePending());
        CHECK(test, PerformedAbilityCountForTest() == 2);
        CHECK(test,
            LastPerformedAnimationIdForTest() == 501);

        // Clock correction can place an otherwise current action slightly in
        // the future. Keep it pending instead of presenting it early.
        TransportMessage future = MakePlayerActionTransportMessage(
            actorId, connectionNonce, 502);
        PlayerActionMessage futureAction;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            future.payload.data(), future.payloadSize, futureAction));
        futureAction.startedAtSessionTimeMs = sessionNow + 30'000;
        futureAction.expectedDurationMs = 1'200;
        futureAction.presentationRevision = 502;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            futureAction,
            future.payload.data(),
            future.payload.size(),
            future.payloadSize));
        CHECK(test, actions.HandleReliableMessage(future));
        CHECK(test, actions.ReplayRemotePending());
        CHECK(test, PerformedAbilityCountForTest() == 2);
        CHECK(test,
            LastPerformedAnimationIdForTest() == 501);

        ConfigureRemoteActionPresentationForTest(
            false, nullptr, false);
        actions.Shutdown();
    }

    void TestTimedPresentationReplayPolicy()
    {
        constexpr const char* test = "timed presentation replay policy";
        using Presentation =
            fable::multiplayer::presentation::RemotePlayerActionPresentation;

        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::AbilityRequest, 0, 1'200));
        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::AbilityRequest, 250, 1'200));
        CHECK(test, !Presentation::IsReplayEligible(
            PlayerActionKind::AbilityRequest, 251, 1'200));
        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::Expression, 50, 200));
        CHECK(test, !Presentation::IsReplayEligible(
            PlayerActionKind::Expression, 51, 200));
        CHECK(test, !Presentation::IsReplayEligible(
            PlayerActionKind::HeroAbility, 1'201, 1'200));
        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::RangedAim, 10'000, 0));
        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::RangedAimEnd, 250, 250));
        CHECK(test, Presentation::IsReplayEligible(
            PlayerActionKind::RangedAimEnd, 10'000, 250));

        // Revisions use serial-number arithmetic, so wrap-around remains
        // newer without allowing equality or the reverse direction through.
        CHECK(test, Presentation::IsRevisionNewer(1, 0xFFFFFFFFu));
        CHECK(test, !Presentation::IsRevisionNewer(0xFFFFFFFFu, 1));
        CHECK(test, !Presentation::IsRevisionNewer(7, 7));
    }

    void TestActionAndVitalsProducersStayFairUnderActorBackpressure()
    {
        constexpr const char* test =
            "action and vitals producer stream fairness";
        constexpr std::uint64_t actorAId = 3101;
        constexpr std::uint64_t actorBId = 3102;
        constexpr std::uint64_t actorANonce = 3111;
        constexpr std::uint64_t actorBNonce = 3222;

        const auto actorA = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorAId);
        const auto actorB = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorBId);
        RemotePlayerChannels actionChannels;
        CHECK(test, actionChannels.ApplyActorState(
            actorA, 1, actorANonce));
        CHECK(test, actionChannels.ApplyActorState(
            actorB, 1, actorBNonce));

        UdpPeer actionTransport;
        CHECK(test, actionTransport.StartGuest(
            "127.0.0.1", 39204, 1001, TestDiagnostics()));
        const TransportMessage actionAFill =
            MakePlayerActionTransportMessage(
                actorAId, actorANonce, 1, true);
        for (std::size_t index = 0;
             index < fable::multiplayer::ReliableStreamTransport::
                 PerStreamQueueLimit;
             ++index)
        {
            CHECK(test, actionTransport.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(actorAId),
                actionAFill.type,
                actionAFill.payload.data(),
                actionAFill.payloadSize));
        }

        fable::multiplayer::replication::LocalHeroReplication localHero;
        fable::multiplayer::presentation::RemotePlayerRegistry remotePlayers;
        fable::multiplayer::entities::EntityNetworkIdentityRegistry identities;
        fable::multiplayer::entities::EntityPresenceReplication presence;
        fable::multiplayer::combat::PlayerCombatantDirectory combatants;
        fable::multiplayer::combat::CombatActionLedger combatLedger;
        fable::game::creature::combat::CreatureCombatService combat;
        fable::game::hero_pawn::abilities::HeroWillAbilityService abilities;
        fable::multiplayer::replication::PlayerActionReplication actions;
        actions.Initialize(
            PeerRole::Host,
            1001,
            actionTransport,
            localHero,
            actionChannels,
            remotePlayers,
            identities,
            presence,
            combatants,
            combatLedger,
            combat,
            abilities,
            TestDiagnostics());
        CHECK(test, actions.HandleReliableMessage(
            MakePlayerActionTransportMessage(
                actorAId, actorANonce, 10, true)));
        CHECK(test, actions.HandleReliableMessage(
            MakePlayerActionTransportMessage(
                actorBId, actorBNonce, 11, true)));

        const TransportMessage actionBFill =
            MakePlayerActionTransportMessage(
                actorBId, actorBNonce, 12, true);
        for (std::size_t index = 1;
             index < fable::multiplayer::ReliableStreamTransport::
                 PerStreamQueueLimit;
             ++index)
        {
            CHECK(test, actionTransport.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(actorBId),
                actionBFill.type,
                actionBFill.payload.data(),
                actionBFill.payloadSize));
        }
        CHECK(test, !actionTransport.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(actorBId),
            actionBFill.type,
            actionBFill.payload.data(),
            actionBFill.payloadSize));
        actions.Shutdown();
        actionTransport.Shutdown();

        RemotePlayerChannels vitalsChannels;
        CHECK(test, vitalsChannels.ApplyActorState(
            actorA, 1, actorANonce));
        CHECK(test, vitalsChannels.ApplyActorState(
            actorB, 1, actorBNonce));
        UdpPeer vitalsTransport;
        CHECK(test, vitalsTransport.StartGuest(
            "127.0.0.1", 39205, 1001, TestDiagnostics()));
        const TransportMessage vitalsAFill =
            MakePlayerVitalsTransportMessage(
                actorAId, actorANonce, 1, 75.0f, true);
        for (std::size_t index = 0;
             index < fable::multiplayer::ReliableStreamTransport::
                 PerStreamQueueLimit;
             ++index)
        {
            CHECK(test, vitalsTransport.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(actorAId),
                vitalsAFill.type,
                vitalsAFill.payload.data(),
                vitalsAFill.payloadSize));
        }

        fable::multiplayer::authority::AuthorityReplication authority;
        fable::multiplayer::entities::EntityLifecycleReplication
            entityLifecycle;
        fable::multiplayer::replication::EntityVitalsReplication vitals;
        vitals.Initialize(
            PeerRole::Host,
            1001,
            vitalsTransport,
            authority,
            entityLifecycle,
            identities,
            vitalsChannels,
            combat,
            TestDiagnostics());
        CHECK(test, vitals.HandleReliableMessage(
            MakePlayerVitalsTransportMessage(
                actorAId, actorANonce, 1, 70.0f, true)));
        CHECK(test, vitals.HandleReliableMessage(
            MakePlayerVitalsTransportMessage(
                actorBId, actorBNonce, 1, 80.0f, true)));

        const TransportMessage vitalsBFill =
            MakePlayerVitalsTransportMessage(
                actorBId, actorBNonce, 2, 79.0f, true);
        for (std::size_t index = 1;
             index < fable::multiplayer::ReliableStreamTransport::
                 PerStreamQueueLimit;
             ++index)
        {
            CHECK(test, vitalsTransport.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(actorBId),
                vitalsBFill.type,
                vitalsBFill.payload.data(),
                vitalsBFill.payloadSize));
        }
        CHECK(test, !vitalsTransport.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(actorBId),
            vitalsBFill.type,
            vitalsBFill.payload.data(),
            vitalsBFill.payloadSize));
        vitals.Shutdown();
        vitalsTransport.Shutdown();
    }

    void TestReliableActorStreamsAreIndependent()
    {
        constexpr const char* test = "independent actor reliable streams";
        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, 39176))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }

        const auto actorA = Construct(
            700, 1, 1, 1, 100, "Albion", 3001);
        const auto actorB = Construct(
            700, 1, 1, 1, 101, "Bowerstone", 3002);
        std::array<std::uint8_t, 1472> bytesA = {};
        std::array<std::uint8_t, 1472> bytesB = {};
        std::size_t sizeA = 0;
        std::size_t sizeB = 0;
        CHECK(test, EncodeActorPayload(actorA, bytesA, sizeA));
        CHECK(test, EncodeActorPayload(actorB, bytesB, sizeB));

        const auto streamA = fable::multiplayer::reliable_stream::Actor(3001);
        const auto streamB = fable::multiplayer::reliable_stream::Actor(3002);
        for (int index = 0; index < 4; ++index)
        {
            CHECK(test, guest.SubmitReliable(
                streamA,
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                bytesA.data(), sizeA));
        }
        for (int index = 0; index < 2; ++index)
        {
            CHECK(test, guest.SubmitReliable(
                streamB,
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                bytesB.data(), sizeB));
        }

        // Do not read the application queue while both streams are in flight.
        // The transport must still deliver both streams; their sequence
        // spaces are independent and no actor can consume another's quota.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::array<std::uint32_t, 2> lastSequence = {};
        std::array<int, 2> counts = {};
        CHECK(test, WaitFor([&host, &lastSequence, &counts]
        {
            TransportMessage message;
            while (host.TryConsumeReliable(message))
            {
                const std::size_t streamIndex =
                    message.streamId ==
                        fable::multiplayer::reliable_stream::Actor(3001)
                    ? 0
                    : message.streamId ==
                        fable::multiplayer::reliable_stream::Actor(3002)
                    ? 1
                    : 2;
                if (streamIndex >= counts.size())
                {
                    return false;
                }
                ++counts[streamIndex];
                if (message.sequence != lastSequence[streamIndex] + 1)
                {
                    return false;
                }
                lastSequence[streamIndex] = message.sequence;
            }
            return counts[0] == 4 && counts[1] == 2;
        }));
        CHECK(test, lastSequence[0] == 4);
        CHECK(test, lastSequence[1] == 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        TransportMessage duplicate;
        CHECK(test, !host.TryConsumeReliable(duplicate));

        guest.Shutdown();
        host.Shutdown();
    }

    void TestHostFanoutOwnsEveryPeerBeforeReportingSuccess()
    {
        constexpr const char* test = "host fanout owns every peer";
        constexpr std::uint16_t port = 39204;
        constexpr std::uint64_t hostActorId = 1001;
        constexpr std::uint64_t actorAId = 2002;
        constexpr std::uint64_t actorBId = 2003;
        constexpr std::uint64_t actorANonce = 0xA002;
        constexpr std::uint64_t actorBNonce = 0xB003;

        WSADATA winsock = {};
        CHECK(test, WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
        UdpPeer host;
        CHECK(test, host.StartHost(port, hostActorId, TestDiagnostics()));
        const SOCKET actorA = OpenRawUdpSocket();
        const SOCKET actorB = OpenRawUdpSocket();
        CHECK(test, actorA != INVALID_SOCKET);
        CHECK(test, actorB != INVALID_SOCKET);
        if (actorA == INVALID_SOCKET || actorB == INVALID_SOCKET ||
            !host.IsStarted())
        {
            if (actorA != INVALID_SOCKET)
            {
                closesocket(actorA);
            }
            if (actorB != INVALID_SOCKET)
            {
                closesocket(actorB);
            }
            host.Shutdown();
            WSACleanup();
            return;
        }

        const sockaddr_in hostEndpoint = LoopbackEndpoint(port);
        std::uint64_t observedHostNonceA = 0;
        std::uint64_t observedHostNonceB = 0;
        CHECK(test, CompleteRawGuestHandshake(
            actorA,
            hostEndpoint,
            actorAId,
            actorANonce,
            observedHostNonceA));
        CHECK(test, WaitFor([&host]
        {
            return host.ConnectedPeerCount() == 1;
        }));
        CHECK(test, CompleteRawGuestHandshake(
            actorB,
            hostEndpoint,
            actorBId,
            actorBNonce,
            observedHostNonceB));
        CHECK(test, WaitFor([&host]
        {
            return host.ConnectedPeerCount() == 2;
        }));
        CHECK(test, observedHostNonceA == host.ConnectionNonce());
        CHECK(test, observedHostNonceB == host.ConnectionNonce());

        auto hostActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", hostActorId);
        PopulateAllBoneScales(hostActor);
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(hostActor, payload, payloadSize));
        constexpr std::size_t logicalPerStreamCapacity =
            fable::multiplayer::ReliableStreamTransport::
                PerStreamQueueLimit /
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount;
        for (std::size_t index = 0;
             index < logicalPerStreamCapacity;
             ++index)
        {
            CHECK(test, host.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(hostActorId),
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                payload.data(),
                payloadSize));
        }

        // Peer A deliberately withholds every acknowledgement. Peer B drains
        // the same stream, proving fanout backpressure is isolated per peer.
        std::uint32_t actorBHighestSequence = 0;
        CHECK(test, ReceiveAndAcknowledgeRawActorThrough(
            actorB,
            actorBId,
            actorBNonce,
            hostActorId,
            host.ConnectionNonce(),
            static_cast<std::uint32_t>(
                logicalPerStreamCapacity *
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount),
            actorBHighestSequence));

        // Success means the transport durably owns this message for both
        // peers. B must receive it before A is allowed to drain, and A must
        // receive the same logical sequence after capacity becomes available.
        const bool fanoutOwned = host.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(hostActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            payload.data(),
            payloadSize);
        CHECK(test, fanoutOwned);
        CHECK(test, ReceiveAndAcknowledgeRawActorThrough(
            actorB,
            actorBId,
            actorBNonce,
            hostActorId,
            host.ConnectionNonce(),
            static_cast<std::uint32_t>(
                (logicalPerStreamCapacity + 1) *
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount),
            actorBHighestSequence,
            std::chrono::milliseconds(2'000)));

        std::uint32_t actorAHighestSequence = 0;
        CHECK(test, ReceiveAndAcknowledgeRawActorThrough(
            actorA,
            actorAId,
            actorANonce,
            hostActorId,
            host.ConnectionNonce(),
            static_cast<std::uint32_t>(
                (logicalPerStreamCapacity + 1) *
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount),
            actorAHighestSequence,
            std::chrono::milliseconds(7'500)));
        const auto expectedPhysicalSequence = static_cast<std::uint32_t>(
            (logicalPerStreamCapacity + 1) *
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        CHECK(test, actorAHighestSequence == expectedPhysicalSequence);
        CHECK(test, actorBHighestSequence == expectedPhysicalSequence);

        closesocket(actorA);
        closesocket(actorB);
        host.Shutdown();
        WSACleanup();
    }

    void TestHostLogicalStreamsStayFairUnderBackpressure()
    {
        constexpr const char* test =
            "host logical stream admission fairness";
        constexpr std::uint16_t port = 39208;
        constexpr std::uint64_t hostActorId = 1001;
        constexpr std::uint64_t rawActorId = 2002;
        constexpr std::uint64_t rawConnectionNonce = 0xA002;
        constexpr std::uint64_t blockedActorId = 6101;
        constexpr std::uint64_t availableActorId = 6102;

        WSADATA winsock = {};
        CHECK(test, WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
        UdpPeer host;
        CHECK(test, host.StartHost(port, hostActorId, TestDiagnostics()));
        const SOCKET rawGuest = OpenRawUdpSocket();
        CHECK(test, rawGuest != INVALID_SOCKET);
        if (rawGuest == INVALID_SOCKET || !host.IsStarted())
        {
            if (rawGuest != INVALID_SOCKET)
            {
                closesocket(rawGuest);
            }
            host.Shutdown();
            WSACleanup();
            return;
        }

        std::uint64_t observedHostNonce = 0;
        CHECK(test, CompleteRawGuestHandshake(
            rawGuest,
            LoopbackEndpoint(port),
            rawActorId,
            rawConnectionNonce,
            observedHostNonce));
        CHECK(test, WaitFor([&host]
        {
            return host.ConnectedPeerCount() == 1;
        }));
        CHECK(test, observedHostNonce == host.ConnectionNonce());

        auto blockedActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", blockedActorId);
        auto availableActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", availableActorId);
        PopulateAllBoneScales(blockedActor);
        PopulateAllBoneScales(availableActor);
        std::array<std::uint8_t, 1472> blockedPayload = {};
        std::array<std::uint8_t, 1472> availablePayload = {};
        std::size_t blockedPayloadSize = 0;
        std::size_t availablePayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            blockedActor, blockedPayload, blockedPayloadSize));
        CHECK(test, EncodeActorPayload(
            availableActor, availablePayload, availablePayloadSize));

        // Fill the peer's actor-A stream, then its durable host backlog. The
        // raw guest deliberately never acknowledges actor A.
        constexpr std::size_t blockedCapacity =
            fable::multiplayer::ReliableStreamTransport::
                PerStreamQueueLimit * 2;
        std::size_t acceptedBlocked = 0;
        const auto fillDeadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(5'000);
        while (acceptedBlocked < blockedCapacity &&
            std::chrono::steady_clock::now() < fillDeadline)
        {
            if (host.SubmitReliable(
                    fable::multiplayer::reliable_stream::Actor(
                        blockedActorId),
                    fable::multiplayer::protocol::PacketType::
                        PlayerActorState,
                    blockedPayload.data(),
                    blockedPayloadSize))
            {
                ++acceptedBlocked;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        CHECK(test, acceptedBlocked == blockedCapacity);
        CHECK(test, !host.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(blockedActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            blockedPayload.data(),
            blockedPayloadSize));

        // Actor B has an independent durable share and must be delivered even
        // though actor A remains fully backpressured.
        CHECK(test, host.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(availableActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            availablePayload.data(),
            availablePayloadSize));
        std::uint32_t availableHighestSequence = 0;
        CHECK(test, ReceiveAndAcknowledgeRawActorStreamThrough(
            rawGuest,
            rawActorId,
            rawConnectionNonce,
            hostActorId,
            availableActorId,
            host.ConnectionNonce(),
            static_cast<std::uint32_t>(
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount),
            availableHighestSequence,
            std::chrono::milliseconds(2'000)));
        CHECK(test, availableHighestSequence ==
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        CHECK(test, !host.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(blockedActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            blockedPayload.data(),
            blockedPayloadSize));

        closesocket(rawGuest);
        host.Shutdown();
        WSACleanup();
    }

    void TestDelayedAcknowledgementCannotDrainRecreatedStream()
    {
        constexpr const char* test = "recreated stream ack incarnation";
        constexpr std::uint64_t actorId = 0xD000000000000042ull;
        auto actor = Construct(
            700, 1, 1, 1, kMapId, "Albion", actorId);
        PopulateAllBoneScales(actor);
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(actor, payload, payloadSize));

        fable::multiplayer::ReliableStreamTransport transport;
        const auto stream = fable::multiplayer::reliable_stream::Actor(actorId);
        CHECK(test, transport.Enqueue(
            stream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            actorId,
            payload.data(),
            payloadSize));
        const auto originalDue = transport.Due(1, 100);
        CHECK(test, originalDue.size() ==
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        if (originalDue.empty())
        {
            return;
        }
        const TransportMessage original = originalDue.front();
        for (const auto& fragment : originalDue)
        {
            CHECK(test, transport.AcceptAcknowledgement(
                stream,
                fragment.streamIncarnation,
                fragment.sequence));
        }
        CHECK(test, transport.Due(2, 100).empty());
        CHECK(test, transport.OutboundSize() == 0);

        CHECK(test, transport.Enqueue(
            stream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            actorId,
            payload.data(),
            payloadSize));
        const auto recreatedDue = transport.Due(4, 100);
        CHECK(test, recreatedDue.size() ==
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        if (recreatedDue.empty())
        {
            return;
        }
        const TransportMessage recreated = recreatedDue.front();
        CHECK(test, recreated.streamIncarnation != 0);
        CHECK(test, recreated.streamIncarnation !=
            original.streamIncarnation);
        CHECK(test, !transport.AcceptAcknowledgement(
            stream,
            original.streamIncarnation,
            original.sequence));

        const auto retry = transport.Due(105, 100);
        CHECK(test, retry.size() == recreatedDue.size());
        if (!retry.empty())
        {
            CHECK(test, retry.front().streamIncarnation ==
                recreated.streamIncarnation);
            CHECK(test, retry.front().sequence == recreated.sequence);
        }
        for (const auto& fragment : recreatedDue)
        {
            CHECK(test, transport.AcceptAcknowledgement(
                stream,
                fragment.streamIncarnation,
                fragment.sequence));
        }
        CHECK(test, transport.Due(107, 100).empty());
        CHECK(test, transport.OutboundSize() == 0);
    }

    void TestIncarnationChurnRejectsReclaimedDelayedData()
    {
        constexpr const char* test = "stream incarnation churn fence";
        constexpr std::uint64_t activeActorId = 0xD000000000000043ull;
        constexpr std::uint64_t churnActorId = 0xD000000000000044ull;
        constexpr std::size_t incarnationCount =
            fable::multiplayer::ReliableStreamTransport::
                StreamMetadataLimit + 2;

        auto activeActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", activeActorId);
        auto churnActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", churnActorId);
        PopulateAllBoneScales(activeActor);
        PopulateAllBoneScales(churnActor);
        std::array<std::uint8_t, 1472> activePayload = {};
        std::array<std::uint8_t, 1472> churnPayload = {};
        std::size_t activePayloadSize = 0;
        std::size_t churnPayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            activeActor, activePayload, activePayloadSize));
        CHECK(test, EncodeActorPayload(
            churnActor, churnPayload, churnPayloadSize));

        fable::multiplayer::ReliableStreamTransport sender;
        fable::multiplayer::ReliableStreamTransport receiver;
        const auto activeStream =
            fable::multiplayer::reliable_stream::Actor(activeActorId);
        const auto churnStream =
            fable::multiplayer::reliable_stream::Actor(churnActorId);
        CHECK(test, sender.Enqueue(
            activeStream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            activeActorId,
            activePayload.data(),
            activePayloadSize));
        const auto activeDue = sender.Due(1, 100);
        CHECK(test, activeDue.size() ==
            fable::multiplayer::ReliableStreamTransport::
                MaximumFragmentCount);
        if (activeDue.size() < 2)
        {
            return;
        }
        for (const auto& fragment : activeDue)
        {
            CHECK(test, receiver.AcceptIncoming(fragment) ==
                fable::multiplayer::ReliableReceiveResult::Accepted);
            CHECK(test, sender.AcceptAcknowledgement(
                activeStream,
                fragment.streamIncarnation,
                fragment.sequence));
        }
        const TransportMessage activeMessage = activeDue.back();
        TransportMessage consumed;
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.streamId == activeStream);
        CHECK(test, sender.Due(2, 100).empty());
        CHECK(test, sender.OutboundSize() == 0);

        TransportMessage oldestChurnMessage;
        TransportMessage newestChurnMessage;
        std::uint64_t previousIncarnation = activeMessage.streamIncarnation;
        bool incarnationsMonotonic = previousIncarnation != 0;
        bool churnCompleted = true;
        std::uint64_t now = 10;
        for (std::size_t index = 0; index < incarnationCount; ++index)
        {
            if (!sender.Enqueue(
                    churnStream,
                    fable::multiplayer::protocol::PacketType::
                        PlayerActorState,
                    churnActorId,
                    churnPayload.data(),
                    churnPayloadSize))
            {
                churnCompleted = false;
                break;
            }
            const auto due = sender.Due(now, 100);
            if (due.size() !=
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount)
            {
                churnCompleted = false;
                break;
            }
            const TransportMessage firstMessage = due.front();
            incarnationsMonotonic = incarnationsMonotonic &&
                firstMessage.streamIncarnation > previousIncarnation;
            previousIncarnation = firstMessage.streamIncarnation;
            for (const auto& fragment : due)
            {
                if (fragment.streamIncarnation !=
                        firstMessage.streamIncarnation ||
                    receiver.AcceptIncoming(fragment) !=
                        fable::multiplayer::ReliableReceiveResult::Accepted ||
                    !sender.AcceptAcknowledgement(
                        churnStream,
                        fragment.streamIncarnation,
                        fragment.sequence))
                {
                    churnCompleted = false;
                    break;
                }
            }
            const TransportMessage message = due.back();
            if (!churnCompleted || !receiver.TryConsume(consumed) ||
                consumed.streamId != churnStream)
            {
                churnCompleted = false;
                break;
            }
            if (index == 0)
            {
                oldestChurnMessage = message;
            }
            newestChurnMessage = message;
            if (!sender.Due(now + 1, 100).empty() ||
                sender.OutboundSize() != 0)
            {
                churnCompleted = false;
                break;
            }
            now += 2;
        }
        CHECK(test, churnCompleted);
        CHECK(test, incarnationsMonotonic);
        if (!churnCompleted)
        {
            return;
        }
        CHECK(test, newestChurnMessage.streamIncarnation >
            activeMessage.streamIncarnation);

        // The oldest incarnation has fallen outside the bounded explicit
        // retired set. A monotonic per-stream fence must still reject its
        // delayed data rather than roll the receiver back to incarnation one.
        CHECK(test, receiver.AcceptIncoming(oldestChurnMessage) ==
            fable::multiplayer::ReliableReceiveResult::Rejected);

        // Incarnation ordering is scoped by stream. Retransmission from a
        // still-current stream created before the churn remains a duplicate,
        // never a globally-stale rejection.
        CHECK(test, receiver.AcceptIncoming(activeMessage) ==
            fable::multiplayer::ReliableReceiveResult::Duplicate);
    }

    void TestIndependentStreamsMayArriveOutOfIncarnationOrder()
    {
        constexpr const char* test =
            "cross-stream incarnation reordering";
        constexpr std::uint64_t laterActorId = 0xD000000000000046ull;

        const auto laterActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", laterActorId);
        fable::multiplayer::protocol::AuthorityMessage authority;
        authority.operation =
            fable::multiplayer::protocol::AuthorityOperation::Request;
        authority.scope =
            fable::multiplayer::protocol::AuthorityScope::MapSimulation;
        authority.ownerActorId = laterActorId;
        authority.mapId = kMapId;
        authority.mapName = "Albion";
        TransportMessage earlier;
        earlier.type =
            fable::multiplayer::protocol::PacketType::Authority;
        earlier.sourceActorId = laterActorId;
        earlier.streamId = fable::multiplayer::reliable_stream::Control;
        earlier.sequence = 1;
        CHECK(test,
            fable::multiplayer::protocol::EncodeAuthorityMessage(
                authority,
                earlier.payload.data(),
                earlier.payload.size(),
                earlier.payloadSize));
        TransportMessage later = MakeReliableTransportMessage(
            laterActor, laterActorId, 1);
        earlier.streamIncarnation = 10'000;
        later.streamIncarnation = 10'001;

        fable::multiplayer::ReliableStreamTransport receiver;
        CHECK(test, receiver.AcceptIncoming(later) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        CHECK(test, receiver.AcceptIncoming(earlier) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);

        TransportMessage consumed;
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.streamId == later.streamId);
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.streamId ==
            fable::multiplayer::reliable_stream::Control);

        TransportMessage staleLater = later;
        staleLater.streamIncarnation = later.streamIncarnation - 1;
        CHECK(test, receiver.AcceptIncoming(std::move(staleLater)) ==
            fable::multiplayer::ReliableReceiveResult::Rejected);
    }

    void TestFullWidthReliableStreamIdentityAndValidation()
    {
        constexpr const char* test = "full-width reliable stream identity";
        constexpr std::uint64_t lowActorId = 0x0000000000000042ull;
        constexpr std::uint64_t highActorId = 0xC000000000000042ull;
        const auto lowStream =
            fable::multiplayer::reliable_stream::Actor(lowActorId);
        const auto highStream =
            fable::multiplayer::reliable_stream::Actor(highActorId);
        CHECK(test, lowStream != highStream);
        std::unordered_set<fable::multiplayer::ReliableStreamId> streams;
        streams.insert(lowStream);
        streams.insert(highStream);
        CHECK(test, streams.size() == 2);

        const auto lowActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", lowActorId);
        const auto highActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", highActorId);
        std::array<std::uint8_t, 1472> lowPayload = {};
        std::array<std::uint8_t, 1472> payload = {};
        std::size_t lowPayloadSize = 0;
        std::size_t payloadSize = 0;
        CHECK(test, EncodeActorPayload(
            lowActor, lowPayload, lowPayloadSize));
        CHECK(test, EncodeActorPayload(highActor, payload, payloadSize));

        fable::multiplayer::ReliableStreamTransport streamTransport;
        CHECK(test, streamTransport.Enqueue(
            lowStream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            lowActorId,
            lowPayload.data(), lowPayloadSize));
        CHECK(test, streamTransport.Enqueue(
            highStream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            highActorId,
            payload.data(), payloadSize));
        const auto due = streamTransport.Due(1, 100);
        CHECK(test, due.size() == 2);
        bool sawLow = false;
        bool sawHigh = false;
        for (const auto& message : due)
        {
            sawLow = sawLow || message.streamId == lowStream;
            sawHigh = sawHigh || message.streamId == highStream;
            CHECK(test, message.sequence == 1);
        }
        CHECK(test, sawLow && sawHigh);

        UdpPeer isolatedGuest;
        CHECK(test, isolatedGuest.StartGuest(
            "127.0.0.1", 39201, highActorId, TestDiagnostics()));
        CHECK(test, isolatedGuest.SubmitReliable(
            highStream,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            payload.data(), payloadSize));
        CHECK(test, !isolatedGuest.SubmitReliable(
            fable::multiplayer::reliable_stream::Entity(highActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            payload.data(), payloadSize));
        CHECK(test, !isolatedGuest.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(highActorId + 1),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            payload.data(), payloadSize));
        const fable::multiplayer::ReliableStreamId arbitrary = {
            static_cast<fable::multiplayer::ReliableStreamKind>(99),
            highActorId};
        CHECK(test, !isolatedGuest.SubmitReliable(
            arbitrary,
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            payload.data(), payloadSize));
        isolatedGuest.Shutdown();
    }

    void TestWorldLifecycleTransactionStream()
    {
        constexpr const char* test = "world lifecycle transaction stream";
        fable::multiplayer::protocol::EntityLifecycleMessage baseline;
        baseline.operation = fable::multiplayer::protocol::
            EntityLifecycleOperation::BaselineBegin;
        baseline.baselineId = 17;
        baseline.worldRevision = 1;

        std::array<std::uint8_t, 1472> payload = {};
        std::size_t payloadSize = 0;
        CHECK(test,
            fable::multiplayer::protocol::EncodeEntityLifecycleMessage(
                baseline, payload.data(), payload.size(), payloadSize));

        fable::multiplayer::ReliableStreamTransport sender;
        CHECK(test, sender.CanEnqueueMessage(
            fable::multiplayer::reliable_stream::WorldLifecycle,
            fable::multiplayer::protocol::PacketType::EntityLifecycle,
            payload.data(), payloadSize));
        CHECK(test, sender.Enqueue(
            fable::multiplayer::reliable_stream::WorldLifecycle,
            fable::multiplayer::protocol::PacketType::EntityLifecycle,
            1001, payload.data(), payloadSize));
        CHECK(test, !sender.Enqueue(
            fable::multiplayer::reliable_stream::Entity(0),
            fable::multiplayer::protocol::PacketType::EntityLifecycle,
            1001, payload.data(), payloadSize));

        const auto due = sender.Due(1, 100);
        CHECK(test, due.size() == 1);
        CHECK(test, due.front().streamId ==
            fable::multiplayer::reliable_stream::WorldLifecycle);

        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> datagram = {};
        std::size_t datagramSize = 0;
        CHECK(test,
            fable::multiplayer::transport_codec::EncodeReliablePacket(
                due.front(), 1001, 2002, datagram, datagramSize));
        fable::multiplayer::protocol::PacketView packet;
        CHECK(test, fable::multiplayer::protocol::DecodePacket(
            datagram.data(), datagramSize, packet));
        CHECK(test, packet.envelope.streamKind ==
            static_cast<std::uint8_t>(
                fable::multiplayer::ReliableStreamKind::World));
        CHECK(test, packet.envelope.streamId == 0);

        fable::multiplayer::ReliableStreamTransport receiver;
        CHECK(test, receiver.AcceptIncoming(due.front()) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        TransportMessage consumed;
        CHECK(test, receiver.TryConsume(consumed));
        CHECK(test, consumed.type ==
            fable::multiplayer::protocol::PacketType::EntityLifecycle);
        CHECK(test, consumed.streamId ==
            fable::multiplayer::reliable_stream::WorldLifecycle);
    }

    void TestReliableStreamMetadataIsBoundedAndRetired()
    {
        constexpr const char* test = "bounded reliable stream metadata";
        constexpr std::uint64_t firstActorId = 50'000;
        fable::multiplayer::ReliableStreamTransport outbound;
        for (std::size_t index = 0;
             index < fable::multiplayer::ReliableStreamTransport::
                 StreamMetadataLimit;
             ++index)
        {
            const std::uint64_t actorId = firstActorId + index;
            auto actor = Construct(
                700, 1, 1, 1, kMapId, "Albion", actorId);
            PopulateAllBoneScales(actor);
            std::array<std::uint8_t, 1472> payload = {};
            std::size_t payloadSize = 0;
            CHECK(test, EncodeActorPayload(actor, payload, payloadSize));
            CHECK(test, outbound.Enqueue(
                fable::multiplayer::reliable_stream::Actor(actorId),
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                actorId,
                payload.data(), payloadSize));
        }

        const std::uint64_t overflowActorId = firstActorId +
            fable::multiplayer::ReliableStreamTransport::StreamMetadataLimit;
        auto overflowActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", overflowActorId);
        PopulateAllBoneScales(overflowActor);
        std::array<std::uint8_t, 1472> overflowPayload = {};
        std::size_t overflowPayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            overflowActor, overflowPayload, overflowPayloadSize));
        CHECK(test, !outbound.Enqueue(
            fable::multiplayer::reliable_stream::Actor(overflowActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            overflowActorId,
            overflowPayload.data(), overflowPayloadSize));

        const auto due = outbound.Due(1, 100);
        CHECK(test, due.size() ==
            fable::multiplayer::ReliableStreamTransport::StreamMetadataLimit *
                fable::multiplayer::ReliableStreamTransport::
                    MaximumFragmentCount);
        for (const auto& message : due)
        {
            CHECK(test, outbound.AcceptAcknowledgement(
                message.streamId,
                message.streamIncarnation,
                message.sequence));
        }
        const auto tails = outbound.Due(2, 100);
        CHECK(test, tails.empty());
        CHECK(test, outbound.Due(3, 100).empty());
        CHECK(test, outbound.OutboundSize() == 0);
        CHECK(test, outbound.Enqueue(
            fable::multiplayer::reliable_stream::Actor(overflowActorId),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            overflowActorId,
            overflowPayload.data(), overflowPayloadSize));

        fable::multiplayer::ReliableStreamTransport inbound;
        TransportMessage oldestCompleted;
        for (std::size_t index = 0;
             index < fable::multiplayer::ReliableStreamTransport::
                 StreamMetadataLimit;
             ++index)
        {
            const std::uint64_t actorId = firstActorId + index;
            const auto actor = Construct(
                700, 1, 1, 1, kMapId, "Albion", actorId);
            TransportMessage message = MakeReliableTransportMessage(
                actor, 99, 1);
            message.streamIncarnation = 1'000 + index;
            if (index == 0)
            {
                oldestCompleted = message;
            }
            CHECK(test, inbound.AcceptIncoming(std::move(message)) ==
                fable::multiplayer::ReliableReceiveResult::Accepted);
        }
        TransportMessage overflow = MakeReliableTransportMessage(
            overflowActor, 99, 1);
        overflow.streamIncarnation = 1'000 +
            fable::multiplayer::ReliableStreamTransport::StreamMetadataLimit;
        CHECK(test, inbound.AcceptIncoming(overflow) ==
            fable::multiplayer::ReliableReceiveResult::Backpressured);
        std::size_t consumed = 0;
        TransportMessage consumedMessage;
        while (inbound.TryConsume(consumedMessage))
        {
            ++consumed;
        }
        CHECK(test, consumed ==
            fable::multiplayer::ReliableStreamTransport::StreamMetadataLimit);
        CHECK(test, inbound.AcceptIncoming(overflow) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
        CHECK(test, inbound.AcceptIncoming(std::move(oldestCompleted)) ==
            fable::multiplayer::ReliableReceiveResult::Rejected);
        inbound.Clear();
        CHECK(test, inbound.AcceptIncoming(std::move(overflow)) ==
            fable::multiplayer::ReliableReceiveResult::Accepted);
    }

    void TestActorLifecycleProducerFairnessUnderBackpressure()
    {
        constexpr const char* test =
            "actor lifecycle producer stream fairness";
        constexpr std::uint64_t actorAId = 3001;
        constexpr std::uint64_t actorBId = 3002;
        const auto actorA = Construct(
            700, 1, 1, 1, 101, "Albion", actorAId);
        const auto actorB = Construct(
            700, 1, 1, 1, 102, "Bowerstone", actorBId);
        std::array<std::uint8_t, 1472> actorAPayload = {};
        std::size_t actorAPayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            actorA, actorAPayload, actorAPayloadSize));

        UdpPeer isolatedGuest;
        CHECK(test, isolatedGuest.StartGuest(
            "127.0.0.1", 39202, actorAId, TestDiagnostics()));
        for (std::size_t index = 0;
             index < fable::multiplayer::ReliableStreamTransport::
                 PerStreamQueueLimit;
             ++index)
        {
            CHECK(test, isolatedGuest.SubmitReliable(
                fable::multiplayer::reliable_stream::Actor(actorAId),
                fable::multiplayer::protocol::PacketType::PlayerActorState,
                actorAPayload.data(), actorAPayloadSize));
        }

        fable::multiplayer::replication::PlayerActorStatePublicationQueue
            publication;
        const fable::core::Diagnostics diagnostics = TestDiagnostics();
        publication.Initialize(diagnostics);
        CHECK(test, publication.Append(actorA));
        CHECK(test, publication.Append(actorB));
        CHECK(test, publication.PublishPending(isolatedGuest));
        CHECK(test, publication.Size() == 1);
        CHECK(test, publication.HasConstruct(actorAId));
        CHECK(test, !publication.HasConstruct(actorBId));
        isolatedGuest.Shutdown();
    }

    void TestEntityMovementIngressKeepsLatestPerSubject()
    {
        constexpr const char* test = "entity movement latest per subject";
        fable::multiplayer::MovementTransport transport;
        CHECK(test, transport.AcceptEntity(EntityMovementTransportMessage(
            MakeEntityMovement(9001, 1, 1.0f))));
        CHECK(test, transport.AcceptEntity(EntityMovementTransportMessage(
            MakeEntityMovement(9002, 1, 10.0f))));
        CHECK(test, transport.AcceptEntity(EntityMovementTransportMessage(
            MakeEntityMovement(9001, 2, 2.0f))));
        CHECK(test, transport.AcceptEntity(EntityMovementTransportMessage(
            MakeEntityMovement(9001, 3, 3.0f))));
        CHECK(test, !transport.AcceptEntity(EntityMovementTransportMessage(
            MakeEntityMovement(9001, 2, 2.0f))));

        std::size_t count = 0;
        bool sawFirstLatest = false;
        bool sawSecond = false;
        TransportMessage incoming;
        while (transport.TryConsumeEntity(incoming))
        {
            fable::multiplayer::protocol::EntityMovementMessage decoded;
            CHECK(test,
                fable::multiplayer::protocol::DecodeEntityMovementMessage(
                    incoming.payload.data(), incoming.payloadSize, decoded));
            ++count;
            if (decoded.entityUid == 9001)
            {
                sawFirstLatest = decoded.sequence == 3 &&
                    decoded.position.x == 3.0f;
            }
            if (decoded.entityUid == 9002)
            {
                sawSecond = decoded.sequence == 1;
            }
        }
        CHECK(test, count == 2);
        CHECK(test, sawFirstLatest);
        CHECK(test, sawSecond);

        const TransportMessage newest = EntityMovementTransportMessage(
            MakeEntityMovement(9003, 3, 3.0f));
        const TransportMessage delayed = EntityMovementTransportMessage(
            MakeEntityMovement(9003, 2, 2.0f));
        CHECK(test, transport.QueueEntity(
            newest.sourceActorId,
            newest.type,
            newest.payload.data(),
            newest.payloadSize));
        CHECK(test, transport.QueueEntity(
            delayed.sourceActorId,
            delayed.type,
            delayed.payload.data(),
            delayed.payloadSize));
        const std::vector<TransportMessage> outbound =
            transport.TakeEntityOutbound();
        CHECK(test, outbound.size() == 1);
        fable::multiplayer::protocol::EntityMovementMessage decoded;
        CHECK(test,
            !outbound.empty() &&
            fable::multiplayer::protocol::DecodeEntityMovementMessage(
                outbound.front().payload.data(),
                outbound.front().payloadSize,
                decoded));
        CHECK(test, decoded.sequence == 3);
        CHECK(test, decoded.position.x == 3.0f);
    }

    void TestGuestDropsTrafficBeforeHandshakeLatch()
    {
        constexpr const char* test = "guest pre-handshake traffic fence";
        constexpr std::uint16_t port = 39203;
        WSADATA winsock = {};
        CHECK(test, WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
        const SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        CHECK(test, server != INVALID_SOCKET);
        if (server == INVALID_SOCKET)
        {
            WSACleanup();
            return;
        }
        const sockaddr_in serverEndpoint = LoopbackEndpoint(port);
        CHECK(test, bind(
            server,
            reinterpret_cast<const sockaddr*>(&serverEndpoint),
            sizeof(serverEndpoint)) == 0);
        const DWORD timeout = 2'000;
        CHECK(test, setsockopt(
            server,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout),
            sizeof(timeout)) == 0);

        UdpPeer guest;
        CHECK(test, guest.StartGuest(
            "127.0.0.1", port, 2002, TestDiagnostics()));
        const auto localActor = Construct(
            700, 1, 1, 1, kMapId, "Albion", 2002);
        std::array<std::uint8_t, 1472> localActorPayload = {};
        std::size_t localActorPayloadSize = 0;
        CHECK(test, EncodeActorPayload(
            localActor, localActorPayload, localActorPayloadSize));
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> hello = {};
        sockaddr_in guestEndpoint = {};
        int guestEndpointSize = sizeof(guestEndpoint);
        const int received = recvfrom(
            server,
            reinterpret_cast<char*>(hello.data()),
            static_cast<int>(hello.size()),
            0,
            reinterpret_cast<sockaddr*>(&guestEndpoint),
            &guestEndpointSize);
        CHECK(test, received > 0);

        constexpr std::uint64_t oldHostNonce = 0xA001;
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            wrongChallenge = {};
        const std::uint64_t wrongGuestNonce = guest.ConnectionNonce() + 1;
        std::memcpy(
            wrongChallenge.data(), &wrongGuestNonce,
            sizeof(wrongGuestNonce));
        std::array<std::uint8_t,
            fable::multiplayer::protocol::MaximumDatagramBytes> datagram = {};
        std::size_t datagramSize = 0;
        CHECK(test, fable::multiplayer::transport_codec::EncodePeerHello(
            1001,
            oldHostNonce,
            wrongChallenge.data(),
            wrongChallenge.size(),
            datagram,
            datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        const auto actor = Construct(
            700, 1, 1, 1, kMapId, "Albion", 1001);
        TransportMessage reliable;
        CHECK(test, MakeFirstActorReliableDatagram(actor, reliable));
        CHECK(test,
            fable::multiplayer::transport_codec::EncodeReliablePacket(
                reliable, 1001, oldHostNonce, datagram, datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        CHECK(test,
            fable::multiplayer::transport_codec::EncodeAcknowledgement(
                1001,
                oldHostNonce,
                fable::multiplayer::reliable_stream::Actor(2002),
                1,
                1,
                datagram,
                datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        PlayerState player = MovementState(MakeMovement());
        player.actorId = 1001;
        CHECK(test, fable::multiplayer::transport_codec::EncodePlayerPacket(
            player, 1001, oldHostNonce, datagram, datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        TransportMessage entity = EntityMovementTransportMessage(
            MakeEntityMovement(9100, 1, 1.0f), oldHostNonce);
        entity.sourceActorId = 1001;
        CHECK(test,
            fable::multiplayer::transport_codec::EncodeUnreliablePacket(
                entity, oldHostNonce, datagram, datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        // Give the worker a full discovery interval to process every forged
        // pre-latch datagram before the valid challenge is introduced.
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        TransportMessage transportMessage;
        PlayerState remotePlayer;
        CHECK(test, !guest.TryConsumeReliable(transportMessage));
        CHECK(test, !guest.TryConsume(remotePlayer));
        CHECK(test, !guest.TryConsumeUnreliable(transportMessage));

        std::array<std::uint8_t,
            fable::multiplayer::transport_codec::PeerHelloChallengeBytes>
            validChallenge = {};
        CHECK(test,
            fable::multiplayer::transport_codec::EncodePeerHelloChallenge(
                guest.ConnectionNonce(), 0xB002, validChallenge));
        CHECK(test, fable::multiplayer::transport_codec::EncodePeerHello(
            1001,
            oldHostNonce,
            validChallenge.data(),
            validChallenge.size(),
            datagram,
            datagramSize));
        CHECK(test, SendDatagram(
            server, guestEndpoint, datagram, datagramSize));

        const bool handshakeLatched = WaitFor([&guest]
        {
            return guest.PeerSetRevision() != 0;
        });
        CHECK(test, handshakeLatched);
        CHECK(test, guest.SubmitReliable(
            fable::multiplayer::reliable_stream::Actor(2002),
            fable::multiplayer::protocol::PacketType::PlayerActorState,
            localActorPayload.data(), localActorPayloadSize));

        bool sawQueuedReliable = false;
        for (int attempt = 0;
             attempt < 8 && handshakeLatched && !sawQueuedReliable;
             ++attempt)
        {
            sockaddr_in sender = {};
            int senderSize = sizeof(sender);
            const int byteCount = recvfrom(
                server,
                reinterpret_cast<char*>(datagram.data()),
                static_cast<int>(datagram.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderSize);
            if (byteCount <= 0)
            {
                continue;
            }
            fable::multiplayer::protocol::PacketView packet;
            if (fable::multiplayer::protocol::DecodePacket(
                    datagram.data(),
                    static_cast<std::size_t>(byteCount),
                    packet))
            {
                if (packet.payload != nullptr &&
                    (packet.envelope.type ==
                        fable::multiplayer::protocol::PacketType::
                            ReliableFragment ||
                        packet.envelope.type ==
                            fable::multiplayer::protocol::PacketType::
                                PlayerActorState) &&
                    packet.envelope.flags ==
                        fable::multiplayer::protocol::packet_flag::Reliable &&
                    packet.envelope.streamKind == static_cast<std::uint8_t>(
                        fable::multiplayer::ReliableStreamKind::Actor) &&
                    packet.envelope.streamId == 2002 &&
                    packet.envelope.sequence == 1)
                {
                    sawQueuedReliable = true;
                }
            }
        }
        CHECK(test, sawQueuedReliable);

        guest.Shutdown();
        closesocket(server);
        WSACleanup();
    }

    void TestGuestRecoversFromRestartedHostWithoutRestarting()
    {
        constexpr const char* test = "guest host-restart recovery";
        constexpr std::uint16_t port = 39205;
        constexpr std::uint64_t hostActorId = 1001;
        constexpr std::uint64_t guestActorId = 2002;
        constexpr auto leaseSpan = std::chrono::milliseconds(10'250);

        UdpPeer host;
        UdpPeer guest;
        if (!StartTransportPair(host, guest, port))
        {
            CHECK(test, false);
            host.Shutdown();
            guest.Shutdown();
            return;
        }
        const std::uint64_t oldHostNonce = host.ConnectionNonce();
        const std::uint64_t oldGuestNonce = guest.ConnectionNonce();
        const std::uint64_t guestRevisionBeforeRestart =
            guest.PeerSetRevision();

        PlayerState movement;
        movement.actorId = guestActorId;
        movement.authorityEpoch = 700;
        movement.actorGeneration = 1;
        movement.mapEpoch = 1;
        movement.mapId = kMapId;
        movement.changedProperties = Movement;
        movement.moving = true;
        movement.velocity = {1.0f, 0.0f, 0.0f};

        // Transport keepalives must preserve the peer while native game/save
        // loading has not opened a movement channel yet. Requiring gameplay
        // state here recreates the real startup race where the host retired a
        // slow-loading guest before its Hero became available.
        const auto activeDeadline =
            std::chrono::steady_clock::now() + leaseSpan;
        while (std::chrono::steady_clock::now() < activeDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        CHECK(test, !guest.HasFailed());
        CHECK(test, !host.HasFailed());
        CHECK(test, host.ConnectedPeerCount() == 1);

        host.Shutdown();

        // Keep the guest transport alive while the host is entirely absent.
        // After a full remote-silence lease it must return to discovery on its
        // own; restarting the guest would hide the lifecycle defect.
        const auto silenceDeadline =
            std::chrono::steady_clock::now() + leaseSpan;
        while (std::chrono::steady_clock::now() < silenceDeadline)
        {
            ++movement.sequence;
            movement.position.x += 0.1f;
            CHECK(test, guest.Submit(movement));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        CHECK(test, !guest.HasFailed());

        const bool guestLeaseReset = WaitFor([&guest,
            guestRevisionBeforeRestart]
        {
            return guest.PeerSetRevision() !=
                guestRevisionBeforeRestart;
        });
        CHECK(test, guestLeaseReset);
        const std::uint64_t rotatedGuestNonce = guest.ConnectionNonce();
        CHECK(test, rotatedGuestNonce != 0);
        CHECK(test, rotatedGuestNonce != oldGuestNonce);
        const std::uint64_t revisionAfterLeaseReset =
            guest.PeerSetRevision();

        // Bind the retired host endpoint before the replacement starts. The
        // guest's fresh discovery gives us its real endpoint, then a delayed
        // challenge echoing the old guest nonce must not restore that retired
        // session or block the upcoming host handshake.
        const SOCKET delayedOldHost = OpenRawUdpSocket(port);
        CHECK(test, delayedOldHost != INVALID_SOCKET);
        if (delayedOldHost != INVALID_SOCKET)
        {
            ++movement.sequence;
            movement.position.x += 0.1f;
            CHECK(test, guest.Submit(movement));
            std::array<std::uint8_t,
                fable::multiplayer::protocol::MaximumDatagramBytes>
                discovery = {};
            sockaddr_in guestEndpoint = {};
            fable::multiplayer::protocol::PacketView discoveryPacket;
            const bool capturedGuestEndpoint = ReceiveRawPacket(
                delayedOldHost,
                discovery,
                guestEndpoint,
                discoveryPacket,
                std::chrono::milliseconds(2'000));
            CHECK(test, capturedGuestEndpoint);
            if (capturedGuestEndpoint)
            {
                std::array<std::uint8_t,
                    fable::multiplayer::transport_codec::
                        PeerHelloChallengeBytes> oldChallenge = {};
                CHECK(test,
                    fable::multiplayer::transport_codec::
                        EncodePeerHelloChallenge(
                            oldGuestNonce,
                            0x0BADF00D12345678ull,
                            oldChallenge));
                std::size_t delayedChallengeSize = 0;
                CHECK(test,
                    fable::multiplayer::transport_codec::EncodePeerHello(
                        hostActorId,
                        oldHostNonce,
                        oldChallenge.data(),
                        oldChallenge.size(),
                        discovery,
                        delayedChallengeSize));
                CHECK(test, SendDatagram(
                    delayedOldHost,
                    guestEndpoint,
                    discovery,
                    delayedChallengeSize));
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
                CHECK(test, guest.PeerSetRevision() ==
                    revisionAfterLeaseReset);
                CHECK(test, guest.ConnectionNonce() ==
                    rotatedGuestNonce);
            }
            closesocket(delayedOldHost);
        }

        UdpPeer replacementHost;
        CHECK(test, replacementHost.StartHost(
            port, hostActorId, TestDiagnostics()));
        CHECK(test, replacementHost.ConnectionNonce() != 0);
        CHECK(test, replacementHost.ConnectionNonce() != oldHostNonce);

        bool recovered = false;
        const auto recoveryDeadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(5'000);
        while (!recovered &&
            std::chrono::steady_clock::now() < recoveryDeadline)
        {
            ++movement.sequence;
            movement.position.x += 0.1f;
            CHECK(test, guest.Submit(movement));
            recovered = replacementHost.ConnectedPeerCount() == 1 &&
                guest.PeerSetRevision() != guestRevisionBeforeRestart;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(test, recovered);

        TransportMessage acceptedCurrentHostMessage;
        if (recovered)
        {
            const auto currentHostActor = Construct(
                700, 1, 1, 1, kMapId, "Albion", hostActorId);
            CHECK(test, SubmitActor(replacementHost, currentHostActor));
            CHECK(test, ReceiveReliable(
                guest, acceptedCurrentHostMessage));
            CHECK(test, acceptedCurrentHostMessage.connectionNonce ==
                replacementHost.ConnectionNonce());
            CHECK(test, acceptedCurrentHostMessage.sourceActorId ==
                hostActorId);
        }

        const std::uint64_t currentHostNonce =
            replacementHost.ConnectionNonce();
        replacementHost.Shutdown();

        // Reuse the real host endpoint to inject one validly encoded packet
        // carrying the retired host nonce. Even with the next expected stream
        // sequence and current incarnation it must be fenced by connection.
        const SOCKET retiredHost = OpenRawUdpSocket(port);
        CHECK(test, retiredHost != INVALID_SOCKET);
        if (recovered && retiredHost != INVALID_SOCKET)
        {
            ++movement.sequence;
            movement.position.x += 0.1f;
            CHECK(test, guest.Submit(movement));
            std::array<std::uint8_t,
                fable::multiplayer::protocol::MaximumDatagramBytes>
                inbound = {};
            sockaddr_in guestEndpoint = {};
            fable::multiplayer::protocol::PacketView guestPacket;
            const bool capturedGuestEndpoint = ReceiveRawPacket(
                retiredHost,
                inbound,
                guestEndpoint,
                guestPacket,
                std::chrono::milliseconds(2'000));
            CHECK(test, capturedGuestEndpoint);

            const auto staleHostActor = Construct(
                700, 1, 1, 1, kMapId, "Albion", hostActorId);
            TransportMessage stale;
            CHECK(test, MakeFirstActorReliableDatagram(
                staleHostActor, stale));
            stale.connectionNonce = oldHostNonce;
            stale.streamIncarnation =
                acceptedCurrentHostMessage.streamIncarnation;
            stale.sequence = acceptedCurrentHostMessage.sequence + 1;
            std::array<std::uint8_t,
                fable::multiplayer::protocol::MaximumDatagramBytes>
                datagram = {};
            std::size_t datagramSize = 0;
            CHECK(test,
                fable::multiplayer::transport_codec::EncodeReliablePacket(
                    stale,
                    hostActorId,
                    oldHostNonce,
                    datagram,
                    datagramSize));
            if (capturedGuestEndpoint)
            {
                CHECK(test, SendDatagram(
                    retiredHost, guestEndpoint, datagram, datagramSize));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                TransportMessage rejectedOldHostMessage;
                CHECK(test,
                    !guest.TryConsumeReliable(rejectedOldHostMessage));
            }
            CHECK(test, currentHostNonce != oldHostNonce);
        }

        if (retiredHost != INVALID_SOCKET)
        {
            closesocket(retiredHost);
        }
        guest.Shutdown();
    }

    void TestSessionRecoversAfterBidirectionalPacketBlackout()
    {
        constexpr const char* test = "bidirectional packet-blackout recovery";
        constexpr std::uint16_t hostPort = 39211;
        constexpr std::uint16_t proxyPort = 39212;
        constexpr std::uint64_t hostActorId = 1001;
        constexpr std::uint64_t guestActorId = 2002;

        UdpPeer host;
        UdpPeer guest;
        UdpBlackholeProxy proxy;
        const bool started = host.StartHost(
                hostPort, hostActorId, TestDiagnostics()) &&
            proxy.Start(proxyPort, hostPort) &&
            guest.StartGuest(
                "127.0.0.1", proxyPort, guestActorId, TestDiagnostics());
        CHECK(test, started);
        if (!started)
        {
            guest.Shutdown();
            proxy.Shutdown();
            host.Shutdown();
            return;
        }

        CHECK(test, WaitFor([&host, &guest]
        {
            return host.HasPeer() && guest.HasPeer();
        }, std::chrono::milliseconds(5'000)));
        const std::uint64_t originalGuestNonce = guest.ConnectionNonce();
        const std::uint64_t originalGuestRevision = guest.PeerSetRevision();

        proxy.SetForwarding(false);
        const bool bothExpired = WaitFor([&host, &guest]
        {
            return !host.HasPeer() && !guest.HasPeer();
        }, std::chrono::milliseconds(12'500));
        CHECK(test, bothExpired);
        CHECK(test, !host.HasFailed());
        CHECK(test, !guest.HasFailed());
        CHECK(test, guest.ConnectionNonce() != 0);
        CHECK(test, guest.ConnectionNonce() != originalGuestNonce);
        CHECK(test, guest.PeerSetRevision() != originalGuestRevision);

        proxy.SetForwarding(true);
        const bool recovered = WaitFor([&host, &guest]
        {
            return host.HasPeer() && guest.HasPeer();
        }, std::chrono::milliseconds(5'000));
        CHECK(test, recovered);

        if (recovered)
        {
            const auto currentHostActor = Construct(
                700, 1, 1, 1, kMapId, "Albion", hostActorId);
            CHECK(test, SubmitActor(host, currentHostActor));
            TransportMessage reliable;
            CHECK(test, ReceiveReliable(guest, reliable));
            CHECK(test, reliable.sourceActorId == hostActorId);
            CHECK(test, reliable.connectionNonce == host.ConnectionNonce());

            PlayerState movement;
            movement.actorId = guestActorId;
            movement.authorityEpoch = 700;
            movement.actorGeneration = 1;
            movement.mapEpoch = 1;
            movement.mapId = kMapId;
            movement.sequence = 1;
            movement.changedProperties = Movement;
            movement.position = {4.0f, 5.0f, 6.0f};
            movement.velocity = {1.0f, 0.0f, 0.0f};
            movement.moving = true;
            CHECK(test, guest.Submit(movement));
            PlayerState receivedMovement;
            CHECK(test, WaitFor([&host, &receivedMovement]
            {
                return host.TryConsume(receivedMovement);
            }));
            CHECK(test, receivedMovement.actorId == guestActorId);
            CHECK(test, receivedMovement.sequence == 1);
        }

        guest.Shutdown();
        proxy.Shutdown();
        host.Shutdown();
    }

    void TestBoundedChallengeReconnectFencing()
    {
        constexpr const char* test = "bounded handshake reconnect fencing";

        // A duplicate challenge is the host retrying the same handshake, not
        // a replacement transport session. Actor lifecycle consumers key
        // their invalidation to this revision, so it must remain stable.
        fable::multiplayer::PeerSessionRegistry guestRegistry;
        constexpr std::uint64_t hostNonce = 70'001;
        constexpr std::uint64_t guestNonce = 70'002;
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            hostChallenge = {};
        CHECK(test,
            fable::multiplayer::transport_codec::EncodePeerHelloChallenge(
                guestNonce, 70'003, hostChallenge));
        bool guestSessionChanged = false;
        CHECK(test, guestRegistry.AcceptHostChallenge(
            hostNonce, guestNonce, hostChallenge, guestSessionChanged));
        CHECK(test, guestSessionChanged);
        const std::uint64_t establishedRevision = guestRegistry.Revision();
        CHECK(test, guestRegistry.AcceptHostChallenge(
            hostNonce, guestNonce, hostChallenge, guestSessionChanged));
        CHECK(test, !guestSessionChanged);
        CHECK(test, guestRegistry.Revision() == establishedRevision);

        fable::multiplayer::PeerSessionRegistry registry;
        const sockaddr_in endpoint = LoopbackEndpoint(41000);
        constexpr std::uint64_t actorId = 0xF000000000000042ull;
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            firstChallenge = {};
        std::uint64_t currentNonce = 0;
        bool allReconnectsAccepted = true;
        for (std::uint64_t reconnect = 1; reconnect <= 32; ++reconnect)
        {
            std::array<std::uint8_t,
                fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
                challenge = {};
            currentNonce = 10'000 + reconnect;
            if (!registry.IssueGuestChallenge(
                    endpoint, actorId, currentNonce, challenge))
            {
                allReconnectsAccepted = false;
                break;
            }
            if (reconnect == 1)
            {
                firstChallenge = challenge;
            }
            std::vector<
                fable::multiplayer::PeerSessionRegistry::RetiredSession>
                retired;
            if (!registry.ConfirmGuest(
                    endpoint, actorId, currentNonce, challenge, retired) ||
                !registry.ValidatePeer(
                    endpoint, actorId, currentNonce, false))
            {
                allReconnectsAccepted = false;
                break;
            }
            if (reconnect != 32)
            {
                registry.Expire(
                    (std::numeric_limits<ULONGLONG>::max)(), 1);
            }
        }
        CHECK(test, allReconnectsAccepted);

        if (allReconnectsAccepted)
        {
            std::array<std::uint8_t,
                fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
                delayedChallenge = {};
            const bool issuedDelayed = registry.IssueGuestChallenge(
                endpoint, actorId, 10'001, delayedChallenge);
            (void)issuedDelayed;
            std::vector<
                fable::multiplayer::PeerSessionRegistry::RetiredSession>
                retired;
            CHECK(test, !registry.ConfirmGuest(
                endpoint, actorId, 10'001, firstChallenge, retired));
            CHECK(test, registry.ValidatePeer(
                endpoint, actorId, currentNonce, false));
            CHECK(test, !registry.ValidatePeer(
                endpoint, actorId, 10'001, false));
        }

        fable::multiplayer::PeerSessionRegistry pendingRegistry;
        std::size_t acceptedPending = 0;
        constexpr std::size_t attempts = 1'024;
        for (std::size_t index = 0; index < attempts; ++index)
        {
            sockaddr_in candidate = LoopbackEndpoint(
                static_cast<std::uint16_t>(42000 + index % 20'000));
            candidate.sin_addr.s_addr = htonl(
                0x7F000001u + static_cast<std::uint32_t>(index));
            std::array<std::uint8_t,
                fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
                challenge = {};
            if (pendingRegistry.IssueGuestChallenge(
                    candidate,
                    actorId + 1 + index,
                    20'000 + index,
                    challenge))
            {
                ++acceptedPending;
            }
        }
        CHECK(test, acceptedPending < attempts);
        pendingRegistry.Expire(
            (std::numeric_limits<ULONGLONG>::max)(), 1);
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            afterExpiry = {};
        CHECK(test, pendingRegistry.IssueGuestChallenge(
            LoopbackEndpoint(45000), actorId + 2'000, 50'000, afterExpiry));

        // A challenge issued while the prior actor session is still leased
        // must survive the exact expiry that removes its blocker. Otherwise
        // the guest can only repeat a confirmation the host has forgotten.
        fable::multiplayer::PeerSessionRegistry blockedRegistry;
        const sockaddr_in oldEndpoint = LoopbackEndpoint(46000);
        const sockaddr_in replacementEndpoint = LoopbackEndpoint(46001);
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            oldChallenge = {};
        std::vector<fable::multiplayer::PeerSessionRegistry::RetiredSession>
            blockedRetired;
        CHECK(test, blockedRegistry.IssueGuestChallenge(
            oldEndpoint, actorId, 60'001, oldChallenge));
        CHECK(test, blockedRegistry.ConfirmGuest(
            oldEndpoint,
            actorId,
            60'001,
            oldChallenge,
            blockedRetired));
        std::array<std::uint8_t,
            fable::multiplayer::PeerSessionRegistry::ChallengeBytes>
            replacementChallenge = {};
        CHECK(test, blockedRegistry.IssueGuestChallenge(
            replacementEndpoint,
            actorId,
            60'002,
            replacementChallenge));
        blockedRetired = blockedRegistry.Expire(
            GetTickCount64() + 10'001, 10'000);
        CHECK(test, !blockedRetired.empty());
        CHECK(test, blockedRegistry.ConfirmGuest(
            replacementEndpoint,
            actorId,
            60'002,
            replacementChallenge,
            blockedRetired));
        CHECK(test, blockedRegistry.ValidatePeer(
            replacementEndpoint, actorId, 60'002, false));
    }

    void TestCodecRoundTripAndRejection()
    {
        constexpr const char* test = "codec roundtrip and rejection";
        const PlayerActorStateMessage source = Construct();
        PlayerActorStateMessage decoded;
        CHECK(test, RoundTripActor(source, decoded));
        CHECK(test, decoded.operation == source.operation);
        CHECK(test, decoded.componentFlags == source.componentFlags);
        CHECK(test, decoded.actorId == source.actorId);
        CHECK(test, decoded.authorityEpoch == source.authorityEpoch);
        CHECK(test, decoded.actorGeneration == source.actorGeneration);
        CHECK(test, decoded.mapEpoch == source.mapEpoch);
        CHECK(test, decoded.structuralRevision == source.structuralRevision);
        CHECK(test, decoded.constructionSnapshotTimeMs ==
            source.constructionSnapshotTimeMs);
        CHECK(test, decoded.componentPatchEffectiveTimeMs ==
            source.componentPatchEffectiveTimeMs);
        CHECK(test, decoded.transitionStartedAtSessionTimeMs ==
            source.transitionStartedAtSessionTimeMs);
        CHECK(test, decoded.transitionAnimationId ==
            source.transitionAnimationId);
        CHECK(test, decoded.transitionDurationMs == source.transitionDurationMs);
        CHECK(test, decoded.attachmentNotifyOffsetMs ==
            source.attachmentNotifyOffsetMs);
        CHECK(test, decoded.playerId == source.playerId);
        CHECK(test, decoded.mapName == source.mapName);
        CHECK(test, decoded.appearanceDefinition == source.appearanceDefinition);
        CHECK(test, decoded.heroMorph.Equals(source.heroMorph));
        CHECK(test, decoded.heroClothing.Equals(source.heroClothing));
        CHECK(test, decoded.heroAppearanceModifiers.Equals(
            source.heroAppearanceModifiers));
        CHECK(test, decoded.heroEquipment.Equals(source.heroEquipment));
        CHECK(test, decoded.heroBoneScales.valid == source.heroBoneScales.valid);
        CHECK(test, decoded.heroBoneScales.count == source.heroBoneScales.count);
        CHECK(test, std::fabs(decoded.heroBoneScales.entries[0].x - 1.0f) < 0.001f);
        CHECK(test, std::fabs(decoded.heroBoneScales.entries[0].y - 1.25f) < 0.001f);

        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        PlayerActorStateMessage invalid;
        CHECK(test, !EncodePlayerActorStateMessage(
            invalid, bytes.data(), bytes.size(), encodedSize));
        CHECK(test, encodedSize == 0);

        auto partial = source;
        partial.componentFlags &= ~EquipmentChanged;
        CHECK(test, !EncodePlayerActorStateMessage(
            partial, bytes.data(), bytes.size(), encodedSize));

        partial = source;
        partial.componentFlags &= ~(AppearancePresent | EquipmentPresent);
        CHECK(test, !EncodePlayerActorStateMessage(
            partial, bytes.data(), bytes.size(), encodedSize));

        CHECK(test, EncodePlayerActorStateMessage(
            source, bytes.data(), bytes.size(), encodedSize));
        // The normal hero has one materialized bone-scale entry. The compact
        // mask/triple representation fits in one conservative datagram.
        CHECK(test, encodedSize == 474);
        CHECK(test, !DecodePlayerActorStateMessage(
            bytes.data(), encodedSize - 1, decoded));

        // Wire byte 1 is the packed component-flags field. A peer must not
        // be able to decode a Construct after stripping either mandatory
        // component-presence declaration.
        bytes[1] &= static_cast<std::uint8_t>(
            ~(AppearancePresent | EquipmentPresent));
        CHECK(test, !DecodePlayerActorStateMessage(
            bytes.data(), encodedSize, decoded));

        // Bone entries are indexed by the stable skeleton order and are
        // rejected when duplicated or outside the compact mask range.
        auto duplicateBone = source;
        duplicateBone.heroBoneScales.count = 2;
        duplicateBone.heroBoneScales.entries[1] =
            duplicateBone.heroBoneScales.entries[0];
        CHECK(test, !EncodePlayerActorStateMessage(
            duplicateBone, bytes.data(), bytes.size(), encodedSize));
        auto outOfRangeBone = source;
        outOfRangeBone.heroBoneScales.entries[0].boneIndex =
            static_cast<std::uint16_t>(
                fable::game::hero_pawn::appearance::HeroBoneScaleState::
                    MaximumEntries);
        CHECK(test, !EncodePlayerActorStateMessage(
            outOfRangeBone, bytes.data(), bytes.size(), encodedSize));

        auto allBones = source;
        allBones.heroBoneScales.count =
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries;
        for (std::size_t index = 0;
             index < allBones.heroBoneScales.count; ++index)
        {
            allBones.heroBoneScales.entries[index] = {
                static_cast<std::uint16_t>(index), 1.0f, 1.25f, 0.75f};
        }
        std::size_t allBonesSize = 0;
        CHECK(test, EncodePlayerActorStateMessage(
            allBones, bytes.data(), bytes.size(), allBonesSize));
        // 453-byte fixed prefix + 15-byte mask + 120 compact XYZ triples.
        CHECK(test, allBonesSize == 1188);
        CHECK(test, allBonesSize +
                fable::multiplayer::protocol::PacketHeaderBytes >
            fable::multiplayer::protocol::MaximumDatagramBytes);
        CHECK(test, DecodePlayerActorStateMessage(
            bytes.data(), allBonesSize, decoded));
        CHECK(test, decoded.heroBoneScales.count ==
            fable::game::hero_pawn::appearance::HeroBoneScaleState::
                MaximumEntries);
        CHECK(test, decoded.heroBoneScales.entries[119].boneIndex == 119);

        auto movement = MakeMovement();
        movement.sessionTimeMs = 0xFFFF'FFF0u;
        PlayerMovementMessage decodedMovement;
        CHECK(test, RoundTripMovement(movement, decodedMovement));
        CHECK(test, decodedMovement.actorId == movement.actorId);
        CHECK(test, decodedMovement.sequence == movement.sequence);
        CHECK(test, decodedMovement.sessionTimeMs == movement.sessionTimeMs);
        CHECK(test, decodedMovement.position.x == movement.position.x);
        CHECK(test, decodedMovement.velocity.z == movement.velocity.z);
    }

    void TestSessionTimingCodec()
    {
        constexpr const char* test = "session timing codec";
        CHECK(test, fable::multiplayer::protocol::IsSessionTimeWithin(
            0x00000010u, 0xFFFFFFF0u, 32u));
        CHECK(test, !fable::multiplayer::protocol::IsSessionTimeWithin(
            0x00000010u, 0xFFFFFFF0u, 31u));
        CHECK(test, fable::multiplayer::protocol::IsSessionTimeAtOrAfter(
            0x00000010u, 0xFFFFFFF0u));

        auto actor = Construct();
        actor.constructionSnapshotTimeMs = 0xFFFF'FF00u;
        actor.transitionStartedAtSessionTimeMs = 0xFFFF'FF10u;
        actor.transitionAnimationId = 2770;
        actor.transitionDurationMs = 1'500;
        actor.attachmentNotifyOffsetMs = 500;
        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        PlayerActorStateMessage decodedActor;
        CHECK(test, EncodePlayerActorStateMessage(
            actor, bytes.data(), bytes.size(), encodedSize));
        CHECK(test, DecodePlayerActorStateMessage(
            bytes.data(), encodedSize, decodedActor));
        CHECK(test, decodedActor.transitionAnimationId == 2770);

        auto delta = actor;
        delta.operation = PlayerActorStateOperation::ComponentDelta;
        delta.componentFlags = EquipmentChanged | EquipmentPresent;
        delta.constructionSnapshotTimeMs = SessionTimeUnset;
        delta.componentPatchEffectiveTimeMs = 0x00000020u;
        CHECK(test, EncodePlayerActorStateMessage(
            delta, bytes.data(), bytes.size(), encodedSize));
        CHECK(test, DecodePlayerActorStateMessage(
            bytes.data(), encodedSize, decodedActor));
        CHECK(test, decodedActor.componentPatchEffectiveTimeMs == 0x20u);

        auto invalidActor = actor;
        invalidActor.attachmentNotifyOffsetMs = 1'501;
        CHECK(test, !EncodePlayerActorStateMessage(
            invalidActor, bytes.data(), bytes.size(), encodedSize));
        invalidActor = actor;
        invalidActor.transitionStartedAtSessionTimeMs = SessionTimeUnset;
        CHECK(test, !EncodePlayerActorStateMessage(
            invalidActor, bytes.data(), bytes.size(), encodedSize));
        invalidActor = actor;
        invalidActor.componentPatchEffectiveTimeMs = 0x20u;
        CHECK(test, !EncodePlayerActorStateMessage(
            invalidActor, bytes.data(), bytes.size(), encodedSize));

        PlayerActionMessage action;
        action.phase = PlayerActionPhase::Perform;
        action.kind = PlayerActionKind::RangedAim;
        action.ownerActorId = 1001;
        action.actionId = 7;
        action.authorityEpoch = 3;
        action.actorGeneration = 4;
        action.mapEpoch = 5;
        action.weaponFamily = fable::game::creature::equipment::
            CreatureWeaponFamily::Ranged;
        action.requiredWeapons.rangedDefinitionIndex = 5649;
        action.requiredRangedAttachmentSlot = 18;
        action.resolvedAnimationId = 2770;
        action.startedAtSessionTimeMs = 0xFFFF'FF00u;
        action.expectedDurationMs = 12'000;
        action.presentationRevision = 4;
        action.mapName = "FrescoDome";
        action.semanticName = "RangedAimStart";
        action.resolvedActionType = "CCreatureAction_HeroLoadRangedWeapon";
        CHECK(test, EncodePlayerActionMessage(
            action, bytes.data(), bytes.size(), encodedSize));
        PlayerActionMessage decodedAction;
        CHECK(test, DecodePlayerActionMessage(
            bytes.data(), encodedSize, decodedAction));
        CHECK(test, decodedAction.startedAtSessionTimeMs ==
            action.startedAtSessionTimeMs);
        CHECK(test, decodedAction.expectedDurationMs ==
            action.expectedDurationMs);
        CHECK(test, decodedAction.presentationRevision ==
            action.presentationRevision);

        action.expectedDurationMs = 120'001;
        CHECK(test, !EncodePlayerActionMessage(
            action, bytes.data(), bytes.size(), encodedSize));
        action.expectedDurationMs = 0;
        action.startedAtSessionTimeMs = SessionTimeUnset;
        action.presentationRevision = 1;
        CHECK(test, !EncodePlayerActionMessage(
            action, bytes.data(), bytes.size(), encodedSize));
    }

    void TestLostConstructAndRetransmission()
    {
        constexpr const char* test = "lost construct and retransmission";
        RemotePlayerChannels channels;
        PlayerMovementMessage movement = MakeMovement();
        PlayerMovementMessage decodedMovement;
        CHECK(test, RoundTripMovement(movement, decodedMovement));
        CHECK(test, !channels.Apply(MovementState(decodedMovement), 1));

        const auto construct = Construct();
        CHECK(test, channels.ApplyActorState(construct, 2));
        CHECK(test, channels.Apply(MovementState(decodedMovement), 3));
        CHECK(test, channels.Find(kActorId) != nullptr);
        CHECK(test, channels.Find(kActorId)->sequence == 1);
    }

    void TestLostMapPreparationRequestRetriesUntilAcknowledged()
    {
        constexpr const char* test =
            "lost map preparation request retries until acknowledged";
        MapPreparationRetryState retry;

        CHECK(test, !retry.IsPending());
        CHECK(test, retry.IsDue(1000));

        // The first Prepare is lost.  The request remains pending, but the
        // construction loop cannot enqueue another copy during the backoff.
        retry.RecordAttempt(1000);
        CHECK(test, retry.IsPending());
        CHECK(test, retry.AttemptCount() == 1);
        CHECK(test, !retry.IsDue(1249));
        CHECK(test, retry.IsDue(1250));

        // A second identical Prepare is now allowed.  Its backoff grows in a
        // bounded way, and a later Prepared acknowledgement clears it.
        retry.RecordAttempt(1250);
        CHECK(test, retry.AttemptCount() == 2);
        CHECK(test, !retry.IsDue(1749));
        CHECK(test, retry.IsDue(1750));
        retry.Acknowledge();
        CHECK(test, !retry.IsPending());
        CHECK(test, retry.IsDue(1750));
    }

    void TestRemoteEntityHealthProtectionFencesLifecycle()
    {
        constexpr const char* test =
            "remote entity health protection fences lifecycle";
        fable::multiplayer::entities::WorldEntityRecord world;
        world.thingUid = 7001;
        world.generation = 3;
        world.mapEpoch = 9;
        world.simulationOwnerActorId = 2002;
        world.available = true;
        world.live = true;
        world.creature = true;
        fable::multiplayer::entities::LiveEntityRecord live;
        live.thingUid = world.thingUid;
        live.creature = true;
        live.thing = reinterpret_cast<void*>(0x1234);

        CHECK(test, fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, false));
        CHECK(test, !fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, true));

        // Retirement, generation replacement, and map handoff all fence the
        // old native binding before EntityVitals can retain a new one.
        world.available = false;
        CHECK(test, !fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, false));
        world.available = true;
        world.generation = 4;
        CHECK(test, fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, false));
        world.mapEpoch = 10;
        CHECK(test, fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, false));
        world.simulationOwnerActorId = 1001;
        CHECK(test, !fable::multiplayer::replication::
            IsRemoteEntityHealthReplica(world, live, 1001, false));
    }

    void TestReplicaHealthProtectionRevisionGatesUnchangedTicks()
    {
        constexpr const char* test =
            "replica health protection revision gates unchanged ticks";
        fable::multiplayer::replication::ReplicaHealthProtectionRevision
            revision;
        CHECK(test, revision.NeedsReconcile(1, 1));
        revision.Commit(1, 1);
        CHECK(test, !revision.NeedsReconcile(1, 1));
        CHECK(test, revision.NeedsReconcile(2, 1));
        revision.Commit(2, 1);
        CHECK(test, revision.NeedsReconcile(2, 3));
        revision.Invalidate();
        CHECK(test, revision.NeedsReconcile(2, 3));
    }

    void TestRangedFireActionEntersOrderedPlayerStream()
    {
        constexpr const char* test =
            "ranged fire action enters ordered player stream";
        fable::multiplayer::replication::PlayerActionEventQueue queue;
        queue.SetAccepting(true);
        fable::game::creature::actions::CreatureActionLifecycleEvent fire;
        fire.phase = fable::game::creature::actions::
            CreatureActionLifecyclePhase::Submitted;
        fire.accepted = true;
        strcpy_s(
            fire.actionType,
            "CCreatureAction_FireMissileWeapon");
        queue.Enqueue(fire);

        fable::game::creature::actions::CreatureActionLifecycleEvent reload;
        reload.phase = fable::game::creature::actions::
            CreatureActionLifecyclePhase::Submitted;
        reload.accepted = true;
        strcpy_s(
            reload.actionType,
            "CCreatureAction_HeroLoadRangedWeapon");
        queue.Enqueue(reload);

        auto rejectedReload = reload;
        rejectedReload.accepted = false;
        queue.Enqueue(rejectedReload);

        fable::multiplayer::replication::PlayerActionEventQueue::Batch batch;
        queue.Drain(batch);
        CHECK(test, batch.actions.size() == 2);
        if (!batch.actions.empty())
        {
            CHECK(test, std::strstr(
                batch.actions.front().actionType,
                "FireMissileWeapon") != nullptr);
        }
        if (batch.actions.size() == 2)
        {
            CHECK(test, std::strstr(
                batch.actions.back().actionType,
                "HeroLoadRangedWeapon") != nullptr);
            CHECK(test, batch.actions.back().accepted);
        }

        fable::game::creature::locomotion::CreatureModeSourceEvent aimEnd;
        aimEnd.owner = reinterpret_cast<void*>(0x1000);
        aimEnd.source = 25;
        aimEnd.observedAt = 100;
        aimEnd.added = false;
        aimEnd.changed = true;
        queue.Enqueue(aimEnd);
        queue.Drain(batch);
        CHECK(test, batch.modeSources.size() == 1);
        CHECK(test, !batch.modeSources.front().added);

        aimEnd.added = true;
        queue.Enqueue(aimEnd);
        queue.Drain(batch);
        CHECK(test, batch.modeSources.empty());
    }

    void TestRangedAimUsesDedicatedOrderedActionKind()
    {
        constexpr const char* test =
            "ranged aim uses dedicated ordered action kind";
        fable::multiplayer::protocol::PlayerActionMessage aim;
        aim.phase = fable::multiplayer::protocol::PlayerActionPhase::Perform;
        aim.kind = fable::multiplayer::protocol::PlayerActionKind::RangedAim;
        aim.ownerActorId = 1001;
        aim.actionId = 7;
        aim.authorityEpoch = 3;
        aim.actorGeneration = 4;
        aim.mapEpoch = 5;
        aim.weaponFamily = fable::game::creature::equipment::
            CreatureWeaponFamily::Ranged;
        aim.requiredWeapons.rangedDefinitionIndex = 5649;
        aim.requiredRangedAttachmentSlot = 18;
        aim.resolvedAnimationId = 2770;
        aim.mapName = "FrescoDome";
        aim.semanticName = "RangedAimStart";
        aim.resolvedActionType =
            "CCreatureAction_HeroLoadRangedWeapon";

        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            aim, bytes.data(), bytes.size(), encodedSize));
        fable::multiplayer::protocol::PlayerActionMessage decoded;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            bytes.data(), encodedSize, decoded));
        CHECK(test, decoded.kind ==
            fable::multiplayer::protocol::PlayerActionKind::RangedAim);
        CHECK(test, decoded.resolvedAnimationId == 2770);
        CHECK(test, decoded.requiredWeapons.rangedDefinitionIndex == 5649);

        aim.abilityId = 1101;
        CHECK(test, !fable::multiplayer::protocol::EncodePlayerActionMessage(
            aim, bytes.data(), bytes.size(), encodedSize));
    }

    void TestRangedAimEndUsesDedicatedOrderedActionKind()
    {
        constexpr const char* test =
            "ranged aim end uses dedicated ordered action kind";
        fable::multiplayer::protocol::PlayerActionMessage aimEnd;
        aimEnd.phase = fable::multiplayer::protocol::PlayerActionPhase::Perform;
        aimEnd.kind =
            fable::multiplayer::protocol::PlayerActionKind::RangedAimEnd;
        aimEnd.ownerActorId = 1001;
        aimEnd.actionId = 8;
        aimEnd.authorityEpoch = 3;
        aimEnd.actorGeneration = 4;
        aimEnd.mapEpoch = 5;
        aimEnd.mapName = "FrescoDome";
        aimEnd.semanticName = "RangedAimEnd";

        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            aimEnd, bytes.data(), bytes.size(), encodedSize));
        fable::multiplayer::protocol::PlayerActionMessage decoded;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            bytes.data(), encodedSize, decoded));
        CHECK(test, decoded.kind ==
            fable::multiplayer::protocol::PlayerActionKind::RangedAimEnd);
        CHECK(test, decoded.resolvedAnimationId == 0);

        aimEnd.resolvedAnimationId = 2770;
        CHECK(test, !fable::multiplayer::protocol::EncodePlayerActionMessage(
            aimEnd, bytes.data(), bytes.size(), encodedSize));
    }

    void TestExpressionUsesSemanticOrderedActionKind()
    {
        constexpr const char* test =
            "expression uses semantic ordered action kind";
        fable::multiplayer::protocol::PlayerActionMessage expression;
        expression.phase =
            fable::multiplayer::protocol::PlayerActionPhase::Perform;
        expression.kind =
            fable::multiplayer::protocol::PlayerActionKind::Expression;
        expression.ownerActorId = 1001;
        expression.actionId = 9;
        expression.authorityEpoch = 3;
        expression.actorGeneration = 4;
        expression.mapEpoch = 5;
        expression.resolvedAnimationId = 2801;
        expression.expressionDurationTicks = 12;
        expression.expressionTriggerTicks = 0;
        expression.mapName = "FrescoDome";
        expression.semanticName = "EXPRESSION_FART";
        expression.resolvedActionType =
            "CCreatureAction_PerformExpression";

        std::array<std::uint8_t, 1472> bytes = {};
        std::size_t encodedSize = 0;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            expression, bytes.data(), bytes.size(), encodedSize));
        fable::multiplayer::protocol::PlayerActionMessage decoded;
        CHECK(test, fable::multiplayer::protocol::DecodePlayerActionMessage(
            bytes.data(), encodedSize, decoded));
        CHECK(test, decoded.kind ==
            fable::multiplayer::protocol::PlayerActionKind::Expression);
        CHECK(test, decoded.semanticName == "EXPRESSION_FART");
        CHECK(test, decoded.expressionDurationTicks == 12);
        CHECK(test, decoded.expressionTriggerTicks == 0);
        CHECK(test, decoded.targetThingUid == 0);

        expression.targetThingUid = 0x1234;
        CHECK(test, fable::multiplayer::protocol::EncodePlayerActionMessage(
            expression, bytes.data(), bytes.size(), encodedSize));
        expression.targetPlayerActorId = 1002;
        CHECK(test, !fable::multiplayer::protocol::EncodePlayerActionMessage(
            expression, bytes.data(), bytes.size(), encodedSize));
    }

    void TestPlayerDeathUsesPostNativeHealthOutcome()
    {
        constexpr const char* test =
            "player death uses post-native health outcome";
        using fable::multiplayer::combat::ClassifyPlayerDeath;
        using fable::multiplayer::combat::PlayerDeathOutcome;
        CHECK(test, ClassifyPlayerDeath(25.0f, 100.0f) ==
            PlayerDeathOutcome::Alive);
        CHECK(test, ClassifyPlayerDeath(0.0f, 100.0f) ==
            PlayerDeathOutcome::GuildRespawnRequired);
        CHECK(test, ClassifyPlayerDeath(
            std::numeric_limits<float>::quiet_NaN(), 100.0f) ==
            PlayerDeathOutcome::Invalid);
    }

    void TestPlayerActorLifecycleReducer()
    {
        constexpr const char* test = "player actor lifecycle reducer";
        const auto construct = Construct();
        PlayerActorStateMessage next;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            nullptr, construct, next) ==
            PlayerActorLifecycleReduction::Applied);
        CHECK(test, next.actorId == construct.actorId);
        CHECK(test, next.heroEquipment.meleeDefinitionIndex ==
            construct.heroEquipment.meleeDefinitionIndex);

        auto deltaWithoutBaseline = construct;
        deltaWithoutBaseline.operation =
            PlayerActorStateOperation::ComponentDelta;
        deltaWithoutBaseline.structuralRevision = 2;
        deltaWithoutBaseline.componentFlags =
            EquipmentChanged | EquipmentPresent;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            nullptr, deltaWithoutBaseline, next) ==
            PlayerActorLifecycleReduction::Rejected);

        auto unknown = construct;
        unknown.operation = static_cast<PlayerActorStateOperation>(255);
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            nullptr, unknown, next) ==
            PlayerActorLifecycleReduction::Rejected);
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, unknown, next) ==
            PlayerActorLifecycleReduction::Rejected);

        auto transitionWithoutBaseline = construct;
        transitionWithoutBaseline.operation =
            PlayerActorStateOperation::MapTransition;
        transitionWithoutBaseline.structuralRevision = 2;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            nullptr, transitionWithoutBaseline, next) ==
            PlayerActorLifecycleReduction::Ignored);
        auto retireWithoutBaseline = construct;
        retireWithoutBaseline.operation = PlayerActorStateOperation::Retire;
        retireWithoutBaseline.structuralRevision = 2;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            nullptr, retireWithoutBaseline, next) ==
            PlayerActorLifecycleReduction::Ignored);

        auto delta = construct;
        delta.operation = PlayerActorStateOperation::ComponentDelta;
        delta.structuralRevision = 2;
        delta.componentFlags = EquipmentChanged | EquipmentPresent;
        delta.heroEquipment.meleeDefinitionIndex = 3002;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, delta, next) ==
            PlayerActorLifecycleReduction::Applied);
        CHECK(test, next.heroMorph.strength == construct.heroMorph.strength);
        CHECK(test, next.heroEquipment.meleeDefinitionIndex == 3002);
        CHECK(test, next.structuralRevision == 2);

        auto stale = delta;
        stale.structuralRevision = 1;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, stale, next) ==
            PlayerActorLifecycleReduction::Ignored);

        auto sameIncarnationConstruct = construct;
        sameIncarnationConstruct.structuralRevision = 3;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, sameIncarnationConstruct, next) ==
            PlayerActorLifecycleReduction::Rejected);

        auto transition = construct;
        transition.operation = PlayerActorStateOperation::MapTransition;
        transition.structuralRevision = 4;
        transition.actorGeneration = 2;
        transition.mapEpoch = 2;
        transition.mapId = 200;
        transition.mapName = "NewMap";
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, transition, next) ==
            PlayerActorLifecycleReduction::Applied);
        CHECK(test, next.heroEquipment.meleeDefinitionIndex ==
            construct.heroEquipment.meleeDefinitionIndex);
        CHECK(test, next.mapName == "NewMap");
        const auto transitionCurrent = next;

        auto olderConstruct = construct;
        olderConstruct.actorGeneration = 1;
        olderConstruct.mapEpoch = 9;
        olderConstruct.structuralRevision = 7;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &transitionCurrent, olderConstruct, next) ==
            PlayerActorLifecycleReduction::Rejected);

        auto wrongAuthorityTransition = transition;
        wrongAuthorityTransition.authorityEpoch = 8;
        wrongAuthorityTransition.structuralRevision = 5;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &construct, wrongAuthorityTransition, next) ==
            PlayerActorLifecycleReduction::Rejected);

        auto retire = transition;
        retire.operation = PlayerActorStateOperation::Retire;
        retire.structuralRevision = 6;
        PlayerActorStateMessage retired;
        CHECK(test, PlayerActorLifecycleReducer::Reduce(
            &transitionCurrent, retire, retired) ==
            PlayerActorLifecycleReduction::Applied);
        CHECK(test, retired.operation == PlayerActorStateOperation::Retire);
        CHECK(test, retired.transitionAnimationId == 0);
    }

    void TestStaleDeltaDoesNotOverwrite()
    {
        constexpr const char* test = "stale component delta";
        RemotePlayerChannels channels;
        const auto construct = Construct();
        auto earlyDelta = construct;
        earlyDelta.operation = PlayerActorStateOperation::ComponentDelta;
        earlyDelta.structuralRevision = 2;
        earlyDelta.heroMorph.strength = 0.8f;
        CHECK(test, !channels.ApplyActorState(earlyDelta, 0));
        CHECK(test, channels.ApplyActorState(construct, 1));

        auto newer = construct;
        newer.operation = PlayerActorStateOperation::ComponentDelta;
        newer.structuralRevision = 3;
        newer.heroMorph.strength = 0.9f;
        CHECK(test, channels.ApplyActorState(newer, 2));
        CHECK(test, channels.Find(kActorId)->heroMorph.strength == 0.9f);

        auto stale = newer;
        stale.structuralRevision = 2;
        stale.heroMorph.strength = 0.1f;
        CHECK(test, !channels.ApplyActorState(stale, 3));
        CHECK(test, channels.Find(kActorId)->heroMorph.strength == 0.9f);
        CHECK(test, channels.FindLifecycle(kActorId)->structuralRevision == 3);
    }

    void TestNewAuthorityRequiresNewIncarnation()
    {
        constexpr const char* test = "new authority incarnation";
        RemotePlayerChannels channels;
        CHECK(test, channels.ApplyActorState(Construct(), 1));
        CHECK(test, channels.ApplyActorState(Construct(8, 2, 1, 1), 2));
        CHECK(test, channels.Find(kActorId)->authorityEpoch == 8);
        CHECK(test, channels.Find(kActorId)->actorGeneration == 2);

        auto staleConstruct = Construct(9, 1, 9, 3);
        CHECK(test, !channels.ApplyActorState(staleConstruct, 3));
        CHECK(test, channels.Find(kActorId)->authorityEpoch == 8);
        CHECK(test, channels.Find(kActorId)->actorGeneration == 2);

        const auto oldMovement = MakeMovement(1, 7, 1, 1, kMapId);
        CHECK(test, !channels.Apply(MovementState(oldMovement), 3));
    }

    void TestRetirePreventsResurrection()
    {
        constexpr const char* test = "retire prevents resurrection";
        RemotePlayerChannels channels;
        const auto construct = Construct();
        CHECK(test, channels.ApplyActorState(construct, 1));
        auto retire = construct;
        retire.operation = PlayerActorStateOperation::Retire;
        retire.componentFlags = 0;
        retire.structuralRevision = 2;
        CHECK(test, channels.ApplyActorState(retire, 2));
        CHECK(test, channels.Size() == 0);
        CHECK(test, !channels.Apply(MovementState(MakeMovement()), 3));

        auto staleDelta = construct;
        staleDelta.operation = PlayerActorStateOperation::ComponentDelta;
        staleDelta.structuralRevision = 2;
        CHECK(test, !channels.ApplyActorState(staleDelta, 4));
        CHECK(test, channels.Size() == 0);
    }

    void TestLateJoinConstructIdempotence()
    {
        constexpr const char* test = "late join construct idempotence";
        RemotePlayerChannels channels;
        const auto construct = Construct();
        CHECK(test, channels.ApplyActorState(construct, 10));
        const auto* first = channels.Find(kActorId);
        CHECK(test, first != nullptr);
        CHECK(test, channels.ApplyActorState(construct, 11));
        CHECK(test, channels.Size() == 1);
        const auto* second = channels.Find(kActorId);
        CHECK(test, second != nullptr);
        CHECK(test, second->position.x == first->position.x);
        CHECK(test, channels.FindLifecycle(kActorId)->structuralRevision == 1);
    }

    void TestMapHandoff()
    {
        constexpr const char* test = "map handoff";
        RemotePlayerChannels channels;
        const auto oldConstruct = Construct();
        CHECK(test, channels.ApplyActorState(oldConstruct, 1));
        auto retire = oldConstruct;
        retire.operation = PlayerActorStateOperation::Retire;
        retire.componentFlags = 0;
        retire.structuralRevision = 2;
        CHECK(test, channels.ApplyActorState(retire, 2));

        const auto newConstruct = Construct(
            kAuthorityEpoch, 2, 2, 1, 200, "NewMap");
        CHECK(test, channels.ApplyActorState(newConstruct, 3));
        CHECK(test, channels.FindLifecycle(kActorId)->actorGeneration == 2);
        CHECK(test, channels.FindLifecycle(kActorId)->mapEpoch == 2);
        CHECK(test, channels.Find(kActorId)->mapName == "NewMap");
        CHECK(test, !channels.Apply(
            MovementState(MakeMovement(2, kAuthorityEpoch, 1, 1, kMapId)), 4));
        CHECK(test, channels.Apply(
            MovementState(MakeMovement(1, kAuthorityEpoch, 2, 2, 200)), 5));
    }

    void TestActorLifecycleAdmissionLimit()
    {
        constexpr const char* test = "bounded actor lifecycle admission";
        RemotePlayerChannels channels;
        for (std::size_t index = 0;
             index < fable::multiplayer::replication::
                 player_actor_lifecycle::MaxTrackedActors;
             ++index)
        {
            const auto construct = Construct(
                kAuthorityEpoch,
                kGeneration,
                kMapEpoch,
                1,
                kMapId,
                "Albion",
                10'000 + index);
            CHECK(test, channels.ApplyActorState(construct, 1, 55));
        }
        CHECK(test, channels.Size() ==
            fable::multiplayer::replication::player_actor_lifecycle::
                MaxTrackedActors);
        CHECK(test, !channels.ApplyActorState(
            Construct(kAuthorityEpoch, kGeneration, kMapEpoch, 1, kMapId,
                "Albion", 99'999),
            2,
            55));

        auto retire = Construct(
            kAuthorityEpoch, kGeneration, kMapEpoch, 2, kMapId, "Albion", 10'000);
        retire.operation = PlayerActorStateOperation::Retire;
        retire.componentFlags = 0;
        PlayerActorLifecycleReducer::ClearStructuralTiming(retire);
        CHECK(test, channels.ApplyActorState(retire, 3, 55));
        CHECK(test, channels.Size() + 1 ==
            fable::multiplayer::replication::player_actor_lifecycle::
                MaxTrackedActors);
        CHECK(test, channels.ApplyActorState(
            Construct(kAuthorityEpoch, kGeneration, kMapEpoch, 1, kMapId,
                "Albion", 99'999),
            4,
            77));
        CHECK(test, channels.Size() ==
            fable::multiplayer::replication::player_actor_lifecycle::
                MaxTrackedActors);
    }

    void TestActorLifecyclePublicationDoesNotCrossBoundary()
    {
        constexpr const char* test =
            "actor lifecycle publication structural boundary";
        fable::multiplayer::replication::PlayerActorStatePublicationQueue queue;
        queue.Initialize(TestDiagnostics());
        auto delta = Construct();
        delta.operation = PlayerActorStateOperation::ComponentDelta;
        delta.componentFlags = AppearanceChanged | AppearancePresent;
        delta.structuralRevision = 2;
        CHECK(test, queue.Enqueue(
            delta, &PlayerActorLifecycleReducer::CoalesceDelta));
        auto construct = Construct();
        construct.structuralRevision = 3;
        CHECK(test, queue.Append(construct));
        auto newerDelta = delta;
        newerDelta.structuralRevision = 4;
        newerDelta.heroMorph.strength = 0.9f;
        CHECK(test, queue.Enqueue(
            newerDelta, &PlayerActorLifecycleReducer::CoalesceDelta));
        CHECK(test, queue.Size() == 3);
    }
}

int main()
{
    TestCodecRoundTripAndRejection();
    TestSessionTimingCodec();
    TestLostConstructAndRetransmission();
    TestLostMapPreparationRequestRetriesUntilAcknowledged();
    TestRemoteEntityHealthProtectionFencesLifecycle();
    TestReplicaHealthProtectionRevisionGatesUnchangedTicks();
    TestRangedFireActionEntersOrderedPlayerStream();
    TestRangedAimUsesDedicatedOrderedActionKind();
    TestRangedAimEndUsesDedicatedOrderedActionKind();
    TestExpressionUsesSemanticOrderedActionKind();
    TestPlayerDeathUsesPostNativeHealthOutcome();
    TestPlayerActorLifecycleReducer();
    TestStaleDeltaDoesNotOverwrite();
    TestNewAuthorityRequiresNewIncarnation();
    TestRetirePreventsResurrection();
    TestLateJoinConstructIdempotence();
    TestMapHandoff();
    TestActorLifecycleAdmissionLimit();
    TestActorLifecyclePublicationDoesNotCrossBoundary();
    TestReliableActorTransportLifecycle();
    TestGuestMovementKeepaliveUsesLastValidSnapshot();
    TestReliableControlIncarnationAndAcknowledgementRoundTrip();
    TestReliableReconnectRequiresFreshBaseline();
    TestActorStateServiceFencingAndReadiness();
    TestLateJoinReceivesCompleteActorBaseline();
    TestSameMapHeroRebindReopensActorLifecycle();
    TestDelayedRetireCannotEraseReplacementSession();
    TestMandatoryComponentRemovalIsRejected();
    TestConstructionRetransmissionOrdersBaselineBeforeAction();
    TestLargeActorBaselineFragmentsReliably();
    TestQueuedActionAndVitalsAreFencedByReplacementSession();
    TestActionQueueClearsAcrossRetirementAndMapHandoff();
    TestRapidActionsKeepOnlyLatestCurrentPresentation();
    TestTimedPresentationReplayPolicy();
    TestActionAndVitalsProducersStayFairUnderActorBackpressure();
    TestReliableActorStreamsAreIndependent();
    TestHostFanoutOwnsEveryPeerBeforeReportingSuccess();
    TestHostLogicalStreamsStayFairUnderBackpressure();
    TestDelayedAcknowledgementCannotDrainRecreatedStream();
    TestIncarnationChurnRejectsReclaimedDelayedData();
    TestIndependentStreamsMayArriveOutOfIncarnationOrder();
    TestFullWidthReliableStreamIdentityAndValidation();
    TestWorldLifecycleTransactionStream();
    TestReliableStreamMetadataIsBoundedAndRetired();
    TestActorLifecycleProducerFairnessUnderBackpressure();
    TestEntityMovementIngressKeepsLatestPerSubject();
    TestGuestDropsTrafficBeforeHandshakeLatch();
    TestGuestRecoversFromRestartedHostWithoutRestarting();
    TestSessionRecoversAfterBidirectionalPacketBlackout();
    TestBoundedChallengeReconnectFencing();
    const auto runFocused = [](const char* const name, const int result)
    {
        if (result != 0)
        {
            std::cerr << name << ": " << result << " failure(s)\n";
        }
        return result;
    };
    failures += runFocused(
        "combat hit replication", RunCombatHitReplicationTests());
    failures += runFocused("code patch", RunCodePatchTests());
    failures += runFocused("session clock", RunSessionClockTests());
    failures += runFocused(
        "reliable stream window", RunReliableStreamWindowTests());
    failures += runFocused(
        "equipment transition timing", RunEquipmentTransitionTimingTests());

    if (failures != 0)
    {
        std::cerr << failures << " player replication test assertion(s) failed\n";
        return 1;
    }
    std::cout << "Player replication tests passed\n";
    return 0;
}
