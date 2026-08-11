#pragma once

#include <cstdint>

namespace fable::game::native
{
    struct CountedPointerInfo
    {
        unsigned int referenceCount;
        void* deleteFunction;
        void* data;
    };

    struct ScriptThing
    {
        void** vtable = nullptr;
        void* implementation = nullptr;
        CountedPointerInfo* pointerInfo = nullptr;
    };

    struct CharString
    {
        void* stringData = nullptr;
    };

    struct GameScriptInterface
    {
        void** vtable = nullptr;
    };

    struct ScriptControlHandle
    {
        void** vtable = nullptr;
        void* baseData = nullptr;
        void* implementation = nullptr;
        CountedPointerInfo* pointerInfo = nullptr;
    };

    static_assert(sizeof(ScriptThing) == 12, "Unexpected Fable CScriptThing layout.");
    static_assert(sizeof(ScriptControlHandle) == 16, "Unexpected Fable scripted-control handle layout.");
}
