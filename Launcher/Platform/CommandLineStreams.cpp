#include "CommandLineStreams.h"

#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <streambuf>
#include <vector>

namespace fable::launcher::platform
{
    namespace
    {
        class HandleStreamBuffer final : public std::wstreambuf
        {
        public:
            explicit HandleStreamBuffer(const HANDLE handle)
                : handle_(handle)
            {
                DWORD mode = 0;
                console_ = handle_ != nullptr &&
                    handle_ != INVALID_HANDLE_VALUE &&
                    GetConsoleMode(handle_, &mode) != FALSE;
            }

        protected:
            int_type overflow(const int_type value) override
            {
                if (traits_type::eq_int_type(value, traits_type::eof()))
                {
                    return traits_type::not_eof(value);
                }
                const wchar_t character = traits_type::to_char_type(value);
                return Write(&character, 1) == 1
                    ? value : traits_type::eof();
            }

            std::streamsize xsputn(
                const wchar_t* text,
                const std::streamsize count) override
            {
                return Write(text, count);
            }

        private:
            std::streamsize Write(
                const wchar_t* text,
                const std::streamsize count) const
            {
                if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE ||
                    text == nullptr || count <= 0)
                {
                    return 0;
                }

                std::streamsize completed = 0;
                while (completed < count)
                {
                    const DWORD characters = static_cast<DWORD>((std::min)(
                        count - completed,
                        static_cast<std::streamsize>(32'768)));
                    if (console_)
                    {
                        DWORD written = 0;
                        if (!WriteConsoleW(
                                handle_, text + completed, characters,
                                &written, nullptr))
                        {
                            break;
                        }
                        completed += written;
                        continue;
                    }

                    const int required = WideCharToMultiByte(
                        CP_UTF8, 0, text + completed,
                        static_cast<int>(characters), nullptr, 0,
                        nullptr, nullptr);
                    if (required <= 0)
                    {
                        break;
                    }
                    std::vector<char> bytes(static_cast<std::size_t>(required));
                    if (WideCharToMultiByte(
                            CP_UTF8, 0, text + completed,
                            static_cast<int>(characters), bytes.data(),
                            required, nullptr, nullptr) != required)
                    {
                        break;
                    }
                    DWORD offset = 0;
                    while (offset < static_cast<DWORD>(bytes.size()))
                    {
                        DWORD written = 0;
                        if (!WriteFile(
                                handle_, bytes.data() + offset,
                                static_cast<DWORD>(bytes.size()) - offset,
                                &written, nullptr) || written == 0)
                        {
                            return completed;
                        }
                        offset += written;
                    }
                    completed += characters;
                }
                return completed;
            }

            HANDLE handle_ = INVALID_HANDLE_VALUE;
            bool console_ = false;
        };

        bool IsUsable(const HANDLE handle)
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }
    }

    void AttachCommandLineStreams()
    {
        if (GetConsoleWindow() == nullptr)
        {
            AttachConsole(ATTACH_PARENT_PROCESS);
        }

        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        if (IsUsable(output))
        {
            static HandleStreamBuffer* outputBuffer =
                new HandleStreamBuffer(output);
            std::wcout.rdbuf(outputBuffer);
            std::wcout.clear();
        }
        if (IsUsable(error))
        {
            static HandleStreamBuffer* errorBuffer =
                new HandleStreamBuffer(error);
            std::wcerr.rdbuf(errorBuffer);
            std::wcerr.clear();
        }
    }
}
