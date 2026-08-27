#pragma once

#include <string>
#include <vector>

namespace fable::launcher::diagnostics
{
    enum class HostReachabilityState
    {
        NotTested,
        AlbionTogetherDetected,
        AddressReachable,
        Unreachable,
        InvalidAddress,
        Error,
    };

    struct HostReachabilityResult final
    {
        HostReachabilityState state = HostReachabilityState::NotTested;
        std::wstring resolvedAddress;
        std::wstring detail;

        [[nodiscard]] bool IsReachable() const noexcept
        {
            return state == HostReachabilityState::AlbionTogetherDetected ||
                state == HostReachabilityState::AddressReachable;
        }
    };

    [[nodiscard]] std::vector<std::wstring> LocalIpv4Addresses();
    [[nodiscard]] HostReachabilityResult TestHostReachability(
        const std::wstring& host,
        unsigned short port,
        unsigned long timeoutMilliseconds = 1'200);
}
