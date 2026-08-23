#include <Windows.h>

#include "Core/Bootstrap/ClientRuntime.h"

extern "C" __declspec(dllexport) const wchar_t* __cdecl FableTogetherVersion()
{
    return L"development-current";
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        fable::core::bootstrap::CaptureClientModule(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

