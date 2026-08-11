#include "GameInterface.h"

#include "Addresses.h"

namespace fable::game::native
{
    bool GameInterfaceAccess::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        if (Resolve() == nullptr)
        {
            // The retail interface is created after the world begins loading.
            // Static executable validation happens at the client boundary, so
            // not-ready is expected during early framework initialization.
            diagnostics_.Log("Game interface: initialized; runtime object is not ready yet.");
        }
        return gameModule_ != nullptr;
    }

    GameScriptInterface* GameInterfaceAccess::Resolve() const
    {
        if (gameModule_ == nullptr)
        {
            return nullptr;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        GameScriptInterface* gameInterface = nullptr;
        __try
        {
            gameInterface = *reinterpret_cast<GameScriptInterface**>(
                base + rva::GameScriptInterfaceSlot);
            if (gameInterface == nullptr ||
                gameInterface->vtable != reinterpret_cast<void**>(
                    base + rva::GameScriptInterfaceVtable) ||
                gameInterface->vtable[game_interface_slot::GetHero] !=
                    reinterpret_cast<void*>(base + rva::GetHero) ||
                gameInterface->vtable[game_interface_slot::GetThingWithScriptName] !=
                    reinterpret_cast<void*>(base + rva::GetThingWithScriptName) ||
                gameInterface->vtable[game_interface_slot::CreateCreature] !=
                    reinterpret_cast<void*>(base + rva::CreateCreature) ||
                gameInterface->vtable[game_interface_slot::StartScriptingEntity] !=
                    reinterpret_cast<void*>(base + rva::StartScriptingEntity) ||
                gameInterface->vtable[game_interface_slot::TeleportThing] !=
                    reinterpret_cast<void*>(base + rva::TeleportThing) ||
                gameInterface->vtable[game_interface_slot::SetAttackImmediately] !=
                    reinterpret_cast<void*>(base + rva::SetAttackImmediately) ||
                gameInterface->vtable[game_interface_slot::AddScreenMessage] !=
                    reinterpret_cast<void*>(base + rva::AddScreenMessage))
            {
                gameInterface = nullptr;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gameInterface = nullptr;
        }
        return gameInterface;
    }

    void* GameInterfaceAccess::ResolveFunction(
        std::size_t vtableIndex,
        std::uintptr_t expectedRva) const
    {
        GameScriptInterface* gameInterface = Resolve();
        if (gameInterface == nullptr)
        {
            return nullptr;
        }
        void* function = nullptr;
        __try
        {
            function = gameInterface->vtable[vtableIndex];
            if (function != reinterpret_cast<void*>(
                    reinterpret_cast<std::uintptr_t>(gameModule_) + expectedRva))
            {
                function = nullptr;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            function = nullptr;
        }
        return function;
    }

    HMODULE GameInterfaceAccess::GameModule() const noexcept
    {
        return gameModule_;
    }
}
