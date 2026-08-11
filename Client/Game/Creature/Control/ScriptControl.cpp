#include "ScriptControl.h"

#include "Game/Creature/Locomotion/Native/FollowCreatureActionFunctions.h"
#include "Game/Entity/EntityService.h"
#include "Game/Native/Addresses.h"

#include <Windows.h>

namespace
{
    using namespace fable::game;
    using namespace fable::game::native;

    using MoveToPositionFunction = void(__thiscall*)(
        void*, const Vector3*, float, MoveType, bool, bool);
    using MoveToEntityFunction = void(__thiscall*)(
        void*, const ScriptThing*, float, MoveType, void*, bool, bool, bool);
    using EntityFunction = void(__thiscall*)(void*, const ScriptThing*);
    using VoidFunction = void(__thiscall*)(void*);
    using PredicateFunction = bool(__thiscall*)(void*);
    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using PlayAnimationFunction = void(__thiscall*)(
        void*, const CharString*, bool, void*, bool, bool, bool, bool, bool);
    using PlayCombatAnimationFunction = void(__thiscall*)(
        void*, const CharString*, bool, void*, bool, bool, bool, bool);
    using PlayLoopingAnimationFunction = void(__thiscall*)(
        void*, const CharString*, int, bool, void*, bool, bool, bool, bool, bool);

    bool IsExecutableAddress(const void* address)
    {
        if (address == nullptr)
        {
            return false;
        }
        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information))
        {
            return false;
        }
        const DWORD protection = information.Protect & 0xFF;
        return protection == PAGE_EXECUTE ||
            protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }
}

namespace fable::game
{
    ScriptControl* ScriptControl::Create(
        EntityService& service,
        const native::ScriptThing& entity,
        AiPriority priority)
    {
        native::ScriptControlHandle handle;
        if (!service.StartControl(entity, priority, handle))
        {
            return nullptr;
        }

        auto* control = new ScriptControl(service, handle);
        if (!control->IsValid())
        {
            control->Release();
            return nullptr;
        }
        return control;
    }

    ScriptControl::ScriptControl(
        EntityService& service,
        native::ScriptControlHandle handle)
        : service_(&service), handle_(handle)
    {
    }

    ScriptControl::~ScriptControl()
    {
        ReleaseControl();
    }

