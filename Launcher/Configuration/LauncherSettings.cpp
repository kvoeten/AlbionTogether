#include "LauncherSettings.h"

#include <Windows.h>
#include <ShlObj.h>

#include <cwchar>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace fable::launcher
{
    namespace
    {
        std::filesystem::path SettingsPath()
        {
            PWSTR localAppData = nullptr;
            if (FAILED(SHGetKnownFolderPath(
                    FOLDERID_LocalAppData,
                    KF_FLAG_CREATE,
                    nullptr,
                    &localAppData)))
            {
                return {};
            }
            const std::filesystem::path path =
                std::filesystem::path(localAppData) /
                L"AlbionTogether" / L"launcher.ini";
            CoTaskMemFree(localAppData);
            return path;
        }

        std::wstring ReadString(
            const std::filesystem::path& path,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            std::vector<wchar_t> value(32'768, L'\0');
            const DWORD length = GetPrivateProfileStringW(
                L"Launcher",
                key,
                fallback,
                value.data(),
                static_cast<DWORD>(value.size()),
                path.c_str());
            return std::wstring(value.data(), length);
        }

        bool ReadBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            const bool fallback)
        {
            return GetPrivateProfileIntW(
                L"Launcher", key, fallback ? 1 : 0, path.c_str()) != 0;
        }

        bool WriteString(
            const std::filesystem::path& path,
            const wchar_t* key,
            const std::wstring& value)
        {
            return WritePrivateProfileStringW(
                L"Launcher", key, value.c_str(), path.c_str()) != FALSE;
        }
    }

    LauncherSettingsStore::LauncherSettingsStore()
        : path_(SettingsPath())
    {
    }

    LauncherSettings LauncherSettingsStore::Load() const
    {
        LauncherSettings settings;
        if (path_.empty())
        {
            return settings;
        }

        settings.gameDirectory = ReadString(path_, L"GamePath", L"");
        settings.playerName = ReadString(path_, L"PlayerName", L"Player");
        settings.hostAddress = ReadString(path_, L"HostAddress", L"");
        const UINT port = GetPrivateProfileIntW(
            L"Launcher", L"Port", 38171, path_.c_str());
        settings.port = port >= 1 && port <= 65'535
            ? static_cast<unsigned short>(port)
            : static_cast<unsigned short>(38171);
        settings.showConsole = ReadBoolean(path_, L"ShowConsole", true);
        settings.generateLogs = ReadBoolean(path_, L"GenerateLogs", true);
        if (settings.playerName.empty())
        {
            settings.playerName = L"Player";
        }
        return settings;
    }

    bool LauncherSettingsStore::Save(const LauncherSettings& settings) const
    {
        if (path_.empty())
        {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error)
        {
            return false;
        }

        return WriteString(path_, L"GamePath", settings.gameDirectory.wstring()) &&
            WriteString(path_, L"PlayerName", settings.playerName) &&
            WriteString(path_, L"HostAddress", settings.hostAddress) &&
            WriteString(path_, L"Port", std::to_wstring(settings.port)) &&
            WriteString(path_, L"ShowConsole", settings.showConsole ? L"1" : L"0") &&
            WriteString(path_, L"GenerateLogs", settings.generateLogs ? L"1" : L"0");
    }

    const std::filesystem::path& LauncherSettingsStore::Path() const noexcept
    {
        return path_;
    }
}
