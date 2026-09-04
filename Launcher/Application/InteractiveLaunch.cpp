#include "InteractiveLaunch.h"

#include "LaunchPlan.h"
#include "SingleGameLaunch.h"
#include "../Configuration/Paths.h"
#include "../Safety/SaveBackup.h"

#include <cwctype>

namespace fable::launcher::application
{
    namespace
    {
        bool HasVisibleText(const std::wstring& value)
        {
            bool hasVisibleCharacter = false;
            for (const wchar_t character : value)
            {
                if (std::iswcntrl(character) != 0)
                {
                    return false;
                }
                if (std::iswspace(character) == 0)
                {
                    hasVisibleCharacter = true;
                }
            }
            return hasVisibleCharacter;
        }

        bool ValidPlayerName(const std::wstring& name)
        {
            return name.size() <= 32 && HasVisibleText(name);
        }
    }

    bool LaunchInteractiveGame(
        const InteractiveLaunchRequest& request,
        std::wstring& result)
    {
        result.clear();
        if (!ValidPlayerName(request.settings.playerName))
        {
            result = L"Display names must contain 1 to 32 visible characters.";
            return false;
        }
        if (request.settings.port == 0)
        {
            result = L"Choose a UDP port between 1 and 65535.";
            return false;
        }
        if (request.role == InteractiveRole::Guest &&
            !HasVisibleText(request.settings.hostAddress))
        {
            result = L"Enter the host IP address.";
            return false;
        }

        Options options;
        options.executable = request.gameExecutable;
        options.gameDirectory = request.settings.gameDirectory;
        options.multiplayerRole = request.role == InteractiveRole::Host
            ? L"host" : L"guest";
        options.multiplayerAddress = request.role == InteractiveRole::Guest
            ? request.settings.hostAddress : L"";
        options.multiplayerPlayerId = request.settings.playerName;
        options.multiplayerAppearance = kRemoteHeroDefinition;
        options.multiplayerPort = request.settings.port;
        options.showConsole = request.settings.showConsole;
        options.generateLogs = request.settings.generateLogs;

        const std::filesystem::path launcherDirectory = GetLauncherDirectory();
        const LaunchPlan plan = BuildLaunchPlan(options, launcherDirectory);
        if (!ValidateLaunchPlan(plan))
        {
            result = L"The game or AlbionTogether client files could not be validated.";
            return false;
        }

        const safety::SaveBackupReport saveBackup =
            safety::CreateDefaultFableSaveBackup();
        if (!saveBackup.success)
        {
            result = L"Safety backup failed: " + saveBackup.detail +
                L"\n\nLaunch cancelled so the live Fable save is not put at additional alpha risk.";
            return false;
        }

        if (!PrepareLaunchArtifacts(plan))
        {
            result = L"The launcher could not prepare its run files.";
            return false;
        }
        if (RunSingleGame(plan) != 0)
        {
            result = L"Fable Anniversary could not be started.";
            return false;
        }
        result = request.role == InteractiveRole::Host
            ? L"Game launched. Friends can now join your IP address."
            : L"Game launched. Connection begins after your save is loaded.";
        if (saveBackup.sourcePresent && !saveBackup.backupDirectory.empty())
        {
            result += L"\nSafety backup: " + saveBackup.backupDirectory.wstring();
        }
        return true;
    }
}