    void ScriptControl::AddRef() noexcept
    {
        referenceCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void ScriptControl::Release() noexcept
    {
        if (referenceCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete this;
        }
    }

    void** ScriptControl::ExpertVtable() const
    {
        void** vtable = nullptr;
        __try
        {
            if (handle_.implementation != nullptr)
            {
                vtable = *reinterpret_cast<void***>(handle_.implementation);
                if (vtable == nullptr ||
                    !IsExecutableAddress(vtable[native::scripted_control_slot::MoveToPosition]) ||
                    !IsExecutableAddress(vtable[native::scripted_control_slot::ClearCommands]) ||
                    !IsExecutableAddress(vtable[native::scripted_control_slot::IsPerformingScriptTask]))
                {
                    vtable = nullptr;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtable = nullptr;
        }
        return vtable;
    }

    bool ScriptControl::IsValid() const
    {
        return ExpertVtable() != nullptr;
    }

    bool ScriptControl::MoveToPosition(
        const Vector3& position,
        float radius,
        MoveType moveType,
        bool avoidDynamicObstacles,
        bool ignorePathPreferability)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr)
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            const auto function = reinterpret_cast<MoveToPositionFunction>(
                vtable[native::scripted_control_slot::MoveToPosition]);
            function(
                handle_.implementation,
                &position,
                radius,
                moveType,
                avoidDynamicObstacles,
                ignorePathPreferability);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    bool ScriptControl::MoveToEntity(
        Entity* target,
        float radius,
        MoveType moveType,
        bool avoidDynamicObstacles,
        bool ignorePathPreferability,
        bool faceMovement)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || target == nullptr || !target->IsValid())
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            const auto function = reinterpret_cast<MoveToEntityFunction>(
                vtable[native::scripted_control_slot::MoveToEntity]);
            function(
                handle_.implementation,
                &target->NativeHandle(),
                radius,
                moveType,
                nullptr,
                avoidDynamicObstacles,
                ignorePathPreferability,
                faceMovement);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    bool ScriptControl::Follow(
        Entity* target,
        float distance,
        bool avoidDynamicObstacles)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || target == nullptr || !target->IsValid())
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            const auto function = reinterpret_cast<
                creature::locomotion::native::FollowCreatureActionFunctions::
                    FollowEntityPointer>(
                vtable[native::scripted_control_slot::FollowEntity]);
            function(
                handle_.implementation,
                &target->NativeHandle(),
                distance,
                avoidDynamicObstacles);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    bool ScriptControl::StopFollowing(Entity* target)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || target == nullptr)
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            const auto function = reinterpret_cast<EntityFunction>(
                vtable[native::scripted_control_slot::StopFollowing]);
            function(handle_.implementation, &target->NativeHandle());
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    bool ScriptControl::FireProjectileAt(Entity* target)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || target == nullptr || !target->IsValid())
        {
            return false;
        }
        bool submitted = false;
        __try
        {
            const auto function = reinterpret_cast<EntityFunction>(
                vtable[native::scripted_control_slot::FireProjectileAt]);
            function(handle_.implementation, &target->NativeHandle());
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        return submitted;
    }

    bool ScriptControl::PlayAnimation(
        const std::string& animation,
        bool waitForFinish,
        bool stayOnLastFrame,
        bool allowLooking)
    {
        return PlayAnimationNative(
            animation.c_str(), waitForFinish, stayOnLastFrame, allowLooking);
    }

    bool ScriptControl::PlayAnimationNative(
        const char* animation,
        bool waitForFinish,
        bool stayOnLastFrame,
        bool allowLooking)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || service_ == nullptr || animation == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(service_->GameModule());
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        CharString name;
        bool constructed = false;
        bool submitted = false;
        __try
        {
            constructString(&name, animation, -1);
            constructed = true;
            const auto function = reinterpret_cast<PlayAnimationFunction>(
                vtable[native::scripted_control_slot::PlayAnimation]);
            function(
                handle_.implementation,
                &name,
                stayOnLastFrame,
                nullptr,
                true,
                waitForFinish,
                true,
                false,
                allowLooking);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        if (constructed)
        {
            __try { destroyString(&name); }
            __except (EXCEPTION_EXECUTE_HANDLER) { submitted = false; }
        }
        return submitted;
    }

    bool ScriptControl::PlayCombatAnimation(
        const std::string& animation,
        bool waitForFinish,
        bool allowLooking)
    {
        return PlayCombatAnimationNative(animation.c_str(), waitForFinish, allowLooking);
    }

    bool ScriptControl::PlayCombatAnimationNative(
        const char* animation,
        bool waitForFinish,
        bool allowLooking)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || service_ == nullptr || animation == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(service_->GameModule());
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        CharString name;
        bool constructed = false;
        bool submitted = false;
        __try
        {
            constructString(&name, animation, -1);
            constructed = true;
            const auto function = reinterpret_cast<PlayCombatAnimationFunction>(
                vtable[native::scripted_control_slot::PlayCombatAnimation]);
            function(
                handle_.implementation,
                &name,
                true,
                nullptr,
                true,
                waitForFinish,
                false,
                allowLooking);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        if (constructed)
        {
            __try { destroyString(&name); }
            __except (EXCEPTION_EXECUTE_HANDLER) { submitted = false; }
        }
        return submitted;
    }

    bool ScriptControl::PlayLoopingAnimation(
        const std::string& animation,
        int loops,
        bool useMovement,
        bool allowLooking)
    {
        return PlayLoopingAnimationNative(
            animation.c_str(), loops, useMovement, allowLooking);
    }

    bool ScriptControl::PlayLoopingAnimationNative(
        const char* animation,
        int loops,
        bool useMovement,
        bool allowLooking)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr || service_ == nullptr || animation == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(service_->GameModule());
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        CharString name;
        bool constructed = false;
        bool submitted = false;
        __try
        {
            constructString(&name, animation, -1);
            constructed = true;
            const auto function = reinterpret_cast<PlayLoopingAnimationFunction>(
                vtable[native::scripted_control_slot::PlayLoopingAnimation]);
            function(
                handle_.implementation,
                &name,
                loops,
                useMovement,
                nullptr,
                true,
                false,
                true,
                false,
                allowLooking);
            submitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        if (constructed)
        {
            __try { destroyString(&name); }
            __except (EXCEPTION_EXECUTE_HANDLER) { submitted = false; }
        }
        return submitted;
    }

    bool ScriptControl::UnsheatheWeapons()
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr)
        {
            return false;
        }
        __try
        {
            reinterpret_cast<VoidFunction>(
                vtable[native::scripted_control_slot::UnsheatheWeapons])(
                    handle_.implementation);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ScriptControl::ClearCommands()
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr)
        {
            return false;
        }
        __try
        {
            reinterpret_cast<VoidFunction>(
                vtable[native::scripted_control_slot::ClearCommands])(
                    handle_.implementation);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ScriptControl::ClearAllActions(bool includeLoopingAnimations)
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr)
        {
            return false;
        }
        const std::size_t slot = includeLoopingAnimations
            ? native::scripted_control_slot::ClearAllActionsIncludingLoops
            : native::scripted_control_slot::ClearAllActions;
        __try
        {
            reinterpret_cast<VoidFunction>(vtable[slot])(handle_.implementation);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ScriptControl::ReleaseControl()
    {
        if (handle_.implementation == nullptr && handle_.pointerInfo == nullptr)
        {
            return true;
        }
        return service_ != nullptr && service_->ReleaseControlHandle(handle_);
    }

    bool ScriptControl::IsBusy() const
    {
        void** vtable = ExpertVtable();
        if (vtable == nullptr)
        {
            return false;
        }
        __try
        {
            return reinterpret_cast<PredicateFunction>(
                vtable[native::scripted_control_slot::IsPerformingScriptTask])(
                    handle_.implementation);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
