#include "Core/Diagnostics/DiagnosticLog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), {}};
    }

    bool CanOpenExclusively(const std::filesystem::path& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }
}

int RunDiagnosticLogTests()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / ".build" /
        ("diagnostic-log-test-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(GetTickCount64()));
    fs::create_directories(root);
    const auto logPath = root / "client.log";
    const auto movedPath = root / "renamed.log";
    const auto eventPath = root / "events.jsonl";
    const auto secondPath = root / "second.log";
    int failures = 0;
    {
        fable::core::DiagnosticLog log;
        log.Initialize(nullptr, logPath.c_str(), true, eventPath.c_str(),
            L"test-run", L"diagnostic-test");
        log.Log("first");
        log.Event("ready", "quote=\" newline=\n");
        // Entries are visible before shutdown: no crash-sensitive RAM queue.
        failures += Read(logPath).find("first") == std::string::npos;
        const auto initialEvent = Read(eventPath);
        failures += initialEvent.find("\"run_id\":\"test-run\"") ==
            std::string::npos;
        failures += initialEvent.find("quote=\\\" newline=\\n") ==
            std::string::npos;

        std::vector<std::thread> writers;
        for (unsigned int thread = 0; thread != 4; ++thread)
        {
            writers.emplace_back([&log] {
                for (unsigned int entry = 0; entry != 50; ++entry)
                    log.Event("concurrent", "whole-line");
            });
        }
        for (auto& writer : writers) writer.join();
        const auto concurrentEvents = Read(eventPath);
        failures += std::count(concurrentEvents.begin(),
            concurrentEvents.end(), '\n') != 201;

        // Rename the open file. Further writes must use its retained handle,
        // not open the original path again for each diagnostic.
        const bool moved = MoveFileExW(
            logPath.c_str(), movedPath.c_str(), 0) != FALSE;
        failures += !moved;
        if (moved)
        {
            log.Log("after-rename");
            failures += fs::exists(logPath);
            failures += Read(movedPath).find("after-rename") ==
                std::string::npos;
        }
        log.Shutdown();
        failures += !CanOpenExclusively(moved ? movedPath : logPath);
        failures += !CanOpenExclusively(eventPath);
        const auto stoppedSize = fs::file_size(eventPath);
        log.Event("after-shutdown", "must-not-reopen");
        failures += fs::file_size(eventPath) != stoppedSize;

        log.Initialize(nullptr, secondPath.c_str(), true, L"", L"next", L"");
        log.Log("second-session");
        failures += Read(secondPath).find("second-session") ==
            std::string::npos;
    }
    failures += !CanOpenExclusively(secondPath); // destructor closes the handle
    // Delete only this test's named files and its now-empty unique directory.
    for (const auto& path : {logPath, movedPath, eventPath, secondPath})
        fs::remove(path);
    fs::remove(root);
    return failures;
}
