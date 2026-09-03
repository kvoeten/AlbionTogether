#include "Game/NPC/Shop/Hooks/ShopKeeperSetupGuard.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using namespace fable::game::npc::shop::native;

    int failures = 0;

    void Check(bool condition, const char* expression)
    {
        if (condition)
        {
            return;
        }
        ++failures;
        std::cerr << "ShopKeeper readiness: " << expression << '\n';
    }

#define CHECK(expression) Check((expression), #expression)

    struct NativeImage final
    {
        std::uint8_t* base = nullptr;
        std::size_t size = 0;

        bool Allocate()
        {
#if !defined(_M_IX86)
            return false;
#else
            size = rva::ShopVtable + 0x2000;
            base = static_cast<std::uint8_t*>(VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE,
                PAGE_NOACCESS));
            return base != nullptr;
#endif
        }

        bool Commit(std::uintptr_t rvaValue)
        {
            return VirtualAlloc(
                base + (rvaValue & ~std::uintptr_t{0xFFF}),
                0x1000,
                MEM_COMMIT,
                PAGE_READWRITE) != nullptr;
        }

        void* At(std::uintptr_t rvaValue) const
        {
            return base + rvaValue;
        }
    };

    struct Graph final
    {
        alignas(void*) std::array<std::uint8_t, 0x2000> bytes{};

        void* At(std::size_t offset)
        {
            return bytes.data() + offset;
        }

        void SetEntries(
            void* thing,
            std::size_t entryOffset,
            std::int32_t type,
            void* component)
        {
            auto* entries = static_cast<
                fable::game::entity::native::ThingComponentEntry*>(
                At(entryOffset));
            entries[0] = {type, component};
            *reinterpret_cast<
                fable::game::entity::native::ThingComponentEntry**>(
                static_cast<std::uint8_t*>(thing) + 0x44) = entries;
            *reinterpret_cast<
                fable::game::entity::native::ThingComponentEntry**>(
                static_cast<std::uint8_t*>(thing) + 0x48) = entries + 1;
        }

        void SetThing(void* thing, void* vtable)
        {
            *static_cast<void**>(thing) = vtable;
        }

        void SetWeak(
            void* object,
            std::size_t wrapperOffset,
            void* header)
        {
            *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(object) + wrapperOffset +
                sizeof(void*)) = header;
        }

        void SetHeader(void* header, void* target, std::int32_t references)
        {
            *static_cast<void**>(header) = target;
            *reinterpret_cast<std::int32_t*>(
                static_cast<std::uint8_t*>(header) + sizeof(void*)) =
                references;
        }
    };

    unsigned int diagnosticEvents = 0;

    void DiagnosticEvent(const char* state, const char*)
    {
        if (state != nullptr && std::strcmp(state, "ShopKeeperSetupDeferred") == 0)
        {
            ++diagnosticEvents;
        }
    }
}

