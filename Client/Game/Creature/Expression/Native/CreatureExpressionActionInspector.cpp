#include "CreatureExpressionActionInspector.h"

#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    using fable::game::native::CharString;

    constexpr std::size_t ExpressionDefinitionOffset = 0xB4;
    constexpr std::size_t ExpressionTargetOffset = 0xB8;
    constexpr std::size_t ActionDurationTicksOffset = 0x10;
    constexpr std::size_t ActionTriggerTicksOffset = 0x14;
    constexpr std::array<std::uint8_t, 12> InstantiationNamePrefix = {
        0x8B, 0x49, 0x25,
        0x8B, 0x44, 0x24, 0x04,
        0x89, 0x08,
        0xC2, 0x04, 0x00,
    };
    constexpr std::array<std::uint8_t, 6> DefinitionCopyPrefix = {
        0x51, 0x56, 0x8B, 0x74, 0x24, 0x0C,
    };
    constexpr std::array<std::uint8_t, 6> WeakPointerGetPrefix = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
    };

    using DefinitionNameToCharString = CharString* (__thiscall*)(
        const void* definitionName,
        CharString* result);
    struct DefinitionNameReference final
    {
        void* value = nullptr;
    };
    using GetInstantiationName = DefinitionNameReference* (__thiscall*)(
        const void* definition,
        DefinitionNameReference* result);
    using CharStringDestructor = void(__thiscall*)(CharString* value);
    using WeakPointerGet = void* (__thiscall*)(void* weakPointer);

    bool CopyDefinition(
        const CharString& source,
        char (&destination)[128]) noexcept
    {
        bool valid = false;
        __try
        {
            if (source.stringData == nullptr)
            {
                return false;
            }
            const auto* const stringData = static_cast<const std::uint8_t*>(
                source.stringData);
            const char* const text = *reinterpret_cast<const char* const*>(
                stringData + sizeof(void*));
            if (text == nullptr)
            {
                return false;
            }
            std::size_t length = 0;
            while (length + 1 < sizeof(destination) &&
                text[length] != '\0')
            {
                destination[length] = text[length];
                ++length;
            }
            destination[length] = '\0';
            valid = length != 0 && text[length] == '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            destination[0] = '\0';
        }
        return valid;
    }
}

namespace fable::game::creature::expression::native
{
    bool CreatureExpressionActionInspector::Inspect(
        HMODULE gameModule,
        void* action,
        const char* actionType,
        CreatureExpressionActionDetails& details) noexcept
    {
        details = {};
        if (gameModule == nullptr || action == nullptr ||
            actionType == nullptr ||
            std::strstr(actionType, "PerformExpression") == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        CharString definitionName;
        DefinitionNameReference definitionNameReference;
        bool definitionConstructed = false;
        bool inspected = false;
        __try
        {
            void* const vtable = *reinterpret_cast<void**>(action);
            const bool knownVtable = vtable == reinterpret_cast<void*>(
                base + game::native::rva::PerformExpressionActionVtable);
            const bool knownExtendedVtable = vtable ==
                reinterpret_cast<void*>(base + game::native::rva::
                    PerformExtendedExpressionActionVtable);
            auto* const copyAddress = reinterpret_cast<std::uint8_t*>(
                base + game::native::rva::DefinitionNameToCharString);
            auto* const getInstantiationNameAddress =
                reinterpret_cast<std::uint8_t*>(base + game::native::rva::
                    ParentDefinitionGetInstantiationName);
            auto* const weakGetAddress = reinterpret_cast<std::uint8_t*>(
                base + game::native::rva::WeakThingPointerGet);
            const bool functionsValid = std::memcmp(
                    copyAddress,
                    DefinitionCopyPrefix.data(),
                    DefinitionCopyPrefix.size()) == 0 &&
                std::memcmp(
                    getInstantiationNameAddress,
                    InstantiationNamePrefix.data(),
                    InstantiationNamePrefix.size()) == 0 &&
                std::memcmp(
                    weakGetAddress,
                    WeakPointerGetPrefix.data(),
                    WeakPointerGetPrefix.size()) == 0;
            void* const definition = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(action) +
                    ExpressionDefinitionOffset);
            if ((knownVtable || knownExtendedVtable) && functionsValid &&
                definition != nullptr)
            {
                const auto getInstantiationName = reinterpret_cast<
                    GetInstantiationName>(getInstantiationNameAddress);
                const auto copy = reinterpret_cast<
                    DefinitionNameToCharString>(copyAddress);
                // CExpressionDef +0x40 is the self-animation label (for
                // example EXPRESSION_SPEAK_SELF), not the definition name.
                // The inherited CParentDefClassBase accessor returns the
                // stable instantiation name accepted by the definition
                // manager on the receiving client.
                if (getInstantiationName(
                        definition,
                        &definitionNameReference) ==
                            &definitionNameReference &&
                    definitionNameReference.value != nullptr &&
                    copy(&definitionNameReference, &definitionName) ==
                        &definitionName)
                {
                    definitionConstructed = true;
                    inspected = CopyDefinition(
                        definitionName, details.definition);
                }
            }
            if (inspected)
            {
                details.durationTicks = *reinterpret_cast<const std::int32_t*>(
                    static_cast<const std::uint8_t*>(action) +
                        ActionDurationTicksOffset);
                details.triggerTicks = *reinterpret_cast<const std::int32_t*>(
                    static_cast<const std::uint8_t*>(action) +
                        ActionTriggerTicksOffset);
                details.target = reinterpret_cast<WeakPointerGet>(
                    weakGetAddress)(
                        static_cast<std::uint8_t*>(action) +
                            ExpressionTargetOffset);
                inspected = details.durationTicks > 0 &&
                    details.durationTicks <= 1'000'000 &&
                    details.triggerTicks >= 0 &&
                    details.triggerTicks <= 1'000'000;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            inspected = false;
        }

        if (definitionConstructed)
        {
            __try
            {
                reinterpret_cast<CharStringDestructor>(
                    base + game::native::rva::CharStringDestructor)(
                        &definitionName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                inspected = false;
            }
        }
        if (!inspected)
        {
            details = {};
        }
        return inspected;
    }
}
