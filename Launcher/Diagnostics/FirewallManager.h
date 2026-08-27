#pragma once

#include <string>

namespace fable::launcher::diagnostics
{
    enum class FirewallState
    {
        Allowed,
        RuleMissing,
        FirewallDisabled,
        Error,
    };

    struct FirewallResult final
    {
        FirewallState state = FirewallState::Error;
        std::wstring detail;

        [[nodiscard]] bool AllowsTraffic() const noexcept
        {
            return state == FirewallState::Allowed ||
                state == FirewallState::FirewallDisabled;
        }
    };

    [[nodiscard]] FirewallResult CheckFirewall(unsigned short port);
    [[nodiscard]] bool InstallFirewallRule(
        unsigned short port,
        std::wstring& error);
    [[nodiscard]] bool RequestElevatedFirewallRepair(
        unsigned short port,
        std::wstring& error);
}
