#include "QuestService.h"

#include "../Entity/EntityService.h"
#include "../Native/Addresses.h"
#include "../Native/ScriptTypes.h"

#include <Windows.h>

namespace
{
    using namespace fable::game::native;

    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using QuestActivator = bool(__thiscall*)(
        GameScriptInterface*, const CharString*);
    using QuestPredicate = bool(__thiscall*)(GameScriptInterface*, const CharString*);
}

namespace fable::game
{
    bool QuestService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        activationValidated_ = false;
        apiValidated_ = false;

        native::GameScriptInterface* gameInterface = entities_->Interface().Resolve();
        if (gameInterface == nullptr)
        {
            diagnostics_.Log("Quest API: validation deferred until the game script interface is ready.");
            return true;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(entities_->GameModule());
        __try
        {
            activationValidated_ =
                gameInterface->vtable[native::game_interface_slot::ActivateQuest] ==
                    reinterpret_cast<void*>(base + native::rva::ActivateQuest);
            apiValidated_ =
                gameInterface->vtable[native::game_interface_slot::IsQuestActive] == reinterpret_cast<void*>(base + native::rva::IsQuestActive) &&
                gameInterface->vtable[native::game_interface_slot::IsQuestRegistered] == reinterpret_cast<void*>(base + native::rva::IsQuestRegistered) &&
                gameInterface->vtable[native::game_interface_slot::IsQuestCompleted] == reinterpret_cast<void*>(base + native::rva::IsQuestCompleted) &&
                gameInterface->vtable[native::game_interface_slot::IsQuestFailed] == reinterpret_cast<void*>(base + native::rva::IsQuestFailed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activationValidated_ = false;
            apiValidated_ = false;
        }
        diagnostics_.Log(apiValidated_
            ? "Quest API: current-build quest-state predicate ABI validated."
            : "Quest API: quest-state predicate ABI is not ready yet.");
        return true;
    }

    bool QuestService::Activate(const std::string& questName) const
    {
        if (entities_ == nullptr || questName.empty())
        {
            return false;
        }
        native::GameScriptInterface* const gameInterface =
            entities_->Interface().Resolve();
        if (gameInterface == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(
            entities_->GameModule());
        bool validated = activationValidated_;
        if (!validated)
        {
            __try
            {
                validated = gameInterface->vtable[
                    native::game_interface_slot::ActivateQuest] ==
                    reinterpret_cast<void*>(base + native::rva::ActivateQuest);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                validated = false;
            }
        }
        if (!validated)
        {
            return false;
        }
        activationValidated_ = true;

        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        native::CharString nativeName;
        bool constructed = false;
        bool activated = false;
        __try
        {
            constructString(&nativeName, questName.c_str(), -1);
            constructed = true;
            const auto activate = reinterpret_cast<QuestActivator>(
                gameInterface->vtable[
                    native::game_interface_slot::ActivateQuest]);
            activated = activate(gameInterface, &nativeName);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activated = false;
        }
        if (constructed)
        {
            __try { destroyString(&nativeName); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // The quest request has already crossed the native boundary.
                // Never retry it merely because local CharString cleanup
                // faulted after acceptance.
                diagnostics_.Event(
                    "QuestActivationCleanupFault", questName.c_str());
            }
        }
        return activated;
    }

    bool QuestService::Query(const std::string& questName, std::size_t vtableIndex) const
    {
        if (entities_ == nullptr || questName.empty())
        {
            return false;
        }
        native::GameScriptInterface* gameInterface = entities_->Interface().Resolve();
        if (gameInterface == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(entities_->GameModule());
        bool validated = apiValidated_;
        if (!validated)
        {
            __try
            {
                validated =
                    gameInterface->vtable[native::game_interface_slot::IsQuestActive] == reinterpret_cast<void*>(base + native::rva::IsQuestActive) &&
                    gameInterface->vtable[native::game_interface_slot::IsQuestRegistered] == reinterpret_cast<void*>(base + native::rva::IsQuestRegistered) &&
                    gameInterface->vtable[native::game_interface_slot::IsQuestCompleted] == reinterpret_cast<void*>(base + native::rva::IsQuestCompleted) &&
                    gameInterface->vtable[native::game_interface_slot::IsQuestFailed] == reinterpret_cast<void*>(base + native::rva::IsQuestFailed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                validated = false;
            }
        }
        if (!validated)
        {
            return false;
        }
        if (!apiValidated_)
        {
            apiValidated_ = true;
            diagnostics_.Log("Quest API: current-build quest-state predicate ABI validated lazily.");
        }

        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        native::CharString nativeName;
        bool constructed = false;
        bool result = false;
        __try
        {
            constructString(&nativeName, questName.c_str(), -1);
            constructed = true;
            const auto predicate = reinterpret_cast<QuestPredicate>(
                gameInterface->vtable[vtableIndex]);
            result = predicate(gameInterface, &nativeName);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = false;
        }
        if (constructed)
        {
            __try { destroyString(&nativeName); }
            __except (EXCEPTION_EXECUTE_HANDLER) { result = false; }
        }
        return result;
    }

    bool QuestService::IsActive(const std::string& questName) const
    {
        return Query(questName, native::game_interface_slot::IsQuestActive);
    }

    bool QuestService::IsRegistered(const std::string& questName) const
    {
        return Query(questName, native::game_interface_slot::IsQuestRegistered);
    }

    bool QuestService::IsCompleted(const std::string& questName) const
    {
        return Query(questName, native::game_interface_slot::IsQuestCompleted);
    }

    bool QuestService::IsFailed(const std::string& questName) const
    {
        return Query(questName, native::game_interface_slot::IsQuestFailed);
    }
}
