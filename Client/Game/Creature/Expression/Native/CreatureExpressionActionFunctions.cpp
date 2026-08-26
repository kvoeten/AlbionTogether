#include "CreatureExpressionActionFunctions.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"
#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"

#include <cstring>

namespace
{
    using fable::game::native::CharString;

    using DefinitionManagerGet = void* (__cdecl*)();
    using ExpressionDefinitionLookup = bool(__thiscall*)(
        void* manager,
        const CharString* definitionName,
        void** definition);
    using CharStringConstructor = CharString* (__thiscall*)(
        CharString* value,
        const char* text,
        int length);
    using CharStringDestructor = void(__thiscall*)(CharString* value);
    using ExpressionActionConstructor = void* (__thiscall*)(
        void* action,
        void* performer,
        void* target,
        const void* definitionPointer,
        std::int32_t firstState,
        std::int32_t secondState,
        std::int32_t firstDuration,
        std::int32_t secondDuration);
    using SubmitAction = bool(__thiscall*)(void* creature, void* action);
    using ScalarDeletingDestructor = void* (__thiscall*)(
        void* action,
        unsigned int flags);
    using DefinitionDelete = void(__thiscall*)(void* definition);
}

namespace fable::game::creature::expression::native
{
    bool CreatureExpressionActionFunctions::Validate(
        HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            const auto* const managerGet = reinterpret_cast<const std::uint8_t*>(
                base + DefinitionManagerGetRva);
            std::uintptr_t managerSlot = 0;
            std::memcpy(&managerSlot, managerGet + 1, sizeof(managerSlot));

            const auto validateSeh = [base](
                const std::uintptr_t addressRva,
                const std::uintptr_t handlerRva)
            {
                const auto* const address =
                    reinterpret_cast<const std::uint8_t*>(base + addressRva);
                std::uintptr_t handler = 0;
                std::memcpy(
                    &handler,
                    address + SehFunctionPrefix.size(),
                    sizeof(handler));
                return std::memcmp(
                           address,
                           SehFunctionPrefix.data(),
                           SehFunctionPrefix.size()) == 0 &&
                    handler == base + handlerRva;
            };

            void** const actionVtable = reinterpret_cast<void**>(
                base + ActionVtableRva);
            valid = managerGet[0] == 0xA1 && managerGet[5] == 0xC3 &&
                managerSlot == base + DefinitionManagerSlotRva &&
                validateSeh(
                    ExpressionDefinitionLookupRva,
                    ExpressionDefinitionLookupExceptionHandlerRva) &&
                validateSeh(
                    ActionConstructorRva,
                    ActionConstructorExceptionHandlerRva) &&
                actionVtable[0] == reinterpret_cast<void*>(
                    base + ActionDeletingDestructorRva);
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
        __try
        {
            if (submit[0] == 0xE9)
            {
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        std::uint8_t* pristine = nullptr;
        return game::creature::actions::native::CreatureActionFunctions::
                ResolveSubmit(gameModule, pristine) &&
            pristine == submit;
    }

    bool CreatureExpressionActionFunctions::ReleaseDefinition(
        void*& definition) noexcept
    {
        if (definition == nullptr)
        {
            return true;
        }

        bool released = false;
        __try
        {
            auto* const referenceCount = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(definition) + sizeof(void*));
            if (*referenceCount != 0)
            {
                --*referenceCount;
                if (*referenceCount == 0)
                {
                    void** const vtable = *reinterpret_cast<void***>(
                        definition);
                    reinterpret_cast<DefinitionDelete>(vtable[1])(
                        definition);
                }
                definition = nullptr;
                released = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            released = false;
        }
        return released;
    }

    CreatureExpressionActionFunctions::SubmissionResult
        CreatureExpressionActionFunctions::Submit(
        HMODULE gameModule,
        void* performer,
        void* target,
        const char* expressionDefinition,
        const std::uint32_t animationId,
        const std::int32_t durationTicks,
        const std::int32_t triggerTicks) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)performer;
        (void)target;
        (void)expressionDefinition;
        (void)animationId;
        (void)durationTicks;
        (void)triggerTicks;
        return {};
#else
        if (gameModule == nullptr || performer == nullptr ||
            expressionDefinition == nullptr || expressionDefinition[0] == '\0' ||
            animationId == 0 || animationId > 0xFFFF ||
            durationTicks <= 0 || durationTicks > 1'000'000 ||
            triggerTicks < 0 || triggerTicks > 1'000'000 ||
            !Validate(gameModule))
        {
            return {};
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        const auto getManager = reinterpret_cast<DefinitionManagerGet>(
            base + DefinitionManagerGetRva);
        const auto lookup = reinterpret_cast<ExpressionDefinitionLookup>(
            base + ExpressionDefinitionLookupRva);
        const auto constructAction = reinterpret_cast<
            ExpressionActionConstructor>(base + ActionConstructorRva);
        const auto submit = reinterpret_cast<SubmitAction>(
            base + SubmitActionRva);
        const auto destroyAction = reinterpret_cast<
            ScalarDeletingDestructor>(base + ActionDeletingDestructorRva);

        SubmissionResult result;
        CharString definitionName;
        void* definition = nullptr;
        bool stringConstructed = false;
        __try
        {
            constructString(&definitionName, expressionDefinition, -1);
            stringConstructed = true;
            void* const manager = getManager();
            result.definitionResolved = manager != nullptr &&
                lookup(manager, &definitionName, &definition) &&
                definition != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result.definitionResolved = false;
        }
        if (stringConstructed)
        {
            __try
            {
                destroyString(&definitionName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                result.cleanupSucceeded = false;
            }
        }
        if (!result.definitionResolved)
        {
            (void)ReleaseDefinition(definition);
            return result;
        }

        alignas(void*) unsigned char action[ActionStorageSize] = {};
        bool constructed = false;
        __try
        {
            constructAction(
                action,
                performer,
                target,
                &definition,
                0,
                0,
                -1,
                -1);
            constructed = true;
            // PerformExpression owns a selected animation-resource record at
            // +0x74. A remote Hero can legitimately select a different local
            // variant for the same semantic expression, and that native
            // action resource supersedes generic AnimationComplex requests.
            // The record is a private 16-byte copy created by the action
            // constructor; replace only its numeric resource identity before
            // SubmitAction clones the action and starts it.
            auto* const animationResource = *reinterpret_cast<std::uint8_t**>(
                action + ActionAnimationResourceOffset);
            if (animationResource != nullptr)
            {
                result.locallySelectedAnimationId =
                    *reinterpret_cast<std::uint32_t*>(animationResource);
                *reinterpret_cast<std::uint32_t*>(animationResource) =
                    animationId;
                result.animationOverridden =
                    *reinterpret_cast<std::uint32_t*>(animationResource) ==
                        animationId;
            }
            if (!result.animationOverridden)
            {
                __leave;
            }
            // The selected animation resource determines these lifecycle
            // ticks. Remote Hero presentations can resolve a different local
            // resource, so preserve the authoritative source action timing
            // before SubmitAction clones and starts the action.
            *reinterpret_cast<std::int32_t*>(
                action + ActionDurationTicksOffset) = durationTicks;
            *reinterpret_cast<std::int32_t*>(
                action + ActionTriggerTicksOffset) = triggerTicks;
            result.invoked = true;
            result.accepted = submit(performer, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result.accepted = false;
        }

        bool actionDestroyed = !constructed;
        if (constructed)
        {
            __try
            {
                // SubmitAction clones ordinary actions. flags=0 destroys the
                // stack input without attempting to free its storage.
                destroyAction(action, 0u);
                actionDestroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                actionDestroyed = false;
            }
        }
        const bool definitionReleased = ReleaseDefinition(definition);
        result.cleanupSucceeded = actionDestroyed && definitionReleased;
        return result;
#endif
    }
}
