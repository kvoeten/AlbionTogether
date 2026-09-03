#pragma once

#include <cstddef>
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

    // Current x86 build's counted string representation. The first field is
    // an opaque ownership/ref-count value; IDA shows data at +4 and length at
    // +8 in the retail quest-save wrapper.
    struct StringRep
    {
        void* unknown = nullptr;
        char* data = nullptr;
        long len = 0;
    };

    struct WideString
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
    static_assert(sizeof(WideString) == 4, "Unexpected Fable CWideString layout.");
    static_assert(sizeof(StringRep) == 12, "Unexpected Fable StringRep layout.");
    static_assert(offsetof(StringRep, data) == 4, "Unexpected Fable StringRep data offset.");
    static_assert(offsetof(StringRep, len) == 8, "Unexpected Fable StringRep length offset.");
    static_assert(sizeof(ScriptControlHandle) == 16, "Unexpected Fable scripted-control handle layout.");
}
