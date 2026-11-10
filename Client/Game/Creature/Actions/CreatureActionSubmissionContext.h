#pragma once

#include <cstdint>

namespace fable::game::creature::actions
{
    enum class ActionSubmissionOrigin : std::uint8_t
    {
        Retail,
        ReplicatedPresentation,
    };

    // Identifies native SubmitAction calls made to present replicated state.
    // Multiplayer authority policy remains outside the Game layer.
    class ScopedActionSubmissionOrigin final
    {
    public:
        explicit ScopedActionSubmissionOrigin(
            ActionSubmissionOrigin origin) noexcept;
        ~ScopedActionSubmissionOrigin();

        ScopedActionSubmissionOrigin(
            const ScopedActionSubmissionOrigin&) = delete;
        ScopedActionSubmissionOrigin& operator=(
            const ScopedActionSubmissionOrigin&) = delete;

        // MSVC does not permit objects requiring C++ unwinding in functions
        // that use __try. Native helpers at those boundaries must pair these
        // calls manually; ordinary code should construct this RAII scope.
        static void PushForSehBoundary(ActionSubmissionOrigin origin) noexcept;
        static void PopForSehBoundary(ActionSubmissionOrigin origin) noexcept;
        [[nodiscard]] static ActionSubmissionOrigin Current() noexcept;

    private:
        ActionSubmissionOrigin origin_ = ActionSubmissionOrigin::Retail;
    };

    // Carries semantic call-site identity through a synchronous native call.
    // SubmitAction can then bind the accepted concrete action to the exact
    // higher-level request that produced it without guessing by timestamp.
    class ScopedActionSubmissionContext final
    {
    public:
        ScopedActionSubmissionContext(
            std::uint64_t correlationId,
            void* creature) noexcept;
        ~ScopedActionSubmissionContext();

        ScopedActionSubmissionContext(
            const ScopedActionSubmissionContext&) = delete;
        ScopedActionSubmissionContext& operator=(
            const ScopedActionSubmissionContext&) = delete;

        [[nodiscard]] static std::uint64_t NextCorrelationId() noexcept;
        [[nodiscard]] static std::uint64_t CurrentCorrelationId(
            void* creature) noexcept;

    private:
        std::uint64_t previousCorrelationId_ = 0;
        void* previousCreature_ = nullptr;
        bool active_ = false;
    };
}
