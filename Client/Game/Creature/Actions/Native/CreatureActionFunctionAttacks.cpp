#include "CreatureActionFunctions.h"

namespace fable::game::creature::actions::native
{
    bool CreatureActionFunctions::SubmitImmediateAttack(
        HMODULE gameModule,
        void* attacker,
        void* target) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)attacker;
        (void)target;
        return false;
#else
        if (gameModule == nullptr || attacker == nullptr || target == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        std::uint8_t* constructorAddress = nullptr;
        std::uint8_t* destructorAddress = nullptr;
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        if (!Resolve(
                gameModule,
                ImmediateAttackConstructorAddressRva,
                ImmediateAttackConstructorExceptionHandlerRva,
                constructorAddress) ||
            !Resolve(
                gameModule,
                ActionDestructorAddressRva,
                ActionDestructorExceptionHandlerRva,
                destructorAddress))
        {
            return false;
        }

        // The lifecycle observer is installed before multiplayer starts and
        // replaces this entry with a near jump. Calling that entry is correct:
        // the observer records the action and forwards through its verified
        // trampoline. Accept either that installed detour or pristine retail
        // bytes for callers that initialize without the observer.
        bool submitReady = false;
        __try
        {
            submitReady = submitAddress[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitReady = false;
        }
        if (!submitReady)
        {
            std::uint8_t* pristineSubmit = nullptr;
            submitReady = ResolveSubmit(gameModule, pristineSubmit) &&
                pristineSubmit == submitAddress;
        }
        if (!submitReady)
        {
            return false;
        }

        void** const attackVtable = reinterpret_cast<void**>(
            base + ImmediateAttackActionVtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = attackVtable[0] == reinterpret_cast<void*>(
                base + ImmediateAttackActionFirstMethodRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtableValid = false;
        }
        if (!vtableValid)
        {
            return false;
        }

        const auto construct = reinterpret_cast<
            ImmediateAttackConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<ActionDestructorPointer>(
            destructorAddress);
        alignas(void*) unsigned char action[ImmediateAttackStorageSize] = {};
        std::uintptr_t actionContext[2] = {};
        bool constructed = false;
        bool submitted = false;
        __try
        {
            construct(
                action,
                attacker,
                target,
                ImmediateAttackActionTime,
                actionContext);
            *reinterpret_cast<void***>(action) = attackVtable;
            constructed = true;
            submitted = submit(attacker, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }

        bool destroyed = !constructed;
        if (constructed)
        {
            __try
            {
                destroy(action);
                destroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destroyed = false;
            }
        }
        return submitted && destroyed;
#endif
    }

    bool CreatureActionFunctions::SubmitUntargetedAttack(
        HMODULE gameModule,
        void* attacker,
        const float (&targetPosition)[3]) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)attacker;
        (void)targetPosition;
        return false;
#else
        if (gameModule == nullptr || attacker == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        std::uint8_t* constructorAddress = nullptr;
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        if (!Resolve(
                gameModule,
                UntargetedAttackConstructorAddressRva,
                UntargetedAttackConstructorExceptionHandlerRva,
                constructorAddress))
        {
            return false;
        }

        bool submitReady = false;
        __try
        {
            submitReady = submitAddress[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitReady = false;
        }
        if (!submitReady)
        {
            std::uint8_t* pristineSubmit = nullptr;
            submitReady = ResolveSubmit(gameModule, pristineSubmit) &&
                pristineSubmit == submitAddress;
        }
        if (!submitReady)
        {
            return false;
        }

        void** const attackVtable = reinterpret_cast<void**>(
            base + UntargetedAttackActionVtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = attackVtable[0] == reinterpret_cast<void*>(
                base + UntargetedAttackActionDeletingDestructorRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtableValid = false;
        }
        if (!vtableValid)
        {
            return false;
        }

        const auto construct = reinterpret_cast<
            UntargetedAttackConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<
            ActionDeletingDestructorPointer>(attackVtable[0]);
        alignas(void*) unsigned char action[UntargetedAttackStorageSize] = {};
        void* actionContext = nullptr;
        bool constructed = false;
        bool submitted = false;
        __try
        {
            construct(
                action,
                attacker,
                nullptr,
                targetPosition,
                &actionContext);
            constructed = true;
            submitted = submit(attacker, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }

        bool destroyed = !constructed;
        if (constructed)
        {
            __try
            {
                // Scalar deleting destructors use bit zero to request the
                // allocation be freed. This action lives in stack storage.
                destroy(action, 0u);
                destroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destroyed = false;
            }
        }
        return submitted && destroyed;
#endif
    }
}
