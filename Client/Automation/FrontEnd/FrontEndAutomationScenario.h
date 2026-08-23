#pragma once

#include <Windows.h>
#include <cstddef>

namespace fable::automation::front_end
{
    void LogUiLifecycleEvent(
        const char* state,
        const void* object,
        const void* frame,
        std::size_t virtualMethodIndex);
    void __stdcall ObserveUiPageDoBegin(void* object, const void* frame);
    void __stdcall ObserveUiPageDoInit(void* object, const void* frame);
    void __stdcall ObserveUiPageStartPlay(void* object, const void* frame);
    void __stdcall ObservePlayLoadMapMovie(void* object, const void* frame);
    void __stdcall ObserveFrontEndStartDoInit(void* object, const void* frame);
    void __stdcall ObserveFrontEndStartDoTick(void* object, const void* frame);
    void OnFrontEndStartInitialized(void* object);
    void DriveBootstrapFixtureProbe();
    void DriveSaveListObservation();
    void ObserveSaveListReadiness();
    void DriveFixtureLoad();
    void OnGameThreadIdle();
}
