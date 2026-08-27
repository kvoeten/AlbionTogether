#include "LauncherDiagnostics.h"

namespace fable::launcher::diagnostics
{
    LauncherDiagnosticsReport RunLauncherDiagnostics(
        const std::filesystem::path& executable,
        const std::wstring& host,
        const unsigned short port)
    {
        LauncherDiagnosticsReport report;
        report.game = CheckGameCompatibility(executable);
        report.firewall = CheckFirewall(port);
        report.localAddresses = LocalIpv4Addresses();
        report.host = TestHostReachability(host, port);
        return report;
    }
}
