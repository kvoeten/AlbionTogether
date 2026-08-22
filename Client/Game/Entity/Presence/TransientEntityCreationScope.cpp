#include "TransientEntityCreationScope.h"

namespace fable::game::entity::presence
{
    thread_local unsigned int TransientEntityCreationScope::depth_ = 0;

    TransientEntityCreationScope::TransientEntityCreationScope(
        bool enabled) noexcept
        : active_(enabled)
    {
        if (active_)
        {
            ++depth_;
        }
    }

    TransientEntityCreationScope::~TransientEntityCreationScope()
    {
        if (active_)
        {
            --depth_;
        }
    }

    bool TransientEntityCreationScope::IsActive() noexcept
    {
        return depth_ != 0;
    }
}
