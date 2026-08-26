#include "FrontEndStartInitializerHook.h"

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
        if (!patch_.Install(
                target,
                target,
                displacedBytes,
                reinterpret_cast<void*>(&FrontEndStartInitializerHook::Invoke),
                displacedBytes))
        {
            diagnostics_.Log(
                "Hook: front-end native initializer patch installation failed.");
            return false;
        }
        original_ = reinterpret_cast<native::FrontEndStartInitializer::Pointer>(
            patch_.Original());
        initializedCallback_ = initializedCallback;
        active_ = this;

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "target=%p replacement=%p trampoline=%p",
            target,
            &FrontEndStartInitializerHook::Invoke,
            patch_.Original());
        diagnostics_.Log("Hook: front-end native initializer installed.");
        diagnostics_.Event(
            "FrontEndNativeInitHookReady",
            detail);
        return true;
#endif
    }

    void FrontEndStartInitializerHook::Shutdown() noexcept
    {
        if (!patch_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: front-end native initializer patch was not removed because target ownership changed.");
            return;
        }
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        initializedCallback_ = nullptr;
        diagnostics_ = {};
    }

    bool FrontEndStartInitializerHook::IsInstalled() const noexcept
    {
        return original_ != nullptr && patch_.IsInstalled() && active_ == this;
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
