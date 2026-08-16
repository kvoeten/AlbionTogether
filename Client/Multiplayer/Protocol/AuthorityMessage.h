#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer::protocol
{
    enum class AuthorityOperation : std::uint8_t
    {
        Grant = 1,
        Release = 2,
        // A player asks the host to acquire the sticky high-sim lease for its
        // current map. Only the host may turn this into a Grant.
        Request = 3,
        // A player has reached a native travel boundary. The host records an
        // atomic destination reservation and publishes the saved-map baseline,
        // but does not activate the lease until a later Request confirms that
        // source-map draining and destination occupancy completed.
        Prepare = 4,
        // Host acknowledgement for one actor's Prepare. The exact saved-map
        // baseline is ordered before this message, but no map lease changes.
        Prepared = 5,
    };

    enum class AuthorityScope : std::uint8_t
    {
        MapSimulation = 1,
        EntityAction = 2,
    };

    enum class ActionLeaseKind : std::uint8_t
    {
        None = 0,
        Ambient = 1,
        Conversation = 2,
        Combat = 3,
        QuestOrCutscene = 4,
        PrimaryAttacker = 5,
    };

    struct AuthorityMessage final
    {
        AuthorityOperation operation = AuthorityOperation::Grant;
        AuthorityScope scope = AuthorityScope::MapSimulation;
        ActionLeaseKind actionKind = ActionLeaseKind::None;
        std::uint64_t ownerActorId = 0;
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint16_t mapId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t actionEpoch = 0;
        std::uint64_t mapBaselineRevision = 0;
        std::string mapName;
    };

    bool EncodeAuthorityMessage(
        const AuthorityMessage& message,
        std::uint8_t* destination,
        std::size_t destinationCapacity,
        std::size_t& encodedSize) noexcept;
    bool DecodeAuthorityMessage(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        AuthorityMessage& message) noexcept;
}
