#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Abilities/HeroAbilityEvent.h"
#include "Game/HeroPawn/Abilities/Hooks/HeroWillAbilityHook.h"
#include "Game/HeroPawn/Abilities/Hooks/PillarAbilityLifecycleHook.h"

#include <Windows.h>

#include <array>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService final
    {
    public:
        using EventSink = void(*)(void*, const HeroAbilityEvent&);

        bool Initialize(
            game::EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool AttachActionLifecycleObserver(
            game::creature::actions::CreatureActionLifecycleObserver&
                observer);
        bool AddEventSink(EventSink sink, void* context) noexcept;
        void RemoveEventSink(EventSink sink, void* context) noexcept;
        [[nodiscard]] bool SubmitAuthoritative(
            void* hero,
            HeroAbility ability,
            HeroAbilityCommand command = HeroAbilityCommand::Use) noexcept;
        [[nodiscard]] bool Replay(
            void* hero,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;
        [[nodiscard]] bool ApplyProgressionState(
            void* hero,
            HeroAbility ability,
            int state) noexcept;
        [[nodiscard]] bool HasActiveAction(
            void* hero,
            HeroAbility ability) const noexcept;
        void Observe(
            void* component,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;

    private:
        struct SinkEntry final
        {
            EventSink sink = nullptr;
            void* context = nullptr;
        };
        static constexpr std::size_t SinkCapacity = 4;

        [[nodiscard]] bool Submit(
            void* hero,
            HeroAbility ability,
            HeroAbilityCommand command,
            bool publish) noexcept;

        game::EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        hooks::HeroWillAbilityHook hook_;
        hooks::PillarAbilityLifecycleHook pillarLifecycleHook_;
        SRWLOCK sinkLock_ = SRWLOCK_INIT;
        std::array<SinkEntry, SinkCapacity> sinks_ = {};
    };
}
