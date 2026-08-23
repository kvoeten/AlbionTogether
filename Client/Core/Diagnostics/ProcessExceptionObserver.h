#pragma once

#include "Core/Bootstrap/ClientRuntimeState.h"
#include <Windows.h>

namespace fable::core::diagnostics
{
    LONG CaptureNativeFault(
        EXCEPTION_POINTERS* exceptionPointers,
        fable::core::bootstrap::NativeFault* fault,
        const char* stage);
    void LogNativeFault(const fable::core::bootstrap::NativeFault& fault);
    LONG CALLBACK ObserveProcessException(EXCEPTION_POINTERS* exceptionPointers);
}
