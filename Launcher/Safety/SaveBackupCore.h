#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fable::launcher::safety
{
    struct SaveBackupReport
    {
        bool success = false;
        bool sourcePresent = false;
        std::filesystem::path backupDirectory;
        std::uintmax_t fileCount = 0;
        std::uintmax_t totalBytes = 0;
        std::wstring detail;
    };

    // Creates a timestamped, complete copy of saveDirectory beneath backupRoot.
    // A uniquely-created .partial staging directory is used and only renamed
    // into place after every regular file has been copied successfully.
    // Completed backups are retained newest-first up to maxBackups (minimum 1).
    SaveBackupReport CreateSaveBackup(
        const std::filesystem::path& saveDirectory,
        const std::filesystem::path& backupRoot,
        std::size_t maxBackups = 10);
}
