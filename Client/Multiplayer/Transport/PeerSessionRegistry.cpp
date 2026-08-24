#include "PeerSessionRegistry.h"

#include "ConnectionNonceRegistry.h"
#include "PeerDatagramCodec.h"

#include <cstring>

namespace fable::multiplayer
{
    PeerSessionRegistry::EndpointKey PeerSessionRegistry::Key(
        const sockaddr_in& endpoint) noexcept
    {
        return {endpoint.sin_addr.s_addr, endpoint.sin_port};
    }

    void PeerSessionRegistry::SetGuestEndpoint(
        const sockaddr_in& endpoint) noexcept
    {
        guestEndpoint_ = endpoint;
    }

    bool PeerSessionRegistry::IsHostEndpoint(
        const sockaddr_in& endpoint) const noexcept
    {
        return endpoint.sin_addr.s_addr == guestEndpoint_.sin_addr.s_addr &&
            endpoint.sin_port == guestEndpoint_.sin_port;
    }

    const sockaddr_in& PeerSessionRegistry::GuestEndpoint() const noexcept
    {
        return guestEndpoint_;
    }

    PeerSessionRegistry::Peer* PeerSessionRegistry::Find(
        const sockaddr_in& endpoint) noexcept
    {
        const auto iterator = peers_.find(Key(endpoint));
        return iterator == peers_.end() ? nullptr : &iterator->second;
    }

    const PeerSessionRegistry::Peer* PeerSessionRegistry::Find(
        const sockaddr_in& endpoint) const noexcept
    {
        const auto iterator = peers_.find(Key(endpoint));
        return iterator == peers_.end() ? nullptr : &iterator->second;
    }

    PeerSessionRegistry::PeerMap& PeerSessionRegistry::Peers() noexcept
    {
        return peers_;
    }

    const PeerSessionRegistry::PeerMap& PeerSessionRegistry::Peers() const
        noexcept
    {
        return peers_;
    }

    bool PeerSessionRegistry::RegisterGuest(
        const sockaddr_in& sender,
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        std::vector<RetiredSession>& retired)
    {
        if (actorId == 0 || connectionNonce == 0)
        {
            return false;
        }

        bool changed = false;
        const EndpointKey endpoint = Key(sender);
        auto endpointPeer = peers_.find(endpoint);
        if (endpointPeer != peers_.end() &&
            endpointPeer->second.actorId != 0 &&
            endpointPeer->second.actorId != actorId)
        {
            retired.push_back({
                endpointPeer->second.actorId,
                endpointPeer->second.connectionNonce});
            peers_.erase(endpointPeer);
            changed = true;
        }

        for (auto iterator = peers_.begin(); iterator != peers_.end();)
        {
            if (!(iterator->first == endpoint) &&
                iterator->second.actorId == actorId)
            {
                retired.push_back({
                    iterator->second.actorId,
                    iterator->second.connectionNonce});
                iterator = peers_.erase(iterator);
                changed = true;
                continue;
            }
            ++iterator;
        }

        const auto [connectedIterator, inserted] = peers_.try_emplace(endpoint);
        Peer& connected = connectedIterator->second;
        changed = changed || inserted;
        if (!inserted && connected.connectionNonce != connectionNonce)
        {
            retired.push_back({connected.actorId, connected.connectionNonce});
            const sockaddr_in preservedEndpoint = connected.endpoint;
            const std::uint64_t preservedActorId = connected.actorId;
            connected = {};
            connected.endpoint = preservedEndpoint;
            connected.actorId = preservedActorId;
            connected.connectionNonce = connectionNonce;
            changed = true;
        }
        connected.endpoint = sender;
        connected.actorId = actorId;
        connected.connectionNonce = connectionNonce;
        connected.lastReceivedAt = GetTickCount64();
        if (changed)
        {
            ++revision_;
        }
        return true;
    }

