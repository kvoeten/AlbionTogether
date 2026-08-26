#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game::hero_pawn::appearance::hooks
{
    enum class HeroAppearanceMutationKind : std::uint8_t
    {
        Clothing = 1,
        AttachableModifier = 2,
    };

    struct HeroAppearanceMutationEvent final
    {
        HeroAppearanceMutationKind kind =
            HeroAppearanceMutationKind::Clothing;
        void* nativeThing = nullptr;
        void* component = nullptr;
    };

    class HeroAppearanceMutationObserver final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const HeroAppearanceMutationEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using ClothingRebuild = void(__thiscall*)(void*, void*);
        // CTCHeroAttachableAppearanceModifiers::RefreshIfDirty returns the
        // component dirty/result byte in AL. Preserve it exactly: callers in
        // the presentation bootstrap use the return value while materializing
        // the promoted Hero pawn.
        using ModifierRefresh = std::uint8_t(__thiscall*)(void*);

        static void __fastcall ObserveClothingRebuild(
            void* component,
            void* unused,
            void* nativeThing);
        static std::uint8_t __fastcall ObserveModifierRefresh(
            void* component,
            void* unused);
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            std::size_t displacedBytes,
            core::hooking::InlineHook& detour) noexcept;
        bool RestoreDetour(core::hooking::InlineHook& detour) noexcept;
        void Notify(const HeroAppearanceMutationEvent& event) noexcept;

        static HeroAppearanceMutationObserver* active_;

        core::Diagnostics diagnostics_ = {};
        ClothingRebuild originalClothingRebuild_ = nullptr;
        ModifierRefresh originalModifierRefresh_ = nullptr;
        core::hooking::InlineHook clothingDetour_;
        core::hooking::InlineHook modifierDetour_;
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint mutationCount_{0};
    };
}
