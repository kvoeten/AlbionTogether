#pragma once

#include "InteractiveLaunch.h"
#include "../Configuration/LauncherSettings.h"
#include "../Diagnostics/FirewallManager.h"
#include "../Diagnostics/GameCompatibility.h"

#include <filesystem>
#include <string>

namespace fable::launcher::application
{
    class LauncherWorkflow final
    {
    public:
        LauncherWorkflow();

        [[nodiscard]] LauncherSettings LoadSettings() const;
        [[nodiscard]] bool SaveSettings(const LauncherSettings& settings) const;
        [[nodiscard]] std::filesystem::path ResolveGameExecutable(
            const LauncherSettings& settings) const;
        [[nodiscard]] std::filesystem::path LatestDiagnosticLog() const;
        [[nodiscard]] diagnostics::GameCompatibilityResult CheckGame(
            const LauncherSettings& settings) const;
        [[nodiscard]] diagnostics::FirewallResult CheckFirewall(
            unsigned short port) const;
        [[nodiscard]] bool RepairFirewall(
            unsigned short port,
            std::wstring& error) const;
        [[nodiscard]] bool Launch(
            InteractiveRole role,
            const LauncherSettings& settings,
            std::wstring& result) const;

    private:
        LauncherSettingsStore settingsStore_;
        std::filesystem::path launcherDirectory_;
    };
}
