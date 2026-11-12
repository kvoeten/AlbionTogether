#include "QuestLifecycleHook.h"

#include "Core/Target/ExecutableValidator.h"
#include "Game/Native/Addresses.h"

#include <array>
#include <cstring>

namespace
{
    constexpr std::array<std::uint8_t, 14> SehPrefix = {
        0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00,
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50};
    constexpr std::array<std::uint8_t, 7> DeactivatePrefix = {
        0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x5D, 0x44};

    constexpr std::uintptr_t ActivateScopeTableRva = 0x0252B238;
    constexpr std::uintptr_t CompleteScopeTableRva = 0x0255D80B;
    constexpr std::uintptr_t FailScopeTableRva = 0x0255D838;

    std::array<std::uint8_t, 14> RebasedSehPrefix(
        const std::uintptr_t base,
        const std::uintptr_t scopeTableRva) noexcept
    {
        std::array<std::uint8_t, 14> expected = SehPrefix;
        const auto scopeTable = static_cast<std::uint32_t>(
            base + scopeTableRva);
        std::memcpy(expected.data() + 3, &scopeTable, sizeof(scopeTable));
        return expected;
    }

    bool CopyName(
        const fable::game::native::CharString* const source,
        char* const destination,
        const std::size_t capacity) noexcept
    {
        if (destination == nullptr || capacity == 0)
        {
            return false;
        }
        destination[0] = '\0';
        __try
        {
            if (source == nullptr || source->stringData == nullptr)
            {
                return false;
            }
            const auto* const representation = static_cast<const
                fable::game::native::StringRep*>(source->stringData);
            if (representation->data == nullptr || representation->len <= 0 ||
                static_cast<std::size_t>(representation->len) >= capacity)
            {
                return false;
            }
            const auto length = static_cast<std::size_t>(representation->len);
            std::memcpy(destination, representation->data, length);
            destination[length] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            destination[0] = '\0';
            return false;
        }
    }

    bool ChangedFor(
        const fable::game::quest::QuestLifecycleOperation operation,
        const fable::game::quest::QuestLifecycleState& before,
        const fable::game::quest::QuestLifecycleState& after) noexcept
    {
        using fable::game::quest::QuestLifecycleOperation;
        switch (operation)
        {
        case QuestLifecycleOperation::Activate:
            return !before.active && after.active;
        case QuestLifecycleOperation::Deactivate:
            return before.active && !after.active;
        case QuestLifecycleOperation::Complete:
            return !before.completed && after.completed;
        case QuestLifecycleOperation::Fail:
            return !before.failed && after.failed;
        }
        return false;
    }
}

namespace fable::game::quest
{
    QuestLifecycleHook* QuestLifecycleHook::active_ = nullptr;
    thread_local std::uint32_t QuestLifecycleHook::authoritativeApplyDepth_ = 0;

