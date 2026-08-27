#pragma once

#include <filesystem>
#include <string>

namespace fable::launcher
{
    struct LauncherSettings final
    {
        std::filesystem::path gameDirectory;
        std::wstring playerName = L"Player";
        std::wstring hostAddress;
        unsigned short port = 38171;
        bool showConsole = true;
        bool generateLogs = true;
    };

    class LauncherSettingsStore final
    {
    public:
        LauncherSettingsStore();

        [[nodiscard]] LauncherSettings Load() const;
        [[nodiscard]] bool Save(const LauncherSettings& settings) const;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

    private:
        std::filesystem::path path_;
    };
}
