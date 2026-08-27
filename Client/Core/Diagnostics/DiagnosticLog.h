#pragma once

#include <Windows.h>

#include <string>

namespace fable::core
{
    class DiagnosticLog final
    {
    public:
        void Initialize(
            HMODULE clientModule,
            const wchar_t* logPath,
            bool writeFile,
            const wchar_t* eventPath,
            const wchar_t* runId,
            const wchar_t* scenario);
        void AttachConsole();

        void Log(const char* message);
        void Event(const char* state, const char* detail = "");

    private:
        static std::string JsonEscape(const std::string& value);
        static std::string WideToUtf8(const wchar_t* value);

        SRWLOCK lock_ = SRWLOCK_INIT;
        HANDLE consoleOutput_ = nullptr;
        std::wstring logPath_;
        std::wstring eventPath_;
        std::wstring runId_;
        std::wstring scenario_;
    };
}
