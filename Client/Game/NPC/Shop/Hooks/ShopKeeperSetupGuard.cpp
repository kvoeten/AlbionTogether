#include "ShopKeeperSetupGuard.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

namespace
{
    [[nodiscard]] bool IsReadableRange(
        const void* address,
        std::size_t bytes) noexcept
    {
        if (address == nullptr || bytes == 0)
        {
            return false;
        }

        MEMORY_BASIC_INFORMATION information = {};
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        if (VirtualQuery(
                address,
                &information,
                sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            return false;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(
            information.BaseAddress);
        const auto regionEnd = regionStart + information.RegionSize;
        return regionEnd > start &&
            start <= (std::numeric_limits<std::uintptr_t>::max)() - bytes &&
            start + bytes <= regionEnd;
    }

    template <typename T>
    [[nodiscard]] bool ReadValue(const void* address, T& value) noexcept
    {
        if (!IsReadableRange(address, sizeof(T)))
        {
            return false;
        }
        __try
        {
            value = *static_cast<const T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return true;
    }
}

namespace fable::game::npc::shop::native
{
    std::atomic<ShopKeeperSetupGuard*> ShopKeeperSetupGuard::active_{nullptr};
    std::mutex ShopKeeperSetupGuard::processMutex_;
    core::hooking::InlineHook* ShopKeeperSetupGuard::processHook_ = nullptr;
    std::atomic<ShopKeeperSetupGuard::PredicatePointer>
        ShopKeeperSetupGuard::processOriginal_{nullptr};
    HMODULE ShopKeeperSetupGuard::processModule_ = nullptr;
    core::Diagnostics ShopKeeperSetupGuard::processDiagnostics_ = {};
    std::atomic_uint ShopKeeperSetupGuard::diagnosticMask_{0};

    bool ShopKeeperSetupGuard::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)diagnostics;
        return false;
#else
        if (gameModule == nullptr)
        {
            return false;
        }

        ShopKeeperSetupGuard* active = active_.load(std::memory_order_acquire);
        if (active != nullptr && active != this)
        {
            return false;
        }

        if (processHook_ == nullptr)
        {
            constexpr std::array<std::uint8_t, 6> expected = {
                0x8B, 0x41, 0x04, 0x83, 0xEC, 0x08};
            void* const target = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                rva::SetupWaresPredicate);
            HMODULE callbackModule = nullptr;
            // Pin the callback DLL before changing game text. Pinning the
            // target executable is insufficient for a process-lifetime hook.
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&ShopKeeperSetupGuard::Invoke),
                    &callbackModule))
            {
                return false;
            }
            (void)callbackModule;

            void* const vtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(gameModule) +
                rva::SetupWaresVtable);
            void* vtablePredicate = nullptr;
            if (!IsReadableRange(
                    static_cast<const std::uint8_t*>(vtable) + 0x2C,
                    sizeof(void*)) ||
                !ReadValue(
                    static_cast<const std::uint8_t*>(vtable) + 0x2C,
                    vtablePredicate) ||
                vtablePredicate != target)
            {
                return false;
            }

            auto* hook = new (std::nothrow) core::hooking::InlineHook();
            if (hook == nullptr ||
                !hook->Install(
                    target,
                    expected.data(),
                    expected.size(),
                    reinterpret_cast<void*>(&ShopKeeperSetupGuard::Invoke),
                    expected.size()))
            {
                delete hook;
                return false;
            }
            processHook_ = hook;
            processOriginal_.store(
                reinterpret_cast<PredicatePointer>(processHook_->Original()),
                std::memory_order_release);
            processModule_ = gameModule;
        }

        const PredicatePointer original = processOriginal_.load(
            std::memory_order_acquire);
        if (original == nullptr || processModule_ != gameModule)
        {
            return false;
        }
        {
            std::lock_guard lock(processMutex_);
            processDiagnostics_ = diagnostics;
        }
        active_.store(this, std::memory_order_release);
        return true;
#endif
    }

    bool ShopKeeperSetupGuard::IsInstalled() const noexcept
    {
        return active_.load(std::memory_order_acquire) == this &&
            processOriginal_.load(std::memory_order_acquire) != nullptr &&
            processHook_ != nullptr &&
            processHook_->IsInstalled();
    }

    void ShopKeeperSetupGuard::Shutdown() noexcept
    {
        ShopKeeperSetupGuard* expected = this;
        (void)active_.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        {
            std::lock_guard lock(processMutex_);
            processDiagnostics_ = {};
        }
        // processHook_ and processOriginal_ intentionally remain untouched;
        // this class follows the process-lifetime trampoline contract.
    }

    void ShopKeeperSetupGuard::ReportDeferred(
        ShopKeeperReadiness state) noexcept
    {
        const auto value = static_cast<unsigned int>(state);
        if (value >= sizeof(unsigned int) * 8)
        {
            return;
        }
        const auto bit = 1u << value;
        unsigned int observed = diagnosticMask_.load(std::memory_order_acquire);
        while ((observed & bit) == 0 &&
               !diagnosticMask_.compare_exchange_weak(
                   observed,
                   observed | bit,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire))
        {
        }
        if ((observed & bit) != 0)
        {
            return;
        }

        const char* name = "unknown";
        switch (state)
        {
        case ShopKeeperReadiness::MissingModule: name = "missing-module"; break;
        case ShopKeeperReadiness::MissingThing: name = "missing-thing"; break;
        case ShopKeeperReadiness::NativeThingUnreadable: name = "thing-unreadable"; break;
        case ShopKeeperReadiness::MissingSetupGroupContext: name = "missing-group-context"; break;
        case ShopKeeperReadiness::OwnerThingUnreadable: name = "owner-unreadable"; break;
        case ShopKeeperReadiness::MissingComponent: name = "missing-shopkeeper"; break;
        case ShopKeeperReadiness::WrongComponentType: name = "wrong-shopkeeper-type"; break;
        case ShopKeeperReadiness::ComponentUnreadable: name = "shopkeeper-unreadable"; break;
        case ShopKeeperReadiness::OwnerComponentMissing: name = "missing-owner-shop"; break;
        case ShopKeeperReadiness::LinkedShopUnreadable: name = "linked-shop-unreadable"; break;
        case ShopKeeperReadiness::Ready: return;
        }
        core::Diagnostics diagnostics;
        {
            std::lock_guard lock(processMutex_);
            diagnostics = processDiagnostics_;
        }
        diagnostics.Event("ShopKeeperSetupDeferred", name);
    }

    bool __fastcall ShopKeeperSetupGuard::Invoke(
        void* stateGroup,
        void*)
    {
        // The guard object is never dereferenced after loading active_. The
        // trampoline and immutable process module are process-lifetime, so a
        // concurrent Shutdown only detaches the readiness policy.
        ShopKeeperSetupGuard* const guard = active_.load(
            std::memory_order_acquire);
        const PredicatePointer passthrough = processOriginal_.load(
            std::memory_order_acquire);
        if (guard == nullptr || passthrough == nullptr)
        {
            return passthrough == nullptr ? false : passthrough(stateGroup);
        }
        const auto readiness = ShopKeeperReadinessAdapter::InspectSetupWares(
            processModule_,
            stateGroup);
        if (!readiness.IsReady())
        {
            ReportDeferred(readiness.state);
            return false;
        }
        return passthrough(stateGroup);
    }
}
