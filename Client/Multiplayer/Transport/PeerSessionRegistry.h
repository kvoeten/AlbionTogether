#pragma once

#include "ReliableStreamTransport.h"

#include <WinSock2.h>

#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer
{
    // Owns endpoint identity, connection nonce fencing, and peer leases. It
    // deliberately does not perform socket I/O; UdpPeer remains the worker
    // and delegates only session decisions here.
    class PeerSessionRegistry final
    {
    public:
        static constexpr std::size_t ChallengeBytes = sizeof(std::uint64_t) * 2;
        static constexpr std::size_t PeerLimit = 64;
        static constexpr std::size_t PendingChallengeLimit = 64;
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
            std::uint64_t connectionNonce = 0;
            ULONGLONG lastReceivedAt = 0;
            ULONGLONG lastHelloSentAt = 0;
            std::unordered_map<std::uint64_t, std::uint32_t> lastSentSequence;
            std::unordered_map<std::uint64_t, ULONGLONG> lastSentAt;
            ReliableStreamTransport reliable;
            ReliableStreamTransport inboundReliable;
        };

        struct RetiredSession final
        {
            std::uint64_t actorId = 0;
            std::uint64_t connectionNonce = 0;
        };

        using PeerMap = std::unordered_map<
            EndpointKey,
            Peer,
            EndpointHash>;

        static EndpointKey Key(const sockaddr_in& endpoint) noexcept;

        void SetGuestEndpoint(const sockaddr_in& endpoint) noexcept;
        void SetLocalGuestNonce(std::uint64_t nonce) noexcept
        {
            localGuestNonce_ = nonce;
        }
        [[nodiscard]] bool IsHostEndpoint(
            const sockaddr_in& endpoint) const noexcept;
        [[nodiscard]] const sockaddr_in& GuestEndpoint() const noexcept;

        [[nodiscard]] Peer* Find(const sockaddr_in& endpoint) noexcept;
        [[nodiscard]] const Peer* Find(
            const sockaddr_in& endpoint) const noexcept;
        [[nodiscard]] PeerMap& Peers() noexcept;
        [[nodiscard]] const PeerMap& Peers() const noexcept;

        // Host-side registration accepts a new endpoint only when its nonce
        // has not been retired, and reports identities that the caller must
        // remove from its actor/state tables.
        bool RegisterGuest(
            const sockaddr_in& sender,
            std::uint64_t actorId,
            std::uint64_t connectionNonce,
            std::vector<RetiredSession>& retired);

        // A Hello only creates a bounded pending challenge. The endpoint is
        // not active until ConfirmGuest succeeds, preventing delayed old
        // Hellos from replacing a live actor session.
        bool IssueGuestChallenge(
            const sockaddr_in& sender,
            std::uint64_t actorId,
            std::uint64_t connectionNonce,
            std::array<std::uint8_t, ChallengeBytes>& challenge);
        bool ConfirmGuest(
            const sockaddr_in& sender,
            std::uint64_t actorId,
            std::uint64_t connectionNonce,
            const std::array<std::uint8_t, ChallengeBytes>& challenge,
            std::vector<RetiredSession>& retired);

        [[nodiscard]] bool ValidatePeer(
            const sockaddr_in& sender,
            std::uint64_t actorId,
            std::uint64_t connectionNonce,
            bool touch);

        // Guest-side challenge admission. The host nonce is latched only
        // after the echoed local nonce and challenge are validated.
        bool AcceptHostChallenge(
            std::uint64_t hostNonce,
            std::uint64_t localGuestNonce,
            const std::array<std::uint8_t, ChallengeBytes>& challenge,
            bool& changed);
        [[nodiscard]] bool HasGuestSession() const noexcept
        {
            return guestSessionEstablished_;
        }
        [[nodiscard]] bool HasPendingGuestConfirmation() const noexcept
        {
            return guestConfirmationPending_;
        }
        // The host's empty PeerHello acknowledges the guest's confirmation.
        // It is deliberately separate from challenge admission so a delayed
        // challenge cannot keep the guest in an endless confirmation loop.
        [[nodiscard]] bool CompleteGuestHandshake(
            std::uint64_t hostNonce) noexcept;
        void TouchGuest() noexcept;
        [[nodiscard]] bool GuestLeaseExpired(
            ULONGLONG now,
            ULONGLONG leaseMilliseconds) const noexcept;
        void ResetGuestSession() noexcept;
        [[nodiscard]] std::array<std::uint8_t, ChallengeBytes>
            GuestConfirmation() const noexcept
        {
            return guestChallenge_;
        }

        std::vector<RetiredSession> Expire(
            ULONGLONG now,
            ULONGLONG leaseMilliseconds);

        [[nodiscard]] std::uint64_t RemoteNonce() const noexcept
        {
            return remoteConnectionNonce_;
        }

        [[nodiscard]] bool HasPeers() const noexcept
        {
            return !peers_.empty();
        }

        [[nodiscard]] std::uint64_t Revision() const noexcept
        {
            return revision_;
        }

    private:
        sockaddr_in guestEndpoint_ = {};
        std::uint64_t remoteConnectionNonce_ = 0;
        std::uint64_t localGuestNonce_ = 0;
        bool guestSessionEstablished_ = false;
        bool guestConfirmationPending_ = false;
        std::array<std::uint8_t, ChallengeBytes> guestChallenge_ = {};
        ULONGLONG lastGuestTrafficAt_ = 0;
        std::uint64_t revision_ = 0;
        PeerMap peers_;

        struct PendingChallenge final
        {
            std::uint64_t actorId = 0;
            std::uint64_t connectionNonce = 0;
            std::uint64_t blockedByConnectionNonce = 0;
            std::array<std::uint8_t, ChallengeBytes> value = {};
            ULONGLONG issuedAt = 0;
        };
        std::unordered_map<EndpointKey, PendingChallenge, EndpointHash>
            pendingChallenges_;
    };
}
