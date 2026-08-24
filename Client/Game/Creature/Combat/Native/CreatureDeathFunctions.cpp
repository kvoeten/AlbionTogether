#include "CreatureDeathFunctions.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"

#include <cstring>

namespace fable::game::creature::combat::native
{
    bool CreatureDeathFunctions::Validate(HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto* const constructor = reinterpret_cast<const std::uint8_t*>(
            base + ConstructorRva);
        void** const vtable = reinterpret_cast<void**>(base + VtableRva);
        bool valid = false;
        __try
        {
            std::uintptr_t exceptionHandler = 0;
            std::memcpy(
                &exceptionHandler,
                constructor + ConstructorPrefix.size(),
                sizeof(exceptionHandler));
            valid = std::memcmp(
                    constructor,
                    ConstructorPrefix.data(),
                    ConstructorPrefix.size()) == 0 &&
                exceptionHandler == base + ConstructorExceptionHandlerRva &&
                vtable[0] == reinterpret_cast<void*>(
                    base + DeletingDestructorRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        auto* const submit = reinterpret_cast<std::uint8_t*>(
            base + SubmitActionRva);
        bool detoured = false;
        __try
        {
            detoured = submit[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            detoured = false;
        }
        if (detoured)
        {
            return true;
        }

        std::uint8_t* pristine = nullptr;
        return ::fable::game::creature::actions::native::
                CreatureActionFunctions::ResolveSubmit(gameModule, pristine) &&
            pristine == submit;
    }

    CreatureDeathFunctions::SubmissionResult CreatureDeathFunctions::Submit(
        HMODULE gameModule,
        void* creature) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)creature;
        return {};
#else
        if (creature == nullptr || !Validate(gameModule))
        {
            return {};
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto construct = reinterpret_cast<ConstructorPointer>(
            base + ConstructorRva);
        const auto submit = reinterpret_cast<SubmitPointer>(
            base + SubmitActionRva);
        const auto destroy = reinterpret_cast<
            ScalarDeletingDestructorPointer>(base + DeletingDestructorRva);
        alignas(void*) unsigned char action[StorageSize] = {};

        bool constructed = false;
        bool accepted = false;
        __try
        {
            // Retail direct-death call sites pass false here. SubmitAction
            // clones the stack input before installing the live action.
            construct(action, creature, false);
            constructed = true;
            accepted = submit(creature, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            accepted = false;
        }

        SubmissionResult result;
        result.invoked = constructed;
        result.accepted = accepted;
        bool cleaned = !constructed;
        if (constructed)
        {
            __try
            {
                destroy(action, 0u);
                cleaned = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                cleaned = false;
            }
        }
        result.cleanupSucceeded = cleaned;
        return result;
#endif
    }
}
