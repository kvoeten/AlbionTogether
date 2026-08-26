#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::equipment::hooks
{
    enum class CreatureCarryingMutationKind : std::uint8_t
    {
        Attached = 1,
        Removed = 2,
    };

    struct CreatureCarryingMutationEvent final
    {
        CreatureCarryingMutationKind kind =
            CreatureCarryingMutationKind::Attached;
        void* component = nullptr;
        void* thing = nullptr;
        std::int32_t carrySlot = 0;
    };

    // Observes the actor-generic CTCCarrying mutation boundary. Hero weapon
    // replication filters these events to the local Hero; NPC equipment can
    // reuse the same seam without installing another native detour.
    class CreatureCarryingMutationObserver final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const CreatureCarryingMutationEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using Attach = void(__thiscall*)(
            void* component,
            void* thing,
            std::int32_t carrySlot,
            bool primary);
        using Remove = std::uintptr_t(__thiscall*)(
            void* component,
            void* thing);

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            core::hooking::InlineHook patch;
            std::size_t displacedBytes = 0;
        };

        static void __fastcall ObserveAttach(
            void* component,
            void* unused,
            void* thing,
            std::int32_t carrySlot,
            bool primary);
        static std::uintptr_t __fastcall ObserveRemove(
            void* component,
            void* unused,
            void* thing);
        bool InstallDetour(
            std::uint8_t* target,
            const void* expected,
            std::size_t expectedSize,
            std::size_t displacedBytes,
            void* replacement,
            Detour& detour) noexcept;
        static bool RestoreDetour(Detour& detour) noexcept;
        void Publish(const CreatureCarryingMutationEvent& event) noexcept;

        static CreatureCarryingMutationObserver* active_;
        core::Diagnostics diagnostics_ = {};
        Attach originalAttach_ = nullptr;
        Remove originalRemove_ = nullptr;
        Detour attachDetour_ = {};
        Detour removeDetour_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
    };
}