    bool QuestLifecycleHook::Install(
        const HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        diagnostics.Log(
            "Hook: quest lifecycle observation requires the x86 client.");
        return false;
#else
        if (gameModule == nullptr || IsInstalled() ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }
        if (!core::target::ValidateFableExecutable(gameModule, nullptr))
        {
            diagnostics.Log(
                "Hook: quest lifecycle observation refused an unvalidated executable.");
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto activatePrefix = RebasedSehPrefix(
            base, ActivateScopeTableRva);
        const auto completePrefix = RebasedSehPrefix(
            base, CompleteScopeTableRva);
        const auto failPrefix = RebasedSehPrefix(base, FailScopeTableRva);
        const auto target = [base](const std::uintptr_t rva) noexcept
        {
            return reinterpret_cast<void*>(base + rva);
        };

        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        (void)gameInterface_.Initialize(gameModule, diagnostics);
        active_ = this;
        const bool installed =
            activatePatch_.Install(
                target(native::rva::QuestManagerActivate),
                activatePrefix.data(), activatePrefix.size(),
                reinterpret_cast<void*>(&ActivateObserved),
                activatePrefix.size()) &&
            deactivatePatch_.Install(
                target(native::rva::QuestManagerDeactivate),
                DeactivatePrefix.data(), DeactivatePrefix.size(),
                reinterpret_cast<void*>(&DeactivateObserved),
                DeactivatePrefix.size()) &&
            completePatch_.Install(
                target(native::rva::QuestManagerComplete),
                completePrefix.data(), completePrefix.size(),
                reinterpret_cast<void*>(&CompleteObserved),
                completePrefix.size()) &&
            failPatch_.Install(
                target(native::rva::QuestManagerFail),
                failPrefix.data(), failPrefix.size(),
                reinterpret_cast<void*>(&FailObserved),
                failPrefix.size());
        if (!installed)
        {
            diagnostics_.Event(
                "QuestLifecycleHookBlocked",
                "one or more CQuestManager prologues did not match");
            Shutdown();
            return false;
        }
        originalActivate_ = reinterpret_cast<ActivateFunction>(
            activatePatch_.Original());
        originalDeactivate_ = reinterpret_cast<DeactivateFunction>(
            deactivatePatch_.Original());
        originalComplete_ = reinterpret_cast<ResolveFunction>(
            completePatch_.Original());
        originalFail_ = reinterpret_cast<ResolveFunction>(
            failPatch_.Original());
        diagnostics_.Event(
            "QuestLifecycleHookReady",
            "activate/deactivate/complete/fail are observed after native state changes");
        return true;
#endif
    }

    void QuestLifecycleHook::Shutdown() noexcept
    {
        const bool stopped = failPatch_.Shutdown() &
            completePatch_.Shutdown() & deactivatePatch_.Shutdown() &
            activatePatch_.Shutdown();
        if (!stopped)
        {
            diagnostics_.Log(
                "Hook: quest lifecycle shutdown deferred because target ownership changed.");
            return;
        }
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        originalActivate_ = nullptr;
        originalDeactivate_ = nullptr;
        originalComplete_ = nullptr;
        originalFail_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void QuestLifecycleHook::SetEventSink(
        const EventSink sink,
        void* const context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    bool QuestLifecycleHook::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr &&
            originalActivate_ != nullptr && originalDeactivate_ != nullptr &&
            originalComplete_ != nullptr && originalFail_ != nullptr &&
            activatePatch_.IsInstalled() && deactivatePatch_.IsInstalled() &&
            completePatch_.IsInstalled() && failPatch_.IsInstalled();
    }

    void* QuestLifecycleHook::Manager() const noexcept
    {
        if (gameModule_ == nullptr)
        {
            return nullptr;
        }
        void* manager = nullptr;
        __try
        {
            manager = *reinterpret_cast<void* const*>(
                reinterpret_cast<std::uintptr_t>(gameModule_) +
                native::rva::QuestManagerGlobal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            manager = nullptr;
        }
        return manager;
    }

    bool QuestLifecycleHook::IsManagerReady() const noexcept
    {
        return IsInstalled() && Manager() != nullptr &&
            gameInterface_.Resolve() != nullptr;
    }

    bool QuestLifecycleHook::ReadState(
        native::GameScriptInterface* const gameInterface,
        const native::CharString& name,
        QuestLifecycleState& state) const noexcept
    {
        if (gameInterface == nullptr || gameModule_ == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        bool read = false;
        __try
        {
            if (gameInterface->vtable[native::game_interface_slot::IsQuestActive] !=
                    reinterpret_cast<void*>(base + native::rva::IsQuestActive) ||
                gameInterface->vtable[native::game_interface_slot::IsQuestCompleted] !=
                    reinterpret_cast<void*>(base + native::rva::IsQuestCompleted) ||
                gameInterface->vtable[native::game_interface_slot::IsQuestFailed] !=
                    reinterpret_cast<void*>(base + native::rva::IsQuestFailed))
            {
                return false;
            }
            const auto active = reinterpret_cast<PredicateFunction>(
                gameInterface->vtable[native::game_interface_slot::IsQuestActive]);
            const auto completed = reinterpret_cast<PredicateFunction>(
                gameInterface->vtable[native::game_interface_slot::IsQuestCompleted]);
            const auto failed = reinterpret_cast<PredicateFunction>(
                gameInterface->vtable[native::game_interface_slot::IsQuestFailed]);
            state.active = active(gameInterface, &name);
            state.completed = completed(gameInterface, &name);
            state.failed = failed(gameInterface, &name);
            read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            read = false;
        }
        return read;
    }

    bool QuestLifecycleHook::ReadState(
        const char* const name,
        QuestLifecycleState& state) const noexcept
    {
        state = {};
        if (name == nullptr || name[0] == '\0' || gameModule_ == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto construct = reinterpret_cast<void(__thiscall*)(
            native::CharString*, const char*, int)>(
                base + native::rva::CharStringConstructor);
        const auto destroy = reinterpret_cast<void(__thiscall*)(
            native::CharString*)>(base + native::rva::CharStringDestructor);
        native::CharString nativeName;
        bool constructed = false;
        bool read = false;
        __try
        {
            construct(&nativeName, name, -1);
            constructed = true;
            read = ReadState(gameInterface_.Resolve(), nativeName, state);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            read = false;
        }
        if (constructed)
        {
            __try
            {
                destroy(&nativeName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                read = false;
            }
        }
        return read;
    }

    void QuestLifecycleHook::Publish(
        const QuestLifecycleOperation operation,
        const char* const name,
        const std::uint32_t (&arguments)[3],
        const QuestLifecycleState& before,
        const QuestLifecycleState& after) noexcept
    {
        if (authoritativeApplyDepth_ != 0 ||
            !ChangedFor(operation, before, after))
        {
            return;
        }
        const EventSink sink = eventSink_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        QuestLifecycleEvent event;
        event.operation = operation;
        std::memcpy(event.arguments.data(), arguments, sizeof(arguments));
        event.before = before;
        event.after = after;
        event.threadId = GetCurrentThreadId();
        const std::size_t length = std::strlen(name);
        if (length >= event.name.size())
        {
            return;
        }
        std::memcpy(event.name.data(), name, length + 1);
        void* const context = eventSinkContext_.load(
            std::memory_order_acquire);
        sink(context, event);
    }

    bool QuestLifecycleHook::CallOriginal(
        const QuestLifecycleOperation operation,
        void* const manager,
        native::CharString* const name,
        const std::uint32_t (&arguments)[3]) noexcept
    {
        bool invoked = false;
        __try
        {
            switch (operation)
            {
            case QuestLifecycleOperation::Activate:
                invoked = originalActivate_(
                    manager, name, arguments[0], arguments[1]);
                break;
            case QuestLifecycleOperation::Deactivate:
                originalDeactivate_(manager, name, arguments[0]);
                invoked = true;
                break;
            case QuestLifecycleOperation::Complete:
                originalComplete_(
                    manager, name, arguments[0], arguments[1], arguments[2]);
                invoked = true;
                break;
            case QuestLifecycleOperation::Fail:
                originalFail_(
                    manager, name, arguments[0], arguments[1], arguments[2]);
                invoked = true;
                break;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        return invoked;
    }

    bool QuestLifecycleHook::ApplyAuthoritative(
        const QuestLifecycleOperation operation,
        const char* const name,
        const std::uint32_t (&arguments)[3],
        bool& changed) noexcept
    {
        changed = false;
        if (!IsManagerReady() || name == nullptr || name[0] == '\0')
        {
            return false;
        }
        QuestLifecycleState before;
        if (!ReadState(name, before))
        {
            return false;
        }
        if ((operation == QuestLifecycleOperation::Activate && before.active) ||
            (operation == QuestLifecycleOperation::Deactivate && !before.active) ||
            (operation == QuestLifecycleOperation::Complete && before.completed) ||
            (operation == QuestLifecycleOperation::Fail && before.failed))
        {
            return true;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto construct = reinterpret_cast<void(__thiscall*)(
            native::CharString*, const char*, int)>(
                base + native::rva::CharStringConstructor);
        const auto destroy = reinterpret_cast<void(__thiscall*)(
            native::CharString*)>(base + native::rva::CharStringDestructor);
        native::CharString nativeName;
        bool constructed = false;
        bool invoked = false;
        ++authoritativeApplyDepth_;
        __try
        {
            construct(&nativeName, name, -1);
            constructed = true;
            invoked = CallOriginal(operation, Manager(), &nativeName, arguments);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        --authoritativeApplyDepth_;
        if (constructed)
        {
            __try
            {
                destroy(&nativeName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                diagnostics_.Event(
                    "QuestLifecycleApplyCleanupFault", name);
            }
        }
        QuestLifecycleState after;
        if (!invoked || !ReadState(name, after))
        {
            return false;
        }
        changed = ChangedFor(operation, before, after);
        return changed;
    }

    bool __fastcall QuestLifecycleHook::ActivateObserved(
        void* const manager,
        void*,
        native::CharString* const name,
        const std::uint32_t argument0,
        const std::uint32_t argument1)
    {
        QuestLifecycleHook* const hook = active_;
        if (hook == nullptr || hook->originalActivate_ == nullptr)
        {
            return false;
        }
        char questName[QuestLifecycleEvent::MaximumNameBytes] = {};
        const bool copied = CopyName(name, questName, sizeof(questName));
        QuestLifecycleState before;
        const bool readBefore = copied && hook->ReadState(questName, before);
        const bool accepted = hook->originalActivate_(
            manager, name, argument0, argument1);
        QuestLifecycleState after;
        const bool readAfter = copied && hook->ReadState(questName, after);
        if (accepted && readBefore && readAfter)
        {
            const std::uint32_t arguments[3] = {argument0, argument1, 0};
            hook->Publish(
                QuestLifecycleOperation::Activate,
                questName, arguments, before, after);
        }
        return accepted;
    }

    void __fastcall QuestLifecycleHook::DeactivateObserved(
        void* const manager,
        void*,
        native::CharString* const name,
        const std::uint32_t argument0)
    {
        QuestLifecycleHook* const hook = active_;
        if (hook == nullptr || hook->originalDeactivate_ == nullptr)
        {
            return;
        }
        char questName[QuestLifecycleEvent::MaximumNameBytes] = {};
        const bool copied = CopyName(name, questName, sizeof(questName));
        QuestLifecycleState before;
        const bool readBefore = copied && hook->ReadState(questName, before);
        hook->originalDeactivate_(manager, name, argument0);
        QuestLifecycleState after;
        const bool readAfter = copied && hook->ReadState(questName, after);
        if (readBefore && readAfter)
        {
            const std::uint32_t arguments[3] = {argument0, 0, 0};
            hook->Publish(
                QuestLifecycleOperation::Deactivate,
                questName, arguments, before, after);
        }
    }

    void __fastcall QuestLifecycleHook::CompleteObserved(
        void* const manager,
        void*,
        native::CharString* const name,
        const std::uint32_t argument0,
        const std::uint32_t argument1,
        const std::uint32_t argument2)
    {
        QuestLifecycleHook* const hook = active_;
        if (hook == nullptr || hook->originalComplete_ == nullptr)
        {
            return;
        }
        char questName[QuestLifecycleEvent::MaximumNameBytes] = {};
        const bool copied = CopyName(name, questName, sizeof(questName));
        QuestLifecycleState before;
        const bool readBefore = copied && hook->ReadState(questName, before);
        hook->originalComplete_(manager, name, argument0, argument1, argument2);
        QuestLifecycleState after;
        const bool readAfter = copied && hook->ReadState(questName, after);
        if (readBefore && readAfter)
        {
            const std::uint32_t arguments[3] = {
                argument0, argument1, argument2};
            hook->Publish(
                QuestLifecycleOperation::Complete,
                questName, arguments, before, after);
        }
    }

    void __fastcall QuestLifecycleHook::FailObserved(
        void* const manager,
        void*,
        native::CharString* const name,
        const std::uint32_t argument0,
        const std::uint32_t argument1,
        const std::uint32_t argument2)
    {
        QuestLifecycleHook* const hook = active_;
        if (hook == nullptr || hook->originalFail_ == nullptr)
        {
            return;
        }
        char questName[QuestLifecycleEvent::MaximumNameBytes] = {};
        const bool copied = CopyName(name, questName, sizeof(questName));
        QuestLifecycleState before;
        const bool readBefore = copied && hook->ReadState(questName, before);
        hook->originalFail_(manager, name, argument0, argument1, argument2);
        QuestLifecycleState after;
        const bool readAfter = copied && hook->ReadState(questName, after);
        if (readBefore && readAfter)
        {
            const std::uint32_t arguments[3] = {
                argument0, argument1, argument2};
            hook->Publish(
                QuestLifecycleOperation::Fail,
                questName, arguments, before, after);
        }
    }
}
