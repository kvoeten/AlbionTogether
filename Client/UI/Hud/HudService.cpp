#include "HudService.h"

#include "../../Game/Native/Addresses.h"
#include "../../Game/Native/GameInterface.h"

namespace
{
    using namespace fable::game::native;

    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using AddScreenMessageFunction = void(__thiscall*)(
        GameScriptInterface*, const CharString*, int);
}

namespace fable::ui
{
    bool HudService::Initialize(
        game::native::GameInterfaceAccess& gameInterface,
        const core::Diagnostics& diagnostics)
    {
        gameInterface_ = &gameInterface;
        diagnostics_ = diagnostics;
        return true;
    }

    bool HudService::ShowMessage(const std::string& textGroup, int selectionMethod)
    {
        if (gameInterface_ == nullptr || textGroup.empty())
        {
            return false;
        }
        auto* const interfaceObject = gameInterface_->Resolve();
        void* const entry = gameInterface_->ResolveFunction(
            game::native::game_interface_slot::AddScreenMessage,
            game::native::rva::AddScreenMessage);
        if (interfaceObject == nullptr || entry == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameInterface_->GameModule());
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        game::native::CharString message;
        bool constructed = false;
        bool displayed = false;
        __try
        {
            constructString(&message, textGroup.c_str(), -1);
            constructed = true;
            reinterpret_cast<AddScreenMessageFunction>(entry)(
                interfaceObject,
                &message,
                selectionMethod);
            displayed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            displayed = false;
        }
        if (constructed)
        {
            __try { destroyString(&message); }
            __except (EXCEPTION_EXECUTE_HANDLER) { displayed = false; }
        }
        diagnostics_.Event(
            displayed ? "HudMessageSubmitted" : "HudMessageFailed",
            textGroup.c_str());
        return displayed;
    }
}
