#include "FrontEndLifecycleHooks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    using LifecycleFunctions = fable::ui::front_end::native::FrontEndLifecycleFunctions;
    constexpr std::size_t kBoundaryCount =
        static_cast<std::size_t>(LifecycleFunctions::Boundary::Count);

    std::uintptr_t g_uiPageDoBeginResume = 0;
    std::uintptr_t g_uiPageDoInitResume = 0;
    std::uintptr_t g_uiPageStartPlayResume = 0;
    std::uintptr_t g_playLoadMapMovieResume = 0;
    std::uintptr_t g_frontEndStartDoInitResume = 0;
    std::uintptr_t g_frontEndStartDoTickResume = 0;

    std::array<std::uintptr_t*, kBoundaryCount> g_resumeSlots = {{
        &g_uiPageDoBeginResume,
        &g_uiPageDoInitResume,
        &g_uiPageStartPlayResume,
        &g_playLoadMapMovieResume,
        &g_frontEndStartDoInitResume,
        &g_frontEndStartDoTickResume,
    }};
}

namespace fable::ui::front_end
{
    struct FrontEndLifecycleThunkAccess final
    {
        static void Dispatch(
            unsigned int boundary,
            void* object,
            const void* frame)
        {
            if (FrontEndLifecycleHooks::active_ == nullptr ||
                boundary >= FrontEndLifecycleHooks::BoundaryCount)
            {
                return;
            }
            FrontEndLifecycleHooks::active_->Dispatch(
                static_cast<FrontEndLifecycleHooks::Boundary>(boundary),
                object,
                frame);
        }
    };

    FrontEndLifecycleHooks* FrontEndLifecycleHooks::active_ = nullptr;

    bool FrontEndLifecycleCallbacks::IsComplete() const noexcept
    {
        return uiPageDoBegin != nullptr &&
            uiPageDoInit != nullptr &&
            uiPageStartPlay != nullptr &&
            playLoadMapMovie != nullptr &&
            frontEndStartDoInit != nullptr &&
            frontEndStartDoTick != nullptr;
    }
}

namespace
{
    void __stdcall DispatchLifecycleBoundary(
        unsigned int boundary,
        void* object,
        const void* frame)
    {
        fable::ui::front_end::FrontEndLifecycleThunkAccess::Dispatch(
            boundary,
            object,
            frame);
    }

#if defined(_M_IX86)
    __declspec(naked) void UiPageDoBeginThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 0
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_uiPageDoBeginResume]
        }
    }

    __declspec(naked) void UiPageDoInitThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 1
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_uiPageDoInitResume]
        }
    }

    __declspec(naked) void UiPageStartPlayThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 2
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_uiPageStartPlayResume]
        }
    }

    __declspec(naked) void PlayLoadMapMovieThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 3
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_playLoadMapMovieResume]
        }
    }

    __declspec(naked) void FrontEndStartDoInitThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 4
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_frontEndStartDoInitResume]
        }
    }

    __declspec(naked) void FrontEndStartDoTickThunk()
    {
        __asm
        {
            pushfd
            pushad
            mov eax, [esp + 28h]
            push eax
            push ecx
            push 5
            call DispatchLifecycleBoundary
            popad
            popfd
            mov eax, [esp + 4]
            inc dword ptr [eax + 18h]
            jmp dword ptr [g_frontEndStartDoTickResume]
        }
    }
#endif

    std::array<void*, kBoundaryCount> LifecycleReplacements()
    {
#if defined(_M_IX86)
        return {{
            UiPageDoBeginThunk,
            UiPageDoInitThunk,
            UiPageStartPlayThunk,
            PlayLoadMapMovieThunk,
            FrontEndStartDoInitThunk,
            FrontEndStartDoTickThunk,
        }};
#else
        return {};
#endif
    }

}

