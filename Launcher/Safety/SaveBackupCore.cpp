#include "SaveBackupCore.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace fable::launcher::safety
{
    namespace fs = std::filesystem;

    namespace
    {
        fs::path AbsoluteNormal(const fs::path& path)
        {
            std::error_code error;
            const fs::path absolute = fs::absolute(path, error);
            return (error ? path : absolute).lexically_normal();
        }

        std::wstring ComparablePart(const fs::path& part)
        {
            std::wstring value = part.wstring();
#if defined(_WIN32)
            std::transform(value.begin(), value.end(), value.begin(),
                [](const wchar_t character)
                {
                    return static_cast<wchar_t>(std::towlower(character));
                });
#endif
            return value;
        }

        bool IsSameOrBelow(const fs::path& candidate, const fs::path& root)
        {
            const fs::path normalizedCandidate = AbsoluteNormal(candidate);
            const fs::path normalizedRoot = AbsoluteNormal(root);

            auto candidatePart = normalizedCandidate.begin();
            for (auto rootPart = normalizedRoot.begin();
                 rootPart != normalizedRoot.end();
                 ++rootPart, ++candidatePart)
            {
                if (candidatePart == normalizedCandidate.end() ||
                    ComparablePart(*candidatePart) != ComparablePart(*rootPart))
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring Timestamp()
        {
            const std::time_t raw = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &raw);
#else
            localtime_r(&raw, &local);
#endif
            std::wostringstream stream;
            stream << std::put_time(&local, L"%Y%m%d-%H%M%S");
            return stream.str();
        }

        fs::path CompletedCandidate(
            const fs::path& backupRoot,
            const std::wstring& baseName,
            const unsigned int suffix)
        {
            if (suffix == 0)
            {
                return backupRoot / baseName;
            }

            std::wostringstream name;
            name << baseName << L"-" << std::setw(3) << std::setfill(L'0') << suffix;
            return backupRoot / name.str();
        }

        bool CreateUniqueStagingDirectory(
            const fs::path& backupRoot,
            fs::path& completed,
            fs::path& partial,
            std::error_code& error)
        {
            const std::wstring baseName = L"backup-" + Timestamp();
            for (unsigned int suffix = 0; suffix < 1000; ++suffix)
            {
                completed = CompletedCandidate(backupRoot, baseName, suffix);
                partial = fs::path(completed.wstring() + L".partial");

                error.clear();
                if (fs::exists(completed, error))
                {
                    if (error)
                    {
                        return false;
                    }
                    continue;
                }
                if (error)
                {
                    return false;
                }

                error.clear();
                if (fs::create_directory(partial, error))
                {
                    return true;
                }
                if (error)
                {
                    return false;
                }

                // Another launcher/process may own this .partial directory.
                // Never remove it; move to the next suffix instead.
            }
            return false;
        }

        void RemoveOwnedPartial(const fs::path& partial)
        {
            std::error_code ignored;
            fs::remove_all(partial, ignored);
        }

        bool IsCompletedBackup(const fs::directory_entry& entry)
        {
            std::error_code statusError;
            const fs::file_status status = entry.symlink_status(statusError);
            if (statusError || !fs::is_directory(status) || fs::is_symlink(status))
            {
                return false;
            }

            const std::wstring name = entry.path().filename().wstring();
            return name.rfind(L"backup-", 0) == 0 &&
                name.find(L".partial") == std::wstring::npos;
        }

        bool PruneCompletedBackups(
            const fs::path& backupRoot,
            const std::size_t maxBackups,
            std::wstring& warning)
        {
            std::error_code error;
            std::vector<fs::directory_entry> completed;
            for (fs::directory_iterator iterator(backupRoot, error), end;
                 !error && iterator != end;
                 iterator.increment(error))
            {
                if (IsCompletedBackup(*iterator))
                {
                    completed.push_back(*iterator);
                }
            }

            if (error)
            {
                warning = L"Backup created, but old backups could not be enumerated for pruning.";
                return false;
            }

            // Timestamped names sort chronologically, including the -NNN
            // collision suffix used for backups created within one second.
            std::sort(completed.begin(), completed.end(),
                [](const fs::directory_entry& left, const fs::directory_entry& right)
                {
                    return left.path().filename().wstring() <
                        right.path().filename().wstring();
                });

            while (completed.size() > maxBackups)
            {
                std::error_code removeError;
                fs::remove_all(completed.front().path(), removeError);
                if (removeError)
                {
                    warning = L"Backup created, but one or more old backups could not be pruned.";
                    return false;
                }
                completed.erase(completed.begin());
            }
            return true;
        }
    }

    SaveBackupReport CreateSaveBackup(
        const fs::path& saveDirectory,
        const fs::path& backupRoot,
        const std::size_t maxBackups)
    {
        SaveBackupReport report;

        if (saveDirectory.empty() || backupRoot.empty())
        {
            report.detail = L"The save or backup path is empty.";
            return report;
        }

        std::error_code error;
        const bool sourceExists = fs::exists(saveDirectory, error);
        if (error)
        {
            report.detail = L"The Fable save path could not be inspected.";
            return report;
        }
        if (!sourceExists)
        {
            report.success = true;
            report.detail = L"No Fable save directory exists yet; no backup was required.";
            return report;
        }
        if (!fs::is_directory(saveDirectory, error) || error)
        {
            report.detail = L"The Fable save path exists but is not a readable directory.";
            return report;
        }
        report.sourcePresent = true;

        if (IsSameOrBelow(backupRoot, saveDirectory))
        {
            report.detail = L"The backup directory must not be inside the live save directory.";
            return report;
        }

        fs::create_directories(backupRoot, error);
        if (error)
        {
            report.detail = L"The AlbionTogether backup directory could not be created.";
            return report;
        }

        fs::path completed;
        fs::path partial;
        if (!CreateUniqueStagingDirectory(
                backupRoot, completed, partial, error))
        {
            report.detail = error
                ? L"A unique temporary backup directory could not be created."
                : L"A unique backup directory name could not be created.";
            return report;
        }

        for (fs::recursive_directory_iterator iterator(saveDirectory, error), end;
             !error && iterator != end;
             iterator.increment(error))
        {
            const fs::directory_entry& entry = *iterator;
            const fs::path relative = entry.path().lexically_relative(saveDirectory);
            const fs::path destination = partial / relative;

            std::error_code typeError;
            const fs::file_status status = entry.symlink_status(typeError);
            if (typeError)
            {
                error = typeError;
                break;
            }

            if (fs::is_symlink(status))
            {
                if (fs::is_directory(entry.status(typeError)) && !typeError)
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }

            if (fs::is_directory(status))
            {
                fs::create_directories(destination, error);
                if (error)
                {
                    break;
                }
                continue;
            }

            if (!fs::is_regular_file(status))
            {
                // Fable saves are ordinary files/directories. Ignore special
                // filesystem entries instead of following them outside the
                // live save tree.
                continue;
            }

            fs::create_directories(destination.parent_path(), error);
            if (error)
            {
                break;
            }
            fs::copy_file(entry.path(), destination, fs::copy_options::none, error);
            if (error)
            {
                break;
            }

            std::error_code sizeError;
            const std::uintmax_t bytes = entry.file_size(sizeError);
            if (sizeError)
            {
                error = sizeError;
                break;
            }
            ++report.fileCount;
            report.totalBytes += bytes;
        }

        if (error)
        {
            RemoveOwnedPartial(partial);
            report.detail = L"A save file could not be copied; launch should remain blocked to protect the live save.";
            return report;
        }

        fs::rename(partial, completed, error);
        if (error)
        {
            RemoveOwnedPartial(partial);
            report.detail = L"The completed backup could not be finalized.";
            return report;
        }

        report.success = true;
        report.backupDirectory = completed;
        report.detail = L"Save backup created successfully.";

        std::wstring pruneWarning;
        if (!PruneCompletedBackups(
                backupRoot,
                std::max<std::size_t>(1, maxBackups),
                pruneWarning) &&
            !pruneWarning.empty())
        {
            report.detail += L" " + pruneWarning;
        }
        return report;
    }
}
