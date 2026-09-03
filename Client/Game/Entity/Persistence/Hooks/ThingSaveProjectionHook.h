#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Entity/Persistence/Native/ThingSaveFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence
{
    // Projects the session's host-authoritative map ID at CThing's retail
    // persistence boundaries. Save projection is temporary; load projection
    // becomes the Thing's placement before derived creature construction.
    class ThingSaveProjectionHook final
    {
    public:
        using MapOverrideSink = bool(*)(
            void* context,
            std::uint64_t thingUid,
            std::uint64_t simulationCreatureUid,
            std::uint16_t definitionIndex,
            const char* scriptName,
            std::uint16_t& mapId);
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetMapOverrideSink(
            MapOverrideSink sink,
            void* context) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 128;

        static void __fastcall SaveProjected(
            void* thing,
            void* unused,
            void* writer);
        static bool __fastcall LoadProjected(
            void* thing,
            void* unused,
            void* reader);
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            core::hooking::InlineHook& detour) noexcept;
        static bool RestoreDetour(core::hooking::InlineHook& detour) noexcept;
        void ReportProjection(
            const char* phase,
            std::uint64_t thingUid,
            std::uint16_t fromMapId,
            std::uint16_t toMapId) noexcept;

        static ThingSaveProjectionHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::ThingSaveFunctions::SavePointer originalSave_ = nullptr;
        native::ThingSaveFunctions::LoadPointer originalLoad_ = nullptr;
        core::hooking::InlineHook saveDetour_;
        core::hooking::InlineHook loadDetour_;
        std::atomic<MapOverrideSink> sink_{nullptr};
        std::atomic<void*> sinkContext_{nullptr};
        std::atomic_uint projectionCount_{0};
    };
}
