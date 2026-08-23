#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <atomic>
#include <cstdint>

namespace fable::scripting
{
    class ScriptHost;
}

namespace fable::automation::appearance_cycle
{
    struct CharacterSnapshot
    {
        int progressionHealthValue = 0;
        float combatHealth = 0.0f;
        float combatHealthMaximum = 0.0f;
        int regionIndex = 0;
        void* creature = nullptr;
        void* creatureVtable = nullptr;
    };

    class AppearanceCycleScenario final
    {
    public:
        void Initialize(
            scripting::ScriptHost& scriptHost,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        void ObserveScriptEvent(const char* state);
        void Tick(const CharacterSnapshot& character);

        [[nodiscard]] bool IsComplete() const noexcept;

    private:
        bool HeroStateMatchesBaseline(
            const CharacterSnapshot& character,
            const char*& failure) const;
        void FinishWithFailure(const char* detail);

        scripting::ScriptHost* scriptHost_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CharacterSnapshot baseline_ = {};
        std::atomic_bool initialized_{false};
        std::atomic_uint stage_{0};
        std::atomic<std::uint64_t> stageStartedAt_{0};
        std::atomic_uint appearanceFormsReady_{0};
        std::atomic_uint hostilityPoliciesApplied_{0};
        std::atomic_uint heroShadowBindings_{0};
        std::atomic_uint combatRouterBindings_{0};
        std::atomic_bool heroShadowUpdated_{false};
        std::atomic_bool appearanceRestored_{false};
        std::atomic_bool playerFrameInputMovementObserved_{false};
    };
}
