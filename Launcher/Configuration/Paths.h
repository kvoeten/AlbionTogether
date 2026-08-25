#pragma once

#include "Options.h"

namespace fable::launcher
{
fs::path GetLauncherDirectory();
fs::path AbsolutePath(const fs::path& path);
bool IsFile(const fs::path& path);
bool IsDirectory(const fs::path& path);
bool IsSamePathOrBelow(const fs::path& candidate, const fs::path& root);
fs::path GetOrdinaryDocumentsPath();
fs::path ExecutableBelow(const fs::path& directory);
fs::path ResolveDeploymentAsset(const fs::path& launcherDirectory, const fs::path& relativePath, bool directory);
fs::path ResolveExecutable(const Options& options, const fs::path& launcherDirectory);
}