    bool PeerSessionRegistry::ValidatePeer(
        const sockaddr_in& sender,
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        const bool touch)
    {
        Peer* peer = Find(sender);
        if (peer == nullptr || peer->actorId != actorId ||
            peer->connectionNonce != connectionNonce ||
            peer->connectionNonce == 0)
        {
            return false;
        }
        if (touch)
        {
            peer->lastReceivedAt = GetTickCount64();
        }
        return true;
    }

    bool PeerSessionRegistry::IssueGuestChallenge(
        const sockaddr_in& sender,
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        std::array<std::uint8_t, ChallengeBytes>& challenge)
    {
        if (actorId == 0 || connectionNonce == 0)
        {
            return false;
        }
        const EndpointKey endpoint = Key(sender);
        const Peer* active = Find(sender);
        if (active == nullptr &&
            peers_.size() >= PeerLimit)
        {
            return false;
        }
        const auto existing = pendingChallenges_.find(endpoint);
        if (existing == pendingChallenges_.end() &&
            pendingChallenges_.size() >= PendingChallengeLimit)
        {
            return false;
        }
        PendingChallenge& pending = pendingChallenges_[endpoint];
        if (pending.actorId != actorId ||
            pending.connectionNonce != connectionNonce)
        {
            pending.actorId = actorId;
            pending.connectionNonce = connectionNonce;
            pending.blockedByConnectionNonce = 0;
            for (const auto& [key, peer] : peers_)
            {
                if (peer.actorId == actorId)
                {
                    pending.blockedByConnectionNonce =
                        peer.connectionNonce;
                    break;
                }
            }
            const std::uint64_t challengeNonce =
                ConnectionNonceRegistry::GenerateLocal();
            if (!transport_codec::EncodePeerHelloChallenge(
                    connectionNonce,
                    challengeNonce,
                    pending.value))
            {
                return false;
            }
        }
        pending.issuedAt = GetTickCount64();
        challenge = pending.value;
        return true;
    }

    bool PeerSessionRegistry::ConfirmGuest(
        const sockaddr_in& sender,
        const std::uint64_t actorId,
        const std::uint64_t connectionNonce,
        const std::array<std::uint8_t, ChallengeBytes>& challenge,
        std::vector<RetiredSession>& retired)
    {
        const auto pending = pendingChallenges_.find(Key(sender));
        const Peer* active = Find(sender);
        for (const auto& [key, peer] : peers_)
        {
            if (!(key == Key(sender)) && peer.actorId == actorId)
            {
                return false;
            }
        }
        if (pending == pendingChallenges_.end() ||
            active != nullptr &&
                (active->actorId != actorId ||
                    active->connectionNonce != connectionNonce) ||
            pending->second.actorId != actorId ||
            pending->second.connectionNonce != connectionNonce ||
            std::memcmp(
                pending->second.value.data(),
                challenge.data(),
                challenge.size()) != 0)
        {
            return false;
        }
        pendingChallenges_.erase(pending);
        return RegisterGuest(sender, actorId, connectionNonce, retired);
    }

