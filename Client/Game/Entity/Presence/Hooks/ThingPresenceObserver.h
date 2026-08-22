#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Presence/Native/MapwhoFunctions.h"
#include "Game/Entity/Presence/ThingPresenceEvent.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::entity::presence
{
    class ThingPresenceObserver final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const ThingPresenceEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetEventSink(EventSink sink, void* context) noexcept;
        bool RequestUnregister(void* component) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int RegistrationCount() const noexcept;
        [[nodiscard]] unsigned int UnregistrationCount() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 2048;

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::array<
                std::uint8_t,
                native::MapwhoFunctions::DisplacedBytes> originalBytes = {};
        };

        struct ThingContext final
        {
            std::uint64_t uid = 0;
            std::uint64_t villageUid = 0;
            std::uint16_t mapId = 0;
            std::uint16_t definitionIndex = 0;
            std::array<char, 96> scriptName = {};
            game::Vector3 position = {};
            float facing = 0.0f;
            bool hasTransform = false;
            bool gamePersistent = false;
            bool levelPersistent = false;
            bool creature = false;
            bool hasHeroMorph = false;
            bool hasVillageMembership = false;
            bool summonedCreature = false;
            void* thing = nullptr;
            bool registered = false;
            bool readable = false;
        };

        static void __fastcall ObserveRegister(
            void* component,
            void* unused,
            const void* worldPosition);
        static void __fastcall ObserveUnregister(
            void* component,
            void* unused);
        static void __fastcall ObserveUpdate(
            void* component,
            void* unused,
            const void* worldPosition);
        static void* __fastcall ObserveDestructor(
            void* component,
            void* unused,
            unsigned int flags);

        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            Detour& detour) noexcept;
        void RestoreDetour(Detour& detour) noexcept;
        void Report(
            ThingPresencePhase phase,
            void* component,
            const ThingContext& context,
            bool destroyed = false) noexcept;
        static ThingContext ReadThingContext(void* component) noexcept;
        static bool ReadRegistered(void* component) noexcept;
        void Notify(const ThingPresenceEvent& event) noexcept;

        static ThingPresenceObserver* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        native::MapwhoFunctions::RegisterPointer originalRegister_ = nullptr;
        native::MapwhoFunctions::RegisterPointer originalUpdate_ = nullptr;
        native::MapwhoFunctions::UnregisterPointer originalUnregister_ = nullptr;
        native::MapwhoFunctions::DestructorPointer originalDestructor_ = nullptr;
        Detour registerDetour_ = {};
        Detour updateDetour_ = {};
        Detour unregisterDetour_ = {};
        Detour destructorDetour_ = {};
        std::atomic_uint registrationCount_{0};
        std::atomic_uint unregistrationCount_{0};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
    };
}
