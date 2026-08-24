#include "RunId.h"

#include <Windows.h>

namespace fable::launcher
{
    std::wstring CreateRunId()
    {
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        wchar_t value[64] = {};
        swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
            static_cast<unsigned int>(time.wYear),
            static_cast<unsigned int>(time.wMonth),
            static_cast<unsigned int>(time.wDay),
            static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute),
            static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds),
            static_cast<unsigned long>(GetCurrentProcessId()));
        return value;
    }
}
