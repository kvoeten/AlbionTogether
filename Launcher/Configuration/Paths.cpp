#include "Paths.h"

#include <Windows.h>

namespace fable::launcher
{
fs::path GetLauncherDirectory()
{
    std::wstring buffer(32'768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

fs::path AbsolutePath(const fs::path& path)
{
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    return error ? path : absolute.lexically_normal();
}

bool IsFile(const fs::path& path)
{
    std::error_code error;
    return fs::is_regular_file(path, error);
}

bool IsDirectory(const fs::path& path)
{
    std::error_code error;
    return fs::is_directory(path, error);
}

bool IsSamePathOrBelow(const fs::path& candidate, const fs::path& root)
{
    const fs::path normalizedCandidate = AbsolutePath(candidate);
    const fs::path normalizedRoot = AbsolutePath(root);
    auto candidatePart = normalizedCandidate.begin();
    for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end(); ++rootPart, ++candidatePart)
    {
        if (candidatePart == normalizedCandidate.end() ||
            _wcsicmp(candidatePart->c_str(), rootPart->c_str()) != 0)
        {
            return false;
        }
    }
    return true;
}

fs::path GetOrdinaryDocumentsPath()
{
    std::wstring userProfile(32'768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", userProfile.data(), static_cast<DWORD>(userProfile.size()));
    if (length == 0 || length >= userProfile.size())
    {
        return {};
    }
    userProfile.resize(length);
    return AbsolutePath(fs::path(userProfile) / L"Documents");
}

fs::path ExecutableBelow(const fs::path& directory)
{
    const fs::path direct = directory / kGameExecutableName;
    if (IsFile(direct))
    {
        return AbsolutePath(direct);
    }
    const fs::path conventional = directory / L"Binaries" / L"Win32" / kGameExecutableName;
    if (IsFile(conventional))
    {
        return AbsolutePath(conventional);
    }
    return {};
}

fs::path ResolveDeploymentAsset(const fs::path& launcherDirectory, const fs::path& relativePath, bool directory)
{
    const fs::path alongside = AbsolutePath(launcherDirectory / relativePath);
    if (directory ? IsDirectory(alongside) : IsFile(alongside))
    {
        return alongside;
    }
    const fs::path development = AbsolutePath(launcherDirectory / L".." / L".." / relativePath);
    if (directory ? IsDirectory(development) : IsFile(development))
    {
        return development;
    }
    return alongside;
}

fs::path ResolveExecutable(const Options& options, const fs::path& launcherDirectory)
{
    if (!options.executable.empty())
    {
        return AbsolutePath(options.executable);
    }
    if (!options.gameDirectory.empty())
    {
        return ExecutableBelow(AbsolutePath(options.gameDirectory));
    }
    const fs::path alongside = ExecutableBelow(launcherDirectory);
    if (!alongside.empty())
    {
        return alongside;
    }
    return ExecutableBelow(fs::path(kDevelopmentGameRoot));
}
}
