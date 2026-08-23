#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <memory>

namespace fable::automation::runtime { class RuntimeConfiguration; }
namespace fable::game { class GameServiceRuntime; }
namespace fable::multiplayer { class MultiplayerSession; }

namespace fable::automation::runtime
{
    // Owns acceptance and fixture drivers. They are deliberately kept outside
    // ScriptHost so enabling a test scenario does not expand the script host's
    // native service graph.
    class AutomationRunner final
    {
    public:
        AutomationRunner();
        ~AutomationRunner();

        AutomationRunner(const AutomationRunner&) = delete;
        AutomationRunner& operator=(const AutomationRunner&) = delete;

        bool Initialize(
            const RuntimeConfiguration& configuration,
            ::fable::game::GameServiceRuntime& services,
            ::fable::multiplayer::MultiplayerSession& multiplayer,
            const ::fable::core::Diagnostics& diagnostics) noexcept;
        void Tick(float deltaSeconds, bool remotePresentationReady) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool HeroWillFocusedAcceptance() const noexcept
        {
            return heroWillFocusedAcceptance_;
        }

        [[nodiscard]] bool IsTargetReady() const noexcept;
        [[nodiscard]] bool IsCombatComplete() const noexcept;

    private:
        class State;
        std::unique_ptr<State> state_;
        bool heroWillFocusedAcceptance_ = false;
    };
}
