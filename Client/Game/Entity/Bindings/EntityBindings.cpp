#include "EntityBindings.h"

#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Entity/Entity.h"

#include <angelscript.h>

namespace fable::scripting::bindings
{
    bool RegisterEntityTypes(asIScriptEngine& engine)
    {
        int result = engine.RegisterEnum("AiPriority");
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "Lowest", 0) : result;
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "Lower", 1) : result;
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "Normal", 2) : result;
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "Higher", 3) : result;
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "High", 4) : result;
        result = result >= 0 ? engine.RegisterEnumValue("AiPriority", "Highest", 5) : result;
        result = result >= 0 ? engine.RegisterEnum("MoveType") : result;
        result = result >= 0 ? engine.RegisterEnumValue("MoveType", "Walk", 0) : result;
        result = result >= 0 ? engine.RegisterEnumValue("MoveType", "Run", 1) : result;
        result = result >= 0 ? engine.RegisterEnumValue("MoveType", "Sneak", 2) : result;
        result = result >= 0
            ? engine.RegisterObjectType("Entity", 0, asOBJ_REF)
            : result;
        result = result >= 0
            ? engine.RegisterObjectType("CreatureControl", 0, asOBJ_REF)
            : result;
        return result >= 0;
    }

    bool RegisterEntityMembers(asIScriptEngine& engine)
    {
        int result = engine.RegisterObjectBehaviour(
            "Entity", asBEHAVE_ADDREF, "void f()", asMETHOD(game::Entity, AddRef), asCALL_THISCALL);
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "Entity", asBEHAVE_RELEASE, "void f()", asMETHOD(game::Entity, Release), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool get_Valid() const", asMETHOD(game::Entity, IsValid), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool get_Alive() const", asMETHOD(game::Entity, IsAlive), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool get_Dead() const", asMETHOD(game::Entity, IsDead), asCALL_THISCALL)
            : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_Sneaking() const", asMETHOD(game::Entity, IsSneaking), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_AwareOfHero() const", asMETHOD(game::Entity, IsAwareOfHero), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_Unconscious() const", asMETHOD(game::Entity, IsUnconscious), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_Usable() const", asMETHOD(game::Entity, IsUsable), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_OpenDoor() const", asMETHOD(game::Entity, IsOpenDoor), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_SummonedCreature() const", asMETHOD(game::Entity, IsSummonedCreature), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "string get_Name() const", asMETHOD(game::Entity, GetName), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "string get_DefinitionName() const", asMETHOD(game::Entity, GetDefinitionName), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "string get_DataString() const", asMETHOD(game::Entity, GetDataString), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "string get_CurrentMapName() const", asMETHOD(game::Entity, GetCurrentMapName), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "string get_HomeMapName() const", asMETHOD(game::Entity, GetHomeMapName), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool get_ActivationTriggerActive() const", asMETHOD(game::Entity, GetActivationTriggerStatus), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "int get_ScriptCounter() const", asMETHOD(game::Entity, GetScriptCounter), asCALL_THISCALL) : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "Vector3 get_Position() const", asMETHOD(game::Entity, GetPosition), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "float get_Facing() const", asMETHOD(game::Entity, GetFacing), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity",
                "bool Teleport(const Vector3 &in, float, bool effect = false)",
                asMETHOD(game::Entity, Teleport),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool SetAttackable(bool)", asMETHOD(game::Entity, SetAttackable), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool SetDamageable(bool)", asMETHOD(game::Entity, SetDamageable), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool SetCollidable(bool)", asMETHOD(game::Entity, SetCollidable), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool SetDrawable(bool)", asMETHOD(game::Entity, SetDrawable), asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity", "bool SetDataString(const string &in)", asMETHOD(game::Entity, SetDataString), asCALL_THISCALL)
            : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool SetUsable(bool)", asMETHOD(game::Entity, SetUsable), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool SetFriendsWithEverything(bool)", asMETHOD(game::Entity, SetFriendsWithEverything), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool SetActivationTriggerStatus(bool)", asMETHOD(game::Entity, SetActivationTriggerStatus), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool SetKillOnLevelUnload(bool)", asMETHOD(game::Entity, SetKillOnLevelUnload), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool RequestDestroy(bool immediate = false)", asMETHOD(game::Entity, RequestDestroy), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool UpdateAttachment()", asMETHOD(game::Entity, UpdateAttachment), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool IncrementScriptCounter()", asMETHOD(game::Entity, IncrementScriptCounter), asCALL_THISCALL) : result;
        result = result >= 0 ? engine.RegisterObjectMethod("Entity", "bool DecrementScriptCounter()", asMETHOD(game::Entity, DecrementScriptCounter), asCALL_THISCALL) : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity",
                "bool Attack(Entity@, bool stopCurrentAction = true, bool unsheathe = true)",
                asMETHOD(game::Entity, Attack),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "Entity",
                "CreatureControl@+ AcquireControl(AiPriority priority = Highest)",
                asMETHOD(game::Entity, AcquireControl),
                asCALL_THISCALL)
            : result;
        return result >= 0;
    }
}
