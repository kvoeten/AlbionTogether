#include "CreatureActionSubmissionContext.h"

#include <atomic>

namespace
{
    std::atomic_uint64_t g_nextCorrelationId{0};
    thread_local std::uint64_t g_currentCorrelationId = 0;
    thread_local void* g_currentCreature = nullptr;
    thread_local unsigned int g_replicatedPresentationDepth = 0;
}

namespace fable::game::creature::actions
{
    ScopedActionSubmissionOrigin::ScopedActionSubmissionOrigin(
        const ActionSubmissionOrigin origin) noexcept
        : origin_(origin)
    {
        PushForSehBoundary(origin_);
    }

    ScopedActionSubmissionOrigin::~ScopedActionSubmissionOrigin()
    {
        PopForSehBoundary(origin_);
    }

    void ScopedActionSubmissionOrigin::PushForSehBoundary(
        const ActionSubmissionOrigin origin) noexcept
    {
        if (origin == ActionSubmissionOrigin::ReplicatedPresentation)
        {
            ++g_replicatedPresentationDepth;
        }
    }

    void ScopedActionSubmissionOrigin::PopForSehBoundary(
        const ActionSubmissionOrigin origin) noexcept
    {
        if (origin == ActionSubmissionOrigin::ReplicatedPresentation &&
            g_replicatedPresentationDepth != 0)
        {
            --g_replicatedPresentationDepth;
        }
    }

    ActionSubmissionOrigin ScopedActionSubmissionOrigin::Current() noexcept
    {
        return g_replicatedPresentationDepth != 0
            ? ActionSubmissionOrigin::ReplicatedPresentation
            : ActionSubmissionOrigin::Retail;
    }

    ScopedActionSubmissionContext::ScopedActionSubmissionContext(
        const std::uint64_t correlationId,
        void* const creature) noexcept
        : previousCorrelationId_(g_currentCorrelationId),
          previousCreature_(g_currentCreature),
          active_(correlationId != 0 && creature != nullptr)
    {
        if (active_)
        {
            g_currentCorrelationId = correlationId;
            g_currentCreature = creature;
        }
    }

    ScopedActionSubmissionContext::~ScopedActionSubmissionContext()
    {
        if (active_)
        {
            g_currentCorrelationId = previousCorrelationId_;
            g_currentCreature = previousCreature_;
        }
    }

    std::uint64_t ScopedActionSubmissionContext::NextCorrelationId() noexcept
    {
        std::uint64_t id = g_nextCorrelationId.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (id == 0)
        {
            id = g_nextCorrelationId.fetch_add(
                1, std::memory_order_relaxed) + 1;
        }
        return id;
    }

    std::uint64_t ScopedActionSubmissionContext::CurrentCorrelationId(
        void* const creature) noexcept
    {
        return creature != nullptr && creature == g_currentCreature
            ? g_currentCorrelationId
            : 0;
    }
}
