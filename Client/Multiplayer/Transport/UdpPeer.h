#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace fable::multiplayer
{
    class UdpPeer final
    {
    public:
        UdpPeer();
        ~UdpPeer();

        UdpPeer(const UdpPeer&) = delete;
        UdpPeer& operator=(const UdpPeer&) = delete;

        bool StartHost(
            std::uint16_t port,
            const core::Diagnostics& diagnostics);
        bool StartGuest(
            const std::string& address,
            std::uint16_t port,
            const core::Diagnostics& diagnostics);
        bool Submit(const PlayerState& localUpdate);
        bool TryConsume(PlayerState& remoteUpdate);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsStarted() const noexcept;
        [[nodiscard]] bool HasPeer() const noexcept;
        [[nodiscard]] bool HasFailed() const noexcept;
        [[nodiscard]] std::size_t ConnectedPeerCount() const noexcept;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> implementation_;
    };
}
