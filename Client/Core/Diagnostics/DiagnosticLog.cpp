#include "DiagnosticLog.h"

#include <algorithm>
#include <cstdio>

namespace fable::core
{
    void DiagnosticLog::Initialize(
        HMODULE clientModule,
        const wchar_t* logPath,
        const wchar_t* eventPath,
        const wchar_t* runId,
        const wchar_t* scenario)
    {
        eventPath_ = eventPath != nullptr ? eventPath : L"";
        runId_ = runId != nullptr ? runId : L"";
        scenario_ = scenario != nullptr ? scenario : L"";
        logPath_ = logPath != nullptr ? logPath : L"";

        if (!logPath_.empty())
        {
            return;
        }

        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(
            clientModule,
            modulePath,
            static_cast<DWORD>(std::size(modulePath)));
        if (length == 0 || length >= std::size(modulePath))
        {
            return;
        }

        logPath_.assign(modulePath, length);
        const std::size_t separator = logPath_.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            logPath_.clear();
            return;
        }
        logPath_.resize(separator + 1);
        logPath_.append(L"AlbionTogether.Client.log");
    }

    void DiagnosticLog::AttachConsole()
    {
        if (GetConsoleWindow() == nullptr)
        {
            if (!::AttachConsole(ATTACH_PARENT_PROCESS) &&
                GetLastError() != ERROR_ACCESS_DENIED)
            {
                AllocConsole();
            }
        }

        SetConsoleTitleW(L"AlbionTogether diagnostics");
        consoleOutput_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (consoleOutput_ == INVALID_HANDLE_VALUE)
        {
            consoleOutput_ = nullptr;
        }
    }

    void DiagnosticLog::Log(const char* message)
    {
        char line[1'024] = {};
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        const int length = std::snprintf(
            line,
            std::size(line),
            "[%02u:%02u:%02u.%03u] %s\r\n",
            static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute),
            static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds),
            message != nullptr ? message : "<null>");
        if (length <= 0)
        {
            return;
        }

        AcquireSRWLockExclusive(&lock_);
        OutputDebugStringA(line);

        const DWORD bytesToWrite = static_cast<DWORD>(
            (std::min)(static_cast<std::size_t>(length), std::size(line) - 1));
        if (consoleOutput_ != nullptr)
        {
            DWORD bytesWritten = 0;
            WriteFile(consoleOutput_, line, bytesToWrite, &bytesWritten, nullptr);
        }

        if (!logPath_.empty())
        {
            const HANDLE file = CreateFileW(
                logPath_.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                DWORD bytesWritten = 0;
                WriteFile(file, line, bytesToWrite, &bytesWritten, nullptr);
                CloseHandle(file);
            }
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    void DiagnosticLog::Event(const char* state, const char* detail)
    {
        if (eventPath_.empty())
        {
            return;
        }

        SYSTEMTIME time = {};
        GetSystemTime(&time);
        const std::string runId = JsonEscape(WideToUtf8(runId_.c_str()));
        const std::string scenario = JsonEscape(WideToUtf8(scenario_.c_str()));
        const std::string escapedState = JsonEscape(state != nullptr ? state : "");
        const std::string escapedDetail = JsonEscape(detail != nullptr ? detail : "");

        char line[2'048] = {};
        const int length = std::snprintf(
            line,
            std::size(line),
            "{\"schema\":1,\"timestamp_utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
            "\"run_id\":\"%s\",\"pid\":%lu,\"thread_id\":%lu,\"scenario\":\"%s\","
            "\"state\":\"%s\",\"detail\":\"%s\"}\r\n",
            static_cast<unsigned int>(time.wYear),
            static_cast<unsigned int>(time.wMonth),
            static_cast<unsigned int>(time.wDay),
            static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute),
            static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds),
            runId.c_str(),
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()),
            scenario.c_str(),
            escapedState.c_str(),
            escapedDetail.c_str());
        if (length <= 0)
        {
            return;
        }

        AcquireSRWLockExclusive(&lock_);
        const HANDLE file = CreateFileW(
            eventPath_.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            const DWORD bytesToWrite = static_cast<DWORD>(
                (std::min)(static_cast<std::size_t>(length), std::size(line) - 1));
            DWORD bytesWritten = 0;
            WriteFile(file, line, bytesToWrite, &bytesWritten, nullptr);
            CloseHandle(file);
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    std::string DiagnosticLog::JsonEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 16);
        constexpr char kHex[] = "0123456789ABCDEF";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '\\': escaped.append("\\\\"); break;
            case '"': escaped.append("\\\""); break;
            case '\b': escaped.append("\\b"); break;
            case '\f': escaped.append("\\f"); break;
            case '\n': escaped.append("\\n"); break;
            case '\r': escaped.append("\\r"); break;
            case '\t': escaped.append("\\t"); break;
            default:
                if (character < 0x20)
                {
                    escaped.append("\\u00");
                    escaped.push_back(kHex[(character >> 4) & 0x0F]);
                    escaped.push_back(kHex[character & 0x0F]);
                }
                else
                {
                    escaped.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        return escaped;
    }

    std::string DiagnosticLog::WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
        {
            return {};
        }

        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }

        std::string converted(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            converted.data(),
            required,
            nullptr,
            nullptr);
        converted.pop_back();
        return converted;
    }
}
