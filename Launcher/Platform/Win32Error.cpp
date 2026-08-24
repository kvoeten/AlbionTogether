#include "Win32Error.h"

namespace fable::launcher::platform
{
std::wstring FormatWindowsError(DWORD error)
{
    wchar_t *rawMessage = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<wchar_t *>(&rawMessage), 0, nullptr);
    std::wstring message =
        length != 0 && rawMessage != nullptr ? std::wstring(rawMessage, length) : L"Unknown Windows error";
    if (rawMessage != nullptr)
        LocalFree(rawMessage);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        message.pop_back();
    return message;
}
} // namespace fable::launcher::platform
