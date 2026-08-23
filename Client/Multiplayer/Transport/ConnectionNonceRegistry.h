#pragma once

#include <cstdint>

namespace fable::multiplayer
{
    // Cryptographically strong local nonce generation. Session admission and
    // fencing are owned by PeerSessionRegistry's current challenge state.
    class ConnectionNonceRegistry final
    {
    public:
        [[nodiscard]] static std::uint64_t GenerateLocal() noexcept;
    };
}