namespace fable::ui::front_end
{
    bool FrontEndLifecycleHooks::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics,
        const FrontEndLifecycleCallbacks& callbacks)
    {
        if (IsInstalled())
        {
            return true;
        }
        if (active_ == this || installed_ ||
            std::any_of(
                patches_.begin(),
                patches_.end(),
                [](const auto& patch) { return patch.IsInstalled(); }))
        {
            diagnostics.Log(
                "Hook: front-end lifecycle installation is partially active; shutdown is required before retrying.");
            return false;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: front-end lifecycle observers are only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another front-end lifecycle observer set is already active.");
            return false;
        }
        if (!callbacks.IsComplete())
        {
            diagnostics_.Log(
                "Hook: front-end lifecycle observers require all six callbacks.");
            return false;
        }

        native::FrontEndLifecycleFunctions::Addresses targets = {};
        const char* failedDefinition = nullptr;
        if (!native::FrontEndLifecycleFunctions::Resolve(
                gameModule,
                targets,
                failedDefinition))
        {
            char message[256] = {};
            std::snprintf(
                message,
                std::size(message),
                "Hook: %s definition validation failed; the executable ABI drifted.",
                failedDefinition != nullptr ? failedDefinition : "front-end lifecycle");
            diagnostics_.Log(message);
            return false;
        }

        const auto replacements = LifecycleReplacements();
        callbacks_ = {{
            callbacks.uiPageDoBegin,
            callbacks.uiPageDoInit,
            callbacks.uiPageStartPlay,
            callbacks.playLoadMapMovie,
            callbacks.frontEndStartDoInit,
            callbacks.frontEndStartDoTick,
        }};
        targets_ = targets;
        for (std::size_t index = 0; index < BoundaryCount; ++index)
        {
            *g_resumeSlots[index] = reinterpret_cast<std::uintptr_t>(
                targets[index] + native::FrontEndLifecycleFunctions::DisplacedBytes);
        }
        active_ = this;
        for (std::size_t index = 0; index < BoundaryCount; ++index)
        {
            if (!patches_[index].Install(
                    targets[index],
                    targets[index],
                    native::FrontEndLifecycleFunctions::DisplacedBytes,
                    replacements[index],
                    native::FrontEndLifecycleFunctions::DisplacedBytes))
            {
                bool rollbackRestored = true;
                for (std::size_t restore = index; restore > 0; --restore)
                {
                    rollbackRestored =
                        patches_[restore - 1].Shutdown() && rollbackRestored;
                }
                if (!rollbackRestored)
                {
                    diagnostics_.Log(
                        "Hook: front-end lifecycle rollback deferred because a target is owned by another hook.");
                    return false;
                }
                active_ = nullptr;
                callbacks_ = {};
                targets_ = {};
                for (std::uintptr_t* const resumeSlot : g_resumeSlots)
                {
                    *resumeSlot = 0;
                }
                diagnostics_.Log(
                    "Hook: a front-end lifecycle boundary patch could not be installed.");
                return false;
            }
        }

        installed_ = true;
        diagnostics_.Log(
            "Hook: six front-end lifecycle observers installed from validated native definitions.");
        diagnostics_.Event(
            "LifecycleHooksReady",
            "ui-begin ui-init start-play load-map-movie front-end-start");
        return true;
#endif
    }

    void FrontEndLifecycleHooks::Shutdown() noexcept
    {
        bool allRestored = true;
        for (std::size_t index = BoundaryCount; index > 0; --index)
        {
            allRestored = patches_[index - 1].Shutdown() && allRestored;
        }
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: front-end lifecycle shutdown deferred because a target boundary is owned by another hook.");
            return;
        }
        if (active_ == this) active_ = nullptr;
        callbacks_ = {};
        targets_ = {};
        for (std::uintptr_t* const resumeSlot : g_resumeSlots)
        {
            *resumeSlot = 0;
        }
        installed_ = false;
        diagnostics_ = {};
    }

    bool FrontEndLifecycleHooks::IsInstalled() const noexcept
    {
        if (!installed_ || active_ != this)
        {
            return false;
        }
        for (const auto& patch : patches_)
        {
            if (!patch.IsInstalled())
            {
                return false;
            }
        }
        return true;
    }

    void FrontEndLifecycleHooks::Dispatch(
        Boundary boundary,
        void* object,
        const void* frame) const
    {
        const auto index = static_cast<std::size_t>(boundary);
        if (index >= callbacks_.size() || callbacks_[index] == nullptr)
        {
            return;
        }
        callbacks_[index](object, frame);
    }
}
