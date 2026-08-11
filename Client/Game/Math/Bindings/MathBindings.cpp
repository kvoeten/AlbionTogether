#include "MathBindings.h"

#include "Game/Math/Vector3.h"

#include <angelscript.h>

#include <new>

namespace
{
    void ConstructVector3(fable::game::Vector3* value)
    {
        new(value) fable::game::Vector3();
    }

    void ConstructVector3Values(
        float x,
        float y,
        float z,
        fable::game::Vector3* value)
    {
        new(value) fable::game::Vector3{x, y, z};
    }
}

namespace fable::scripting::bindings
{
    bool RegisterMathBindings(asIScriptEngine& engine)
    {
        int result = engine.RegisterObjectType(
            "Vector3",
            sizeof(game::Vector3),
            asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<game::Vector3>());
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "Vector3",
                asBEHAVE_CONSTRUCT,
                "void f()",
                asFUNCTION(ConstructVector3),
                asCALL_CDECL_OBJLAST)
            : result;
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "Vector3",
                asBEHAVE_CONSTRUCT,
                "void f(float, float, float)",
                asFUNCTION(ConstructVector3Values),
                asCALL_CDECL_OBJLAST)
            : result;
        result = result >= 0
            ? engine.RegisterObjectProperty("Vector3", "float x", asOFFSET(game::Vector3, x))
            : result;
        result = result >= 0
            ? engine.RegisterObjectProperty("Vector3", "float y", asOFFSET(game::Vector3, y))
            : result;
        result = result >= 0
            ? engine.RegisterObjectProperty("Vector3", "float z", asOFFSET(game::Vector3, z))
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Vector3",
                "float HorizontalDistanceTo(const Vector3 &in) const",
                asMETHOD(game::Vector3, HorizontalDistanceTo),
                asCALL_THISCALL)
            : result;
        return result >= 0;
    }
}
