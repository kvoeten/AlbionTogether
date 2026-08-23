#include "PlayerCommandPollObserver.h"

#include <intrin.h>

#include <cstdio>
#include <cstring>
#include <iterator>

namespace fable::game::player::input
{
    PlayerCommandPollObserver* PlayerCommandPollObserver::active_ = nullptr;

    bool PlayerCommandPollObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: player command-poll observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }

        void** slot = nullptr;
        native::PlayerCommandPollFunction::Pointer original = nullptr;
        if (!native::PlayerCommandPollFunction::Resolve(
                gameModule,
                &slot,
                original))
        {
            diagnostics_.Log(
                "Hook: CGamePlayerInterface command-poll definition failed validation.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previousProtection))
        {
            return false;
        }
        original_ = original;
        vtableSlot_ = slot;
        active_ = this;
        *slot = reinterpret_cast<void*>(&PlayerCommandPollObserver::Intercept);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        DWORD discardedProtection = 0;
        VirtualProtect(
            slot,
            sizeof(*slot),
            previousProtection,
            &discardedProtection);

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "slot=%p original=%p replacement=%p function_rva=0x%08X",
            slot,
            reinterpret_cast<void*>(original_),
            &PlayerCommandPollObserver::Intercept,
            static_cast<unsigned int>(
                native::PlayerCommandPollFunction::FunctionRva));
        diagnostics_.Event("PlayerCommandPollObserverReady", detail);
        return true;
#endif
    }

    bool PlayerCommandPollObserver::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    void PlayerCommandPollObserver::Shutdown() noexcept
    {
        if (active_ == this && vtableSlot_ != nullptr && original_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(vtableSlot_, sizeof(*vtableSlot_), PAGE_READWRITE, &previousProtection))
            {
                if (*vtableSlot_ == reinterpret_cast<void*>(&Intercept))
                {
                    *vtableSlot_ = reinterpret_cast<void*>(original_);
                    FlushInstructionCache(GetCurrentProcess(), vtableSlot_, sizeof(*vtableSlot_));
                }
                DWORD discarded = 0;
                VirtualProtect(vtableSlot_, sizeof(*vtableSlot_), previousProtection, &discarded);
            }
            active_ = nullptr;
        }
        original_ = nullptr;
        vtableSlot_ = nullptr;
        gameModule_ = nullptr;
        observedCommandCount_.store(0, std::memory_order_release);
        diagnostics_ = {};
    }

    bool __fastcall PlayerCommandPollObserver::Intercept(
        void* gamePlayerInterface,
        void*,
        void* outputCommand)
    {
        PlayerCommandPollObserver* const observer = active_;
        if (observer == nullptr || observer->original_ == nullptr)
        {
            return false;
        }

        const bool returned = observer->original_(
            gamePlayerInterface,
            outputCommand);
        if (!returned || outputCommand == nullptr)
        {
            return returned;
        }

        const unsigned int ordinal = observer->observedCommandCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        unsigned char bytes[40] = {};
        bool copied = false;
        __try
        {
            std::memcpy(bytes, outputCommand, sizeof(bytes));
            copied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            copied = false;
        }

        unsigned int commandKind = 0;
        if (copied)
        {
            std::memcpy(&commandKind, bytes, sizeof(commandKind));
        }
        constexpr unsigned int playerAbilityCommandKind = 0x16;
        const bool playerAbility = commandKind == playerAbilityCommandKind;
        if (ordinal <= 12 || playerAbility)
        {

            const auto base = reinterpret_cast<std::uintptr_t>(
                observer->gameModule_);
            const auto caller = reinterpret_cast<std::uintptr_t>(
                _ReturnAddress());
            char detail[640] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u caller=%p caller_rva=%s0x%08X copied=%s command=%02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X%02X",
                ordinal,
                reinterpret_cast<void*>(caller),
                caller >= base ? "" : "outside-game/",
                caller >= base ? static_cast<unsigned int>(caller - base) : 0,
                copied ? "true" : "false",
                bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]);
            observer->diagnostics_.Event("PlayerCommandPolled", detail);
        }
        if (playerAbility)
        {
            char detail[384] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u command=%02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X%02X source=CGamePlayerInterface::PollCommand",
                ordinal,
                bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]);
            observer->diagnostics_.Event("PlayerAbilityCommandPolled", detail);
        }
        return returned;
    }
}
