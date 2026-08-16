#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/Native/ThingSaveFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::persistence
{
    // Projects a host-authoritative map ID at CThing's retail persistence
    // boundaries. Save projection is temporary; load projection becomes the
    // Thing's placement before derived creature construction completes.
    class ThingSaveProjectionHook final
    {
    public:
        using MapOverrideSink = bool(*)(
            void* context,
            std::uint64_t thingUid,
            std::uint16_t definitionIndex,
            const char* scriptName,
            std::uint16_t& mapId);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetMapOverrideSink(
            MapOverrideSink sink,
            void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 128;

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::array<
                std::uint8_t,
                native::ThingSaveFunctions::DisplacedBytes> originalBytes = {};
        };

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
            Detour& detour) noexcept;
        static void RestoreDetour(Detour& detour) noexcept;
        void ReportProjection(
            const char* phase,
            std::uint64_t thingUid,
            std::uint16_t fromMapId,
            std::uint16_t toMapId) noexcept;

        static ThingSaveProjectionHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::ThingSaveFunctions::SavePointer originalSave_ = nullptr;
        native::ThingSaveFunctions::LoadPointer originalLoad_ = nullptr;
        Detour saveDetour_ = {};
        Detour loadDetour_ = {};
        std::atomic<MapOverrideSink> sink_{nullptr};
        std::atomic<void*> sinkContext_{nullptr};
        std::atomic_uint projectionCount_{0};
    };
}
