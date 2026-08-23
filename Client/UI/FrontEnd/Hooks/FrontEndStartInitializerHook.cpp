#include "FrontEndStartInitializerHook.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fable::ui::front_end
{
    FrontEndStartInitializerHook* FrontEndStartInitializerHook::active_ = nullptr;

    bool FrontEndStartInitializerHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics,
        InitializedCallback initializedCallback)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: front-end native initializer is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another front-end native initializer hook is already active.");
            return false;
        }
        if (initializedCallback == nullptr)
        {
            diagnostics_.Log(
                "Hook: front-end native initializer requires a post-initialization callback.");
            return false;
        }

        std::uint8_t* target = nullptr;
        if (!native::FrontEndStartInitializer::Resolve(gameModule, target))
        {
            diagnostics_.Log(
                "Hook: front-end native initializer definition validation failed; the executable ABI drifted.");
            return false;
        }

        constexpr std::size_t displacedBytes =
            native::FrontEndStartInitializer::DisplacedBytes;
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            diagnostics_.Log(
                "Hook: front-end native initializer trampoline allocation failed.");
            return false;
        }

        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t replacementDisplacement =
            reinterpret_cast<std::intptr_t>(&FrontEndStartInitializerHook::Invoke) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            replacementDisplacement < INT32_MIN ||
            replacementDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: front-end native initializer trampoline or callback is outside the x86 relative-jump range.");
            return false;
        }

        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t replacementRelative =
            static_cast<std::int32_t>(replacementDisplacement);
        std::memcpy(
            patch.data() + 1,
            &replacementRelative,
            sizeof(replacementRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            diagnostics_.Log(
                "Hook: front-end native initializer could not change code protection.");
            return false;
        }

        trampoline_ = trampoline;
        target_ = target;
        original_ = reinterpret_cast<native::FrontEndStartInitializer::Pointer>(
            trampoline_);
        initializedCallback_ = initializedCallback;
        active_ = this;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());

        DWORD discardedProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                previousProtection,
                &discardedProtection))
        {
            diagnostics_.Log(
                "Hook: front-end native initializer installed, but code protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p",
            target,
            &FrontEndStartInitializerHook::Invoke,
            trampoline_);
        diagnostics_.Log("Hook: front-end native initializer installed.");
        diagnostics_.Event(
            "FrontEndNativeInitHookReady",
            detail);
        return true;
#endif
    }

    void FrontEndStartInitializerHook::Shutdown() noexcept
    {
#if defined(_M_IX86)
        if (target_ != nullptr && trampoline_ != nullptr)
        {
            constexpr std::size_t bytes = native::FrontEndStartInitializer::DisplacedBytes;
            DWORD protection = 0;
            if (VirtualProtect(target_, bytes, PAGE_EXECUTE_READWRITE, &protection))
            {
                std::memcpy(target_, trampoline_, bytes);
                FlushInstructionCache(GetCurrentProcess(), target_, bytes);
                DWORD discarded = 0;
                VirtualProtect(target_, bytes, protection, &discarded);
            }
        }
#endif
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        initializedCallback_ = nullptr;
        target_ = nullptr;
        if (trampoline_ != nullptr) VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr;
        diagnostics_ = {};
    }

    bool FrontEndStartInitializerHook::IsInstalled() const noexcept
    {
        return original_ != nullptr && trampoline_ != nullptr && active_ == this;
    }

    void __fastcall FrontEndStartInitializerHook::Invoke(
        void* frontEndStart,
        void*)
    {
        FrontEndStartInitializerHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            if (hook != nullptr)
            {
                hook->diagnostics_.Event(
                    "ClientFailed",
                    "front-end-native-init-trampoline-missing");
            }
            return;
        }

        hook->original_(frontEndStart);
        if (hook->initializedCallback_ != nullptr)
        {
            hook->initializedCallback_(frontEndStart);
        }
    }
}