    bool PeerSessionRegistry::AcceptHostChallenge(
        const std::uint64_t hostNonce,
        const std::uint64_t localGuestNonce,
        const std::array<std::uint8_t, ChallengeBytes>& challenge,
        bool& changed)
    {
        std::array<std::uint8_t, ChallengeBytes> decodedChallenge = {};
        if (!transport_codec::DecodePeerHelloChallenge(
                challenge.data(), challenge.size(), decodedChallenge))
        {
            return false;
        }
        std::uint64_t echoedGuestNonce = 0;
        std::memcpy(
            &echoedGuestNonce,
            decodedChallenge.data(),
            sizeof(echoedGuestNonce));
        changed = false;
        if (hostNonce == 0 || localGuestNonce == 0 ||
            echoedGuestNonce != localGuestNonce ||
            localGuestNonce_ != 0 && localGuestNonce_ != localGuestNonce)
        {
            return false;
        }
        if (remoteConnectionNonce_ != 0 &&
            remoteConnectionNonce_ != hostNonce)
        {
            return false;
        }
        if (guestSessionEstablished_ &&
            std::memcmp(
                guestChallenge_.data(),
                challenge.data(),
                challenge.size()) != 0)
        {
            return false;
        }
        const bool challengeChanged =
            std::memcmp(
                guestChallenge_.data(),
                challenge.data(),
                challenge.size()) != 0;
        if (remoteConnectionNonce_ == 0)
        {
            remoteConnectionNonce_ = hostNonce;
            changed = true;
        }
        else if (challengeChanged)
        {
            changed = true;
        }
        localGuestNonce_ = localGuestNonce;
        guestChallenge_ = challenge;
        guestConfirmationPending_ = true;
        guestSessionEstablished_ = true;
        lastGuestTrafficAt_ = GetTickCount64();
        // The host may repeat the same challenge until it receives our
        // confirmation. That is transport retransmission, not a new peer
        // session: advancing the revision would make lifecycle consumers
        // discard an already accepted actor baseline without prompting the
        // unchanged host session to resend it.
        if (changed)
        {
            ++revision_;
        }
        return true;
    }

    bool PeerSessionRegistry::CompleteGuestHandshake(
        const std::uint64_t hostNonce) noexcept
    {
        if (!guestSessionEstablished_ || remoteConnectionNonce_ == 0 ||
            hostNonce != remoteConnectionNonce_)
        {
            return false;
        }
        guestConfirmationPending_ = false;
        lastGuestTrafficAt_ = GetTickCount64();
        return true;
    }

    void PeerSessionRegistry::TouchGuest() noexcept
    {
        if (guestSessionEstablished_)
        {
            lastGuestTrafficAt_ = GetTickCount64();
        }
    }

    bool PeerSessionRegistry::GuestLeaseExpired(
        const ULONGLONG now,
        const ULONGLONG leaseMilliseconds) const noexcept
    {
        return guestSessionEstablished_ && lastGuestTrafficAt_ != 0 &&
            now - lastGuestTrafficAt_ > leaseMilliseconds;
    }

    void PeerSessionRegistry::ResetGuestSession() noexcept
    {
        remoteConnectionNonce_ = 0;
        localGuestNonce_ = 0;
        guestSessionEstablished_ = false;
        guestConfirmationPending_ = false;
        guestChallenge_.fill(0);
        lastGuestTrafficAt_ = 0;
        ++revision_;
    }

    std::vector<PeerSessionRegistry::RetiredSession> PeerSessionRegistry::Expire(
        const ULONGLONG now,
        const ULONGLONG leaseMilliseconds)
    {
        std::vector<RetiredSession> retired;
        for (auto iterator = peers_.begin(); iterator != peers_.end();)
        {
            if (now - iterator->second.lastReceivedAt <= leaseMilliseconds)
            {
                ++iterator;
                continue;
            }
            retired.push_back({
                iterator->second.actorId,
                iterator->second.connectionNonce});
            iterator = peers_.erase(iterator);
        }
        if (!retired.empty())
        {
            ++revision_;
        }
        for (auto iterator = pendingChallenges_.begin();
             iterator != pendingChallenges_.end();)
        {
            bool actorRetired = false;
            bool blockedSessionRetired = false;
            for (const auto& session : retired)
            {
                if (session.actorId == iterator->second.actorId)
                {
                    actorRetired = true;
                    blockedSessionRetired =
                        iterator->second.blockedByConnectionNonce ==
                        session.connectionNonce;
                    break;
                }
            }
            if (blockedSessionRetired)
            {
                iterator->second.blockedByConnectionNonce = 0;
                iterator->second.issuedAt = now;
                ++iterator;
            }
            else if (!actorRetired &&
                now - iterator->second.issuedAt <= leaseMilliseconds)
            {
                ++iterator;
            }
            else
            {
                iterator = pendingChallenges_.erase(iterator);
            }
        }
        return retired;
    }
}
