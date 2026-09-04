#include "../../Launcher/Safety/SaveBackupCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using fable::launcher::safety::CreateSaveBackup;

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* expression)
    {
        if (!condition)
        {
            std::cerr << "SaveBackupCore.Tests: failed: " << expression << '\n';
            ++failures;
        }
    }

#define CHECK(expression) Check((expression), #expression)

    void Write(const fs::path& path, const std::string& contents)
    {
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        CHECK(!error);
        std::ofstream stream(path, std::ios::binary);
        stream << contents;
        CHECK(stream.good());
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    std::size_t CompletedBackupCount(const fs::path& root)
    {
        std::size_t count = 0;
        std::error_code error;
        for (fs::directory_iterator iterator(root, error), end;
             !error && iterator != end;
             iterator.increment(error))
        {
            const auto& entry = *iterator;
            const std::string name = entry.path().filename().string();
            std::error_code statusError;
            if (entry.is_directory(statusError) && !statusError &&
                name.rfind("backup-", 0) == 0 &&
                name.find(".partial") == std::string::npos)
            {
                ++count;
            }
        }
        CHECK(!error);
        return count;
    }
}

int main()
{
    const fs::path root = fs::temp_directory_path() /
        "albiontogether-savebackup-tests";
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root, cleanupError);
    CHECK(!cleanupError);

    // Fresh installs have no live save directory yet; that must not block play.
    {
        const auto report = CreateSaveBackup(
            root / "missing", root / "backups", 10);
        CHECK(report.success);
        CHECK(!report.sourcePresent);
        CHECK(report.backupDirectory.empty());
    }

    const fs::path live = root / "live-saves";
    const fs::path backups = root / "backups";
    Write(live / "Hero1" / "save.dat", "hero-one");
    Write(live / "Hero1" / "profile.bin", "profile");

    // Full recursive copy and accounting.
    {
        const auto report = CreateSaveBackup(live, backups, 10);
        CHECK(report.success);
        CHECK(report.sourcePresent);
        CHECK(!report.backupDirectory.empty());
        CHECK(report.fileCount == 2);
        CHECK(report.totalBytes == 15);
        CHECK(Read(report.backupDirectory / "Hero1" / "save.dat") ==
            "hero-one");
        CHECK(Read(report.backupDirectory / "Hero1" / "profile.bin") ==
            "profile");
    }

    // Multiple launch attempts, including concurrent attempts in the same
    // second, must never delete or share another process's staging directory.
    {
        constexpr std::size_t concurrentBackups = 4;
        std::vector<fable::launcher::safety::SaveBackupReport> reports(
            concurrentBackups);
        std::vector<std::thread> workers;
        workers.reserve(concurrentBackups);
        for (std::size_t index = 0; index < concurrentBackups; ++index)
        {
            workers.emplace_back([&, index]()
            {
                reports[index] = CreateSaveBackup(live, backups, 10);
            });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
        for (const auto& report : reports)
        {
            CHECK(report.success);
            CHECK(!report.backupDirectory.empty());
        }
        for (std::size_t left = 0; left < reports.size(); ++left)
        {
            for (std::size_t right = left + 1; right < reports.size(); ++right)
            {
                CHECK(reports[left].backupDirectory !=
                    reports[right].backupDirectory);
            }
        }
    }

    // Retention keeps only the requested number of completed backups.
    {
        Write(live / "Hero1" / "save.dat", "hero-two");
        CHECK(CreateSaveBackup(live, backups, 2).success);
        Write(live / "Hero1" / "save.dat", "hero-three");
        CHECK(CreateSaveBackup(live, backups, 2).success);
        CHECK(CompletedBackupCount(backups) == 2);
    }

    // Never allow recursive placement below the live save tree.
    {
        const auto report = CreateSaveBackup(
            live, live / "backup-inside-live", 10);
        CHECK(!report.success);
    }

    // A symlink inside a save tree must not pull external data into backups.
    {
        const fs::path external = root / "external";
        Write(external / "secret.bin", "outside-save-tree");
        std::error_code linkError;
        fs::create_directory_symlink(external, live / "external-link", linkError);
        if (!linkError)
        {
            const auto report = CreateSaveBackup(live, backups, 10);
            CHECK(report.success);
            CHECK(!fs::exists(report.backupDirectory /
                "external-link" / "secret.bin"));
        }
    }

    // Successful runs must not leave staging directories behind.
    std::error_code iteratorError;
    for (fs::directory_iterator iterator(backups, iteratorError), end;
         !iteratorError && iterator != end;
         iterator.increment(iteratorError))
    {
        CHECK(iterator->path().filename().string().find(".partial") ==
            std::string::npos);
    }
    CHECK(!iteratorError);

    fs::remove_all(root, cleanupError);
    if (failures != 0)
    {
        std::cerr << failures << " SaveBackupCore test assertion(s) failed\n";
        return 1;
    }
    std::cout << "SaveBackupCore tests passed\n";
    return 0;
}
