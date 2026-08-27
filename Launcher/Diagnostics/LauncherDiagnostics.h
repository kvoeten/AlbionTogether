#pragma once

#include "FirewallManager.h"
#include "GameCompatibility.h"
#include "NetworkDiagnostics.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::diagnostics
{
    struct LauncherDiagnosticsReport final
    {
        GameCompatibilityResult game;
        FirewallResult firewall;
        HostReachabilityResult host;
        std::vector<std::wstring> localAddresses;
    };

    [[nodiscard]] LauncherDiagnosticsReport RunLauncherDiagnostics(
        const std::filesystem::path& executable,
        const std::wstring& host,
        unsigned short port);
}
