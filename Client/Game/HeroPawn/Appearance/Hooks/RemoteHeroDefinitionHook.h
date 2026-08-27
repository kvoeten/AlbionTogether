#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/HeroPawn/Appearance/Native/RemoteHeroRuntimeDefinition.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game::hero_pawn::appearance::hooks
{
    // Builds a private native creature definition from the retail database and
    // exposes it only while AlbionTogether constructs one remote Hero actor.
    // No global definition is mutated and no compiled-definition file is
    // replaced on disk.
    class RemoteHeroDefinitionHook final
    {
    public:
        using ArmToken = std::uint64_t;

        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] ArmToken Arm() noexcept;
        void Cancel(ArmToken token = 0) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsArmed() const noexcept;

    private:
        using DefinitionLookup = native::RemoteHeroRuntimeDefinition::DefinitionLookup;
        using ApplyDefinition = void(__thiscall*)(
            void* thing,
            void* definition);

        static bool __fastcall LookupDefinition(
            void* definitionManager,
            void* unused,
            unsigned int definitionIndex,
            void** result);
        static void __fastcall ApplyRemoteDefinition(
            void* thing,
            void* unused,
            void* definition);

        bool IsActiveOnCurrentThread() const noexcept;
        bool ProvisionHeroComponents(void* thing) noexcept;

        static RemoteHeroDefinitionHook* active_;

        HMODULE gameModule_ = nullptr;
        DefinitionLookup originalLookup_ = nullptr;
        ApplyDefinition originalApply_ = nullptr;
        core::hooking::InlineHook lookupPatch_;
        core::hooking::InlineHook applyPatch_;
        core::Diagnostics diagnostics_ = {};
        native::RemoteHeroRuntimeDefinition runtimeDefinition_;
        std::atomic<DWORD> armedThread_{0};
        std::atomic_uint64_t armToken_{0};
        std::atomic_uint substitutions_{0};
        std::atomic_bool armed_{false};
    };
}
