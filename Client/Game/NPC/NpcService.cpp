#include "NpcService.h"

#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "../Entity/EntityService.h"
#include "../Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <cstdio>
#include <cstdint>

namespace fable::game
{
    bool NpcService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        return entities.GameModule() != nullptr;
    }

    Entity* NpcService::Spawn(
        const std::string& definition,
        const Vector3& position,
        const std::string& scriptName)
    {
        Entity* const npc = entities_ != nullptr
            ? entities_->CreateCreature(definition, position, scriptName)
            : nullptr;
        LogComponentStack(npc, definition);
        return npc;
    }

    ScriptControl* NpcService::TakeControl(Entity* npc, AiPriority priority)
    {
        if (npc == nullptr)
        {
            diagnostics_.Log("NPC API: cannot take control of a null entity.");
            return nullptr;
        }
        return npc->AcquireControl(priority);
    }

    void NpcService::LogComponentStack(
        Entity* npc,
        const std::string& definition)
    {
        constexpr std::size_t kMaximumLoggedStacks = 3;
        constexpr std::size_t kMaximumLoggedComponents = 128;
        if (entities_ == nullptr || npc == nullptr ||
            componentStacksLogged_ >= kMaximumLoggedStacks)
        {
            return;
        }

        void* const nativeThing = entities_->ResolveNative(npc->NativeHandle());
        creature::locomotion::CreatureModeManagerObserver::WatchOwner(
            nativeThing);
        entity::native::ThingComponentRange range;
        if (nativeThing == nullptr ||
            !entity::native::ThingComponentAccess::ReadRange(nativeThing, range))
        {
            diagnostics_.Event(
                "ScriptCreatureComponentStackUnavailable",
                definition.c_str());
            return;
        }

        ++componentStacksLogged_;
        const auto moduleBase = reinterpret_cast<std::uintptr_t>(
            entities_->GameModule());
        const std::size_t count = range.count < kMaximumLoggedComponents
            ? range.count
            : kMaximumLoggedComponents;
        for (std::size_t index = 0; index < count; ++index)
        {
            void* vtable = nullptr;
            bool readable = false;
            __try
            {
                vtable = range.begin[index].instance != nullptr
                    ? *reinterpret_cast<void**>(range.begin[index].instance)
                    : nullptr;
                readable = vtable != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                readable = false;
            }

            const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
            const std::uintptr_t vtableRva = readable && vtableAddress >= moduleBase
                ? vtableAddress - moduleBase
                : 0;
            char detail[384] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "definition=%s stack=%zu index=%zu/%zu component_id=0x%02X instance=%p vtable=%p vtable_rva=0x%08lX readable=%s",
                definition.c_str(),
                componentStacksLogged_,
                index,
                range.count,
                static_cast<unsigned int>(range.begin[index].type),
                range.begin[index].instance,
                vtable,
                static_cast<unsigned long>(vtableRva),
                readable ? "true" : "false");
            diagnostics_.Event("ScriptCreatureComponentObserved", detail);
        }
    }
}
