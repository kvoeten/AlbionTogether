#pragma once

namespace fable::game::entity::presence
{
    // Marks native entities constructed synchronously by an action whose own
    // lifecycle already replicates. The general world roster must not adopt
    // those temporary effects as persistent NPCs.
    class TransientEntityCreationScope final
    {
    public:
        explicit TransientEntityCreationScope(bool enabled = true) noexcept;
        ~TransientEntityCreationScope();

        TransientEntityCreationScope(
            const TransientEntityCreationScope&) = delete;
        TransientEntityCreationScope& operator=(
            const TransientEntityCreationScope&) = delete;

        [[nodiscard]] static bool IsActive() noexcept;

    private:
        bool active_ = false;
        static thread_local unsigned int depth_;
    };
}
