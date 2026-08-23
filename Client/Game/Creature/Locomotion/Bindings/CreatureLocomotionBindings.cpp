#include "CreatureLocomotionBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionState.h"
#include "Game/Entity/Entity.h"

#include <angelscript.h>

namespace
{
    fable::game::creature::locomotion::CreatureLocomotionService* g_locomotion = nullptr;

    fable::game::creature::locomotion::CreatureLocomotionState* InspectLocomotion(
        fable::game::Entity* entity)
    {
        return g_locomotion != nullptr ? g_locomotion->Inspect(entity) : nullptr;
    }

    bool MirrorPhysicsWorldPosition(
        fable::game::Entity* source,
        fable::game::Entity* target)
    {
        return g_locomotion != nullptr &&
            g_locomotion->MirrorPhysicsWorldPosition(source, target);
    }

    void ClearPhysicsWorldPositionMirror()
    {
        if (g_locomotion != nullptr)
        {
            g_locomotion->ClearPhysicsWorldPositionMirror();
        }
    }

    bool MirrorAnimationMotion(
        fable::game::Entity* source,
        fable::game::Entity* target)
    {
        return g_locomotion != nullptr &&
            g_locomotion->MirrorAnimationMotion(source, target);
    }

    void ClearAnimationMotionMirror()
    {
        if (g_locomotion != nullptr)
        {
            g_locomotion->ClearAnimationMotionMirror();
        }
    }

    bool RoutePlayerFrameInput(
        fable::game::Entity* source,
        fable::game::Entity* target)
    {
        return g_locomotion != nullptr &&
            g_locomotion->RoutePlayerFrameInput(source, target);
    }

    void ClearPlayerFrameInputRouter()
    {
        if (g_locomotion != nullptr)
        {
            g_locomotion->ClearPlayerFrameInputRouter();
        }
    }

    bool RouteHeroShadow(
        fable::game::Entity* sourcePuppet,
        fable::game::Entity* targetHero)
    {
        return g_locomotion != nullptr &&
            g_locomotion->RouteHeroShadow(sourcePuppet, targetHero);
    }

    void ClearHeroShadow()
    {
        if (g_locomotion != nullptr)
        {
            g_locomotion->ClearHeroShadow();
        }
    }

    unsigned int HeroShadowUpdateCount()
    {
        return g_locomotion != nullptr
            ? g_locomotion->HeroShadowUpdateCount()
            : 0;
    }

    bool SetPhysicsWorldPosition(
        fable::game::Entity* entity,
        const fable::game::Vector3& worldPosition)
    {
        return g_locomotion != nullptr &&
            g_locomotion->SetPhysicsWorldPosition(entity, worldPosition);
    }

    unsigned int MirroredPhysicsWorldPositionCount()
    {
        return g_locomotion != nullptr
            ? g_locomotion->MirroredPhysicsWorldPositionCount()
            : 0;
    }

    unsigned int MirroredAnimationMotionCount()
    {
        return g_locomotion != nullptr
            ? g_locomotion->MirroredAnimationMotionCount()
            : 0;
    }

    unsigned int RoutedPlayerFrameCount()
    {
        return g_locomotion != nullptr
            ? g_locomotion->RoutedPlayerFrameCount()
            : 0;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureLocomotionBindingGroup(BindingContext& context)
    {
        return RegisterCreatureLocomotionBindings(context.Engine, context.Services.Locomotion());
    }
}

FABLE_SCRIPT_BINDING_GROUP(CreatureLocomotion, 220, &fable::scripting::bindings::RegisterCreatureLocomotionBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterCreatureLocomotionBindings(
        asIScriptEngine& engine,
        game::creature::locomotion::CreatureLocomotionService& locomotion)
    {
        g_locomotion = &locomotion;
        using State = game::creature::locomotion::CreatureLocomotionState;

        int result = engine.RegisterObjectType(
            "CreatureLocomotionState",
            0,
            asOBJ_REF);
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "CreatureLocomotionState",
                asBEHAVE_ADDREF,
                "void f()",
                asMETHOD(State, AddRef),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectBehaviour(
                "CreatureLocomotionState",
                asBEHAVE_RELEASE,
                "void f()",
                asMETHOD(State, Release),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "bool get_Valid() const",
                asMETHOD(State, IsValid),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "bool get_HasPhysicsNavigator() const",
                asMETHOD(State, HasPhysicsNavigator),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "bool get_HasCreatureNavigation() const",
                asMETHOD(State, HasCreatureNavigation),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "bool get_HasAnimationComplex() const",
                asMETHOD(State, HasAnimationComplex),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "bool get_HasCachedNavigationSolution() const",
                asMETHOD(State, HasCachedNavigationSolution),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "uint get_ComponentCount() const",
                asMETHOD(State, ComponentCount),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "uint get_AnimationStateHash() const",
                asMETHOD(State, AnimationStateHash),
                asCALL_THISCALL)
            : result;
        result = result >= 0
            ? engine.RegisterObjectMethod(
                "CreatureLocomotionState",
                "Vector3 get_PhysicsPosition() const",
                asMETHOD(State, PhysicsPosition),
                asCALL_THISCALL)
            : result;
        result = result >= 0 ? engine.SetDefaultNamespace("Creature") : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "CreatureLocomotionState@+ InspectLocomotion(Entity@)",
                asFUNCTION(InspectLocomotion),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool MirrorPhysicsWorldPosition(Entity@, Entity@)",
                asFUNCTION(MirrorPhysicsWorldPosition),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void ClearPhysicsWorldPositionMirror()",
                asFUNCTION(ClearPhysicsWorldPositionMirror),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool MirrorAnimationMotion(Entity@, Entity@)",
                asFUNCTION(MirrorAnimationMotion),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void ClearAnimationMotionMirror()",
                asFUNCTION(ClearAnimationMotionMirror),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool RoutePlayerFrameInput(Entity@, Entity@)",
                asFUNCTION(RoutePlayerFrameInput),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void ClearPlayerFrameInputRouter()",
                asFUNCTION(ClearPlayerFrameInputRouter),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool RouteHeroShadow(Entity@, Entity@)",
                asFUNCTION(RouteHeroShadow),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void ClearHeroShadow()",
                asFUNCTION(ClearHeroShadow),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint HeroShadowUpdateCount()",
                asFUNCTION(HeroShadowUpdateCount),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetPhysicsWorldPosition(Entity@, const Vector3 &in)",
                asFUNCTION(SetPhysicsWorldPosition),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint MirroredPhysicsWorldPositionCount()",
                asFUNCTION(MirroredPhysicsWorldPositionCount),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint MirroredAnimationMotionCount()",
                asFUNCTION(MirroredAnimationMotionCount),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint RoutedPlayerFrameCount()",
                asFUNCTION(RoutedPlayerFrameCount),
                asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
