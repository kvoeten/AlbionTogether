#include "CreatureBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"

#include "Game/Creature/Control/ScriptControl.h"

#include <angelscript.h>

namespace fable::scripting::bindings
{
    bool RegisterCreatureBindings(asIScriptEngine& engine)
    {
        int result = engine.RegisterObjectBehaviour(
            "CreatureControl",
            asBEHAVE_ADDREF,
            "void f()",
            asMETHOD(game::ScriptControl, AddRef),
            asCALL_THISCALL);
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "CreatureControl",
                asBEHAVE_RELEASE,
                "void f()",
                asMETHOD(game::ScriptControl, Release),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool get_Valid() const",
                asMETHOD(game::ScriptControl, IsValid),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool get_Busy() const",
                asMETHOD(game::ScriptControl, IsBusy),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool MoveToPosition(const Vector3 &in, float, MoveType, bool avoidDynamicObstacles = true, bool ignorePathPreferability = false)",
                asMETHOD(game::ScriptControl, MoveToPosition),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool MoveToEntity(Entity@, float, MoveType, bool avoidDynamicObstacles = true, bool ignorePathPreferability = false, bool faceMovement = true)",
                asMETHOD(game::ScriptControl, MoveToEntity),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool Follow(Entity@, float, bool avoidDynamicObstacles = true)",
                asMETHOD(game::ScriptControl, Follow),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool StopFollowing(Entity@)",
                asMETHOD(game::ScriptControl, StopFollowing),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool FireProjectileAt(Entity@)",
                asMETHOD(game::ScriptControl, FireProjectileAt),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool PlayAnimation(const string &in, bool waitForFinish = false, bool stayOnLastFrame = false, bool allowLooking = true)",
                asMETHOD(game::ScriptControl, PlayAnimation),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool PlayCombatAnimation(const string &in, bool waitForFinish = false, bool allowLooking = true)",
                asMETHOD(game::ScriptControl, PlayCombatAnimation),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool PlayLoopingAnimation(const string &in, int loops, bool useMovement = true, bool allowLooking = true)",
                asMETHOD(game::ScriptControl, PlayLoopingAnimation),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl", "bool UnsheatheWeapons()", asMETHOD(game::ScriptControl, UnsheatheWeapons), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl", "bool ClearCommands()", asMETHOD(game::ScriptControl, ClearCommands), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool ClearAllActions(bool includeLoopingAnimations = true)",
                asMETHOD(game::ScriptControl, ClearAllActions),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureControl",
                "bool ReleaseControl()",
                asMETHOD(game::ScriptControl, ReleaseControl),
                asCALL_THISCALL)
            : result;
        return result >= 0;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureBindingGroup(BindingContext& context)
    {
        return RegisterCreatureBindings(context.Engine);
    }
}

FABLE_SCRIPT_BINDING_GROUP(Creature, 200, &fable::scripting::bindings::RegisterCreatureBindingGroup);
