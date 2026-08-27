#include "LauncherWorkflow.h"

#include "../Configuration/Options.h"
#include "../Configuration/Paths.h"

#include <cwchar>

namespace fable::launcher::application
{
    LauncherWorkflow::LauncherWorkflow()
        : launcherDirectory_(GetLauncherDirectory())
    {
    }

    LauncherSettings LauncherWorkflow::LoadSettings() const
    {
        LauncherSettings settings = settingsStore_.Load();
        if (settings.gameDirectory.empty())
        {
            settings.gameDirectory = launcherDirectory_;
        }
        return settings;
    }

    bool LauncherWorkflow::SaveSettings(
        const LauncherSettings& settings) const
    {
        return settingsStore_.Save(settings);
    }

    std::filesystem::path LauncherWorkflow::ResolveGameExecutable(
        const LauncherSettings& settings) const
    {
        Options options;
        options.gameDirectory = settings.gameDirectory;
        const std::filesystem::path configured =
            ResolveExecutable(options, launcherDirectory_);
        if (!configured.empty())
        {
            return configured;
        }

        // The unsaved UI default is the launcher folder. Keep the existing
        // development fallback when that ordinary alongside lookup is empty,
        // but never conceal an explicitly configured invalid folder.
        if (settings.gameDirectory.empty() ||
            _wcsicmp(
                AbsolutePath(settings.gameDirectory).c_str(),
                AbsolutePath(launcherDirectory_).c_str()) != 0)
        {
            return {};
        }

        options.gameDirectory.clear();
        return ResolveExecutable(options, launcherDirectory_);
    }

    std::filesystem::path LauncherWorkflow::LatestDiagnosticLog() const
    {
        const std::filesystem::path root = launcherDirectory_ / L"artifacts";
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            return {};
        }

        std::filesystem::path latest;
        std::filesystem::file_time_type latestTime =
            (std::filesystem::file_time_type::min)();
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end)
        {
            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code entryError;
            if (entry.is_regular_file(entryError))
            {
                const std::filesystem::path extension = entry.path().extension();
                if (_wcsicmp(extension.c_str(), L".log") == 0 ||
                    _wcsicmp(extension.c_str(), L".txt") == 0)
                {
                    const auto modified = entry.last_write_time(entryError);
                    if (!entryError && (latest.empty() || modified > latestTime))
                    {
                        latest = entry.path();
                        latestTime = modified;
                    }
                }
            }
            iterator.increment(error);
        }
        return latest;
    }

    diagnostics::GameCompatibilityResult LauncherWorkflow::CheckGame(
        const LauncherSettings& settings) const
    {
        return diagnostics::CheckGameCompatibility(
            ResolveGameExecutable(settings));
    }

    diagnostics::FirewallResult LauncherWorkflow::CheckFirewall(
        const unsigned short port) const
    {
        return diagnostics::CheckFirewall(port);
    }

    bool LauncherWorkflow::RepairFirewall(
        const unsigned short port,
        std::wstring& error) const
    {
        return diagnostics::RequestElevatedFirewallRepair(port, error);
    }

    bool LauncherWorkflow::Launch(
        const InteractiveRole role,
        const LauncherSettings& settings,
        std::wstring& result) const
    {
        const diagnostics::GameCompatibilityResult compatibility =
            CheckGame(settings);
        if (!compatibility.IsCompatible())
        {
            result = compatibility.detail;
            return false;
        }
        if (!SaveSettings(settings))
        {
            result = L"The launch settings could not be saved.";
            return false;
        }

        InteractiveLaunchRequest request;
        request.role = role;
        request.settings = settings;
        request.gameExecutable = compatibility.executable;
        return LaunchInteractiveGame(request, result);
    }
}