int RunShopKeeperReadinessTests()
{
#if !defined(_M_IX86)
    return 0;
#else
    NativeImage image;
    CHECK(image.Allocate());
    if (image.base == nullptr)
    {
        return failures;
    }
    CHECK(image.Commit(rva::SetupWaresPredicate));
    CHECK(image.Commit(rva::SetupWaresVtable));
    CHECK(image.Commit(rva::ShopVtable));
    CHECK(image.Commit(rva::ShopKeeperVtable));

    auto* const predicate = static_cast<std::uint8_t*>(
        image.At(rva::SetupWaresPredicate));
    constexpr std::array<std::uint8_t, 15> predicateCode = {
        0x8B, 0x41, 0x04,       // mov eax,[ecx+4]
        0x83, 0xEC, 0x08,       // sub esp,8
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0x83, 0xC4, 0x08,
        0xC3};
    std::memcpy(predicate, predicateCode.data(), predicateCode.size());

    auto* const setupVtable = static_cast<std::uint8_t*>(
        image.At(rva::SetupWaresVtable));
    auto* const shopVtable = static_cast<std::uint8_t*>(
        image.At(rva::ShopVtable));
    auto* const keeperVtable = static_cast<std::uint8_t*>(
        image.At(rva::ShopKeeperVtable));
    void* const target = predicate;
    *reinterpret_cast<void**>(setupVtable + 0x2C) = target;
    *reinterpret_cast<void**>(shopVtable + 0x0C) = target;
    *reinterpret_cast<void**>(shopVtable + 0x10) = target;
    *reinterpret_cast<void**>(keeperVtable + 0x0C) = target;

    DWORD oldProtection = 0;
    CHECK(VirtualProtect(
        predicate,
        32,
        PAGE_EXECUTE_READWRITE,
        &oldProtection) != FALSE);
    CHECK(VirtualProtect(
        shopVtable,
        0x40,
        PAGE_READONLY,
        &oldProtection) != FALSE);
    CHECK(VirtualProtect(
        keeperVtable,
        0x40,
        PAGE_READONLY,
        &oldProtection) != FALSE);

    Graph graph;
    void* const merchantThing = graph.At(0x100);
    void* const linkedThing = graph.At(0x300);
    void* const ownerThing = graph.At(0x500);
    void* const keeper = graph.At(0x700);
    void* const linkedShop = graph.At(0x900);
    void* const ownerShop = graph.At(0xB00);
    void* const stateGroup = graph.At(0xD00);
    void* const stateContext = graph.At(0xE00);
    void* const keeperHeader = graph.At(0xF00);

    graph.SetThing(merchantThing, image.At(rva::ShopVtable));
    graph.SetThing(linkedThing, image.At(rva::ShopVtable));
    graph.SetThing(ownerThing, image.At(rva::ShopVtable));
    graph.SetThing(keeper, image.At(rva::ShopKeeperVtable));
    *reinterpret_cast<void**>(static_cast<std::uint8_t*>(stateGroup) + 4) =
        stateContext;
    *reinterpret_cast<void**>(static_cast<std::uint8_t*>(stateContext) + 0x20) =
        merchantThing;
    *reinterpret_cast<void**>(static_cast<std::uint8_t*>(keeper) + 0x04) =
        ownerThing;
    graph.SetWeak(keeper, 0x14, keeperHeader);
    graph.SetHeader(keeperHeader, linkedThing, 1);

    graph.SetThing(linkedShop, image.At(rva::ShopVtable));
    *reinterpret_cast<void**>(static_cast<std::uint8_t*>(linkedShop) + 4) =
        linkedThing;
    graph.SetWeak(linkedShop, 0x10, nullptr);
    graph.SetThing(ownerShop, image.At(rva::ShopVtable));
    *reinterpret_cast<void**>(static_cast<std::uint8_t*>(ownerShop) + 4) =
        ownerThing;
    graph.SetWeak(ownerShop, 0x10, nullptr);
    graph.SetEntries(
        merchantThing,
        0x1200,
        static_cast<std::int32_t>(ShopKeeperComponentType),
        keeper);
    graph.SetEntries(
        linkedThing,
        0x1220,
        static_cast<std::int32_t>(ShopKeeperFallbackComponentType),
        linkedShop);
    graph.SetEntries(
        ownerThing,
        0x1240,
        static_cast<std::int32_t>(ShopKeeperFallbackComponentType),
        ownerShop);

    const auto module = reinterpret_cast<HMODULE>(image.base);
    auto readiness = ShopKeeperReadinessAdapter::InspectSetupWares(
        module,
        stateGroup);
    CHECK(readiness.IsReady());
    CHECK(readiness.linkedShopHeaderPresent);
    CHECK(!readiness.ownerThingPresent);

    *static_cast<void**>(linkedShop) = image.At(rva::ShopKeeperVtable);
    readiness = ShopKeeperReadinessAdapter::InspectSetupWares(module, stateGroup);
    CHECK(readiness.state == ShopKeeperReadiness::LinkedShopUnreadable);
    *static_cast<void**>(linkedShop) = image.At(rva::ShopVtable);

    // A null CTCShopKeeper link follows retail's owner fallback and remains
    // valid; this is distinct from an unreadable non-null header.
    graph.SetWeak(keeper, 0x14, nullptr);
    readiness = ShopKeeperReadinessAdapter::InspectSetupWares(module, stateGroup);
    CHECK(readiness.IsReady());
    CHECK(readiness.ownerThingPresent);
    graph.SetWeak(keeper, 0x14, keeperHeader);
    graph.SetHeader(keeperHeader, linkedThing, 1);

    // The real predicate would dereference this header before selecting an
    // owner; readiness must reject it without entering native code.
    graph.SetWeak(keeper, 0x14, reinterpret_cast<void*>(1));
    readiness = ShopKeeperReadinessAdapter::InspectSetupWares(module, stateGroup);
    CHECK(readiness.state == ShopKeeperReadiness::LinkedShopUnreadable);
    graph.SetWeak(keeper, 0x14, keeperHeader);

    // Verify the vtable relation before installing the production detour.
    *reinterpret_cast<void**>(setupVtable + 0x2C) = image.At(rva::ShopVtable);
    ShopKeeperSetupGuard guard;
    const auto savedPrologueByte = predicate[5];
    predicate[5] = 0x09;
    CHECK(!guard.Install(module));
    predicate[5] = savedPrologueByte;
    CHECK(!guard.Install(module));
    *reinterpret_cast<void**>(setupVtable + 0x2C) = target;

    fable::core::Diagnostics diagnostics;
    diagnostics.event = &DiagnosticEvent;
    CHECK(guard.Install(module, diagnostics));
    CHECK(guard.IsInstalled());
    using Predicate = ShopKeeperSetupGuard::PredicatePointer;
    const auto hooked = reinterpret_cast<Predicate>(target);

    graph.SetWeak(keeper, 0x14, reinterpret_cast<void*>(1));
    CHECK(!hooked(stateGroup));
    CHECK(!hooked(stateGroup));
    CHECK(diagnosticEvents == 1);
    graph.SetWeak(keeper, 0x14, keeperHeader);
    graph.SetHeader(keeperHeader, linkedThing, 1);
    CHECK(hooked(stateGroup));

    guard.Shutdown();
    // Detachment leaves the process-lifetime trampoline in place and native
    // behavior remains available after the observer is gone.
    graph.SetWeak(keeper, 0x14, reinterpret_cast<void*>(1));
    CHECK(hooked(stateGroup));
    return failures;
#endif
}
