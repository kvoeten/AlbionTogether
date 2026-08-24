#pragma once

#include <Windows.h>
#include <string>

namespace fable::launcher::platform
{
std::wstring FormatWindowsError(DWORD error);
}
