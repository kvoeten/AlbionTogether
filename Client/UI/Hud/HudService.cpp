#include "HudService.h"

#include "../../Game/Native/Addresses.h"
#include "../../Game/Native/GameInterface.h"

#include <cmath>
#include <cstdio>

namespace
{
    using namespace fable::game::native;

    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using AddScreenMessageFunction = void(__thiscall*)(
        GameScriptInterface*, const CharString*, int);
    using DisplayQuestInfoFunction = void(__thiscall*)(
        GameScriptInterface*, bool);
    using AddQuestInfoBarFunction = int(__thiscall*)(
        GameScriptInterface*,
        float,
        float,
        const fable::ui::HudColour*,
        const fable::ui::HudColour*,
        const CharString*,
        const CharString*,
        float);
    using UpdateQuestInfoBarFunction = void(__thiscall*)(
        GameScriptInterface*, int, float, float, float);
    using RemoveQuestInfoElementFunction = void(__thiscall*)(
        GameScriptInterface*, int);

    static_assert(sizeof(fable::ui::HudColour) == 4);
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

    bool HudService::AddHealthBar(
        const float current,
        const float maximum,
        const HudColour& primary,
        const HudColour& secondary,
        const std::string& texture,
        const std::string& text,
        const float scale,
        int& elementId)
    {
        elementId = -1;
        if (gameInterface_ == nullptr || texture.empty() || text.empty() ||
            !std::isfinite(current) || !std::isfinite(maximum) ||
            !std::isfinite(scale) || maximum <= 0.0f || scale <= 0.0f)
        {
            return false;
        }

        auto* const interfaceObject = gameInterface_->Resolve();
        void* const displayEntry = gameInterface_->ResolveFunction(
            game::native::game_interface_slot::DisplayQuestInfo,
            game::native::rva::DisplayQuestInfo);
        void* const addEntry = gameInterface_->ResolveFunction(
            game::native::game_interface_slot::AddQuestInfoBar,
            game::native::rva::AddQuestInfoBar);
        if (interfaceObject == nullptr || displayEntry == nullptr ||
            addEntry == nullptr)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=resolve interface=%p display=%p add=%p",
                interfaceObject,
                displayEntry,
                addEntry);
            diagnostics_.Event("HudHealthBarFailed", detail);
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(
            gameInterface_->GameModule());
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        game::native::CharString textureString;
        game::native::CharString textString;
        bool textureConstructed = false;
        bool textConstructed = false;
        bool added = false;
        const char* stage = "display-quest-info";
        DWORD exceptionCode = 0;
        __try
        {
            reinterpret_cast<DisplayQuestInfoFunction>(displayEntry)(
                interfaceObject, true);
            stage = "construct-texture";
            constructString(&textureString, texture.c_str(), -1);
            textureConstructed = true;
            stage = "construct-label";
            constructString(&textString, text.c_str(), -1);
            textConstructed = true;
            stage = "add-bar";
            elementId = reinterpret_cast<AddQuestInfoBarFunction>(addEntry)(
                interfaceObject,
                current,
                maximum,
                &primary,
                &secondary,
                &textureString,
                &textString,
                scale);
            added = elementId >= 0;
            stage = added ? "complete" : "native-rejected";
        }
        __except (
            exceptionCode = GetExceptionCode(),
            EXCEPTION_EXECUTE_HANDLER)
        {
            elementId = -1;
            added = false;
        }
        bool cleanupSucceeded = true;
        if (textConstructed)
        {
            __try { destroyString(&textString); }
            __except (EXCEPTION_EXECUTE_HANDLER) { cleanupSucceeded = false; }
        }
        if (textureConstructed)
        {
            __try { destroyString(&textureString); }
            __except (EXCEPTION_EXECUTE_HANDLER) { cleanupSucceeded = false; }
        }
        if (added && !cleanupSucceeded)
        {
            diagnostics_.Event(
                "HudHealthBarAcceptedWithCleanupFault",
                text.c_str());
        }
        else if (!added)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=%s native_id=%d exception=0x%08lX texture=%s label=%s",
                stage,
                elementId,
                static_cast<unsigned long>(exceptionCode),
                texture.c_str(),
                text.c_str());
            diagnostics_.Event("HudHealthBarFailed", detail);
        }
        return added;
    }

    bool HudService::UpdateHealthBar(
        const int elementId,
        const float current,
        const float maximum,
        const float scale)
    {
        if (gameInterface_ == nullptr || elementId < 0 ||
            !std::isfinite(current) || !std::isfinite(maximum) ||
            !std::isfinite(scale) || maximum <= 0.0f || scale <= 0.0f)
        {
            return false;
        }
        auto* const interfaceObject = gameInterface_->Resolve();
        void* const entry = gameInterface_->ResolveFunction(
            game::native::game_interface_slot::UpdateQuestInfoBar,
            game::native::rva::UpdateQuestInfoBar);
        if (interfaceObject == nullptr || entry == nullptr)
        {
            return false;
        }
        bool updated = false;
        __try
        {
            reinterpret_cast<UpdateQuestInfoBarFunction>(entry)(
                interfaceObject, elementId, current, maximum, scale);
            updated = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            updated = false;
        }
        return updated;
    }

    bool HudService::RemoveElement(const int elementId)
    {
        if (gameInterface_ == nullptr || elementId < 0)
        {
            return false;
        }
        auto* const interfaceObject = gameInterface_->Resolve();
        void* const entry = gameInterface_->ResolveFunction(
            game::native::game_interface_slot::RemoveQuestInfoElement,
            game::native::rva::RemoveQuestInfoElement);
        if (interfaceObject == nullptr || entry == nullptr)
        {
            return false;
        }
        bool removed = false;
        __try
        {
            reinterpret_cast<RemoveQuestInfoElementFunction>(entry)(
                interfaceObject, elementId);
            removed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            removed = false;
        }
        return removed;
    }
}
