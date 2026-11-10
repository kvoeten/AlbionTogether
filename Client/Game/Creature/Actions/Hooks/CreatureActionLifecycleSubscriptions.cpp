#include "CreatureActionLifecycleObserver.h"

#include <array>

namespace fable::game::creature::actions
{
    bool CreatureActionLifecycleObserver::AddEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                ReleaseSRWLockExclusive(&eventSinkLock_);
                return true;
            }
        }
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink == nullptr)
            {
                subscription.context = context;
                subscription.sink = sink;
                ReleaseSRWLockExclusive(&eventSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
        return false;
    }

    void CreatureActionLifecycleObserver::RemoveEventSink(
        EventSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            if (subscription.sink != sink || subscription.context != context)
            {
                continue;
            }
            subscription = {};
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
    }

    bool CreatureActionLifecycleObserver::AddPostUpdateSink(
        PostUpdateSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&postUpdateSinkLock_);
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                ReleaseSRWLockExclusive(&postUpdateSinkLock_);
                return true;
            }
        }
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == nullptr)
            {
                subscription = {sink, context};
                ReleaseSRWLockExclusive(&postUpdateSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&postUpdateSinkLock_);
        return false;
    }

    void CreatureActionLifecycleObserver::RemovePostUpdateSink(
        PostUpdateSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&postUpdateSinkLock_);
        for (PostUpdateSubscription& subscription : postUpdateSinks_)
        {
            if (subscription.sink == sink && subscription.context == context)
            {
                subscription = {};
            }
        }
        ReleaseSRWLockExclusive(&postUpdateSinkLock_);
    }

    void CreatureActionLifecycleObserver::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&eventSinkLock_);
        for (EventSubscription& subscription : eventSinks_)
        {
            subscription = {};
        }
        if (sink != nullptr)
        {
            eventSinks_[0] = {sink, context};
        }
        ReleaseSRWLockExclusive(&eventSinkLock_);
    }

    void CreatureActionLifecycleObserver::SetAuthorityGate(
        AuthorityGate gate,
        void* context) noexcept
    {
        if (gate == nullptr)
        {
            authorityGate_.store(nullptr, std::memory_order_release);
            authorityGateContext_.store(nullptr, std::memory_order_release);
            return;
        }
        authorityGateContext_.store(context, std::memory_order_release);
        authorityGate_.store(gate, std::memory_order_release);
    }

    void CreatureActionLifecycleObserver::Notify(
        const CreatureActionLifecycleEvent& event) noexcept
    {
        std::array<EventSubscription, EventSinkCapacity> subscriptions = {};
        AcquireSRWLockShared(&eventSinkLock_);
        subscriptions = eventSinks_;
        ReleaseSRWLockShared(&eventSinkLock_);
        for (const EventSubscription& subscription : subscriptions)
        {
            if (subscription.sink == nullptr)
            {
                continue;
            }
            subscription.sink(subscription.context, event);
        }
    }

    void CreatureActionLifecycleObserver::NotifyPostUpdate(
        void* creature) noexcept
    {
        std::array<PostUpdateSubscription, PostUpdateSinkCapacity>
            subscriptions = {};
        AcquireSRWLockShared(&postUpdateSinkLock_);
        subscriptions = postUpdateSinks_;
        ReleaseSRWLockShared(&postUpdateSinkLock_);
        for (const PostUpdateSubscription& subscription : subscriptions)
        {
            if (subscription.sink != nullptr)
            {
                subscription.sink(subscription.context, *this, creature);
            }
        }
    }
}
