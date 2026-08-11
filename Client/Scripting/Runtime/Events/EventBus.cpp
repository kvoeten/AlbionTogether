#include "EventBus.h"

#include <angelscript.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace fable::scripting
{
    EventBus::~EventBus()
    {
        Shutdown();
    }

    void EventBus::Initialize(
        asIScriptEngine& engine,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        engine_ = &engine;
        diagnostics_ = diagnostics;
    }

    std::uint32_t EventBus::Subscribe(
        const std::string& eventName,
        asIScriptFunction* callback)
    {
        if (engine_ == nullptr || eventName.empty() || callback == nullptr)
        {
            return 0;
        }
        Subscription subscription;
        subscription.id = nextSubscriptionId_++;
        if (subscription.id == 0)
        {
            subscription.id = nextSubscriptionId_++;
        }
        subscription.eventName = eventName;
        subscription.callback = callback;
        subscription.callback->AddRef();
        const std::uint32_t subscriptionId = subscription.id;
        (emissionDepth_ != 0 ? pendingSubscriptions_ : subscriptions_)
            .push_back(std::move(subscription));
        return subscriptionId;
    }

    bool EventBus::Unsubscribe(std::uint32_t subscriptionId)
    {
        if (subscriptionId == 0)
        {
            return false;
        }
        const auto exists = [subscriptionId](const Subscription& subscription)
        {
            return subscription.id == subscriptionId;
        };
        const bool found =
            std::any_of(subscriptions_.begin(), subscriptions_.end(), exists) ||
            std::any_of(pendingSubscriptions_.begin(), pendingSubscriptions_.end(), exists);
        if (!found)
        {
            return false;
        }
        cancelledSubscriptions_.insert(subscriptionId);
        if (emissionDepth_ == 0)
        {
            Compact();
        }
        return true;
    }

    std::uint32_t EventBus::Emit(
        const std::string& eventName,
        const std::string& detail)
    {
        if (engine_ == nullptr || eventName.empty())
        {
            return 0;
        }
        ++emissionDepth_;
        std::uint32_t delivered = 0;
        for (Subscription& subscription : subscriptions_)
        {
            if (cancelledSubscriptions_.count(subscription.id) != 0 ||
                (subscription.eventName != eventName && subscription.eventName != "*"))
            {
                continue;
            }
            if (Execute(subscription, eventName, detail))
            {
                ++delivered;
            }
            else
            {
                cancelledSubscriptions_.insert(subscription.id);
            }
        }
        --emissionDepth_;
        if (emissionDepth_ == 0)
        {
            Compact();
        }
        return delivered;
    }

    bool EventBus::Execute(
        Subscription& subscription,
        const std::string& eventName,
        const std::string& detail)
    {
        asIScriptContext* context = engine_->CreateContext();
        if (context == nullptr)
        {
            diagnostics_.Log("AngelScript events: execution context allocation failed.");
            return false;
        }
        int result = context->Prepare(subscription.callback);
        if (result >= 0)
        {
            context->SetArgObject(0, const_cast<std::string*>(&eventName));
            context->SetArgObject(1, const_cast<std::string*>(&detail));
            result = context->Execute();
        }
        if (result != asEXECUTION_FINISHED)
        {
            char eventDetail[384] = {};
            const char* exception = context->GetExceptionString();
            std::snprintf(
                eventDetail,
                sizeof(eventDetail),
                "subscription=%lu event=%s callback=%s result=%d exception=%s",
                static_cast<unsigned long>(subscription.id),
                eventName.c_str(),
                subscription.callback->GetDeclaration(),
                result,
                exception != nullptr ? exception : "<none>");
            diagnostics_.Event("ScriptEventCallbackFailed", eventDetail);
        }
        context->Release();
        return result == asEXECUTION_FINISHED;
    }

    void EventBus::UnsubscribeAll()
    {
        for (const Subscription& subscription : subscriptions_)
        {
            cancelledSubscriptions_.insert(subscription.id);
        }
        for (const Subscription& subscription : pendingSubscriptions_)
        {
            cancelledSubscriptions_.insert(subscription.id);
        }
        if (emissionDepth_ == 0)
        {
            Compact();
        }
    }

    void EventBus::ReleaseSubscription(Subscription& subscription) noexcept
    {
        if (subscription.callback != nullptr)
        {
            subscription.callback->Release();
            subscription.callback = nullptr;
        }
    }

    void EventBus::Compact()
    {
        const auto removeCancelled = [this](Subscription& subscription)
        {
            if (cancelledSubscriptions_.count(subscription.id) == 0)
            {
                return false;
            }
            ReleaseSubscription(subscription);
            return true;
        };
        subscriptions_.erase(
            std::remove_if(subscriptions_.begin(), subscriptions_.end(), removeCancelled),
            subscriptions_.end());
        pendingSubscriptions_.erase(
            std::remove_if(pendingSubscriptions_.begin(), pendingSubscriptions_.end(), removeCancelled),
            pendingSubscriptions_.end());
        subscriptions_.insert(
            subscriptions_.end(),
            std::make_move_iterator(pendingSubscriptions_.begin()),
            std::make_move_iterator(pendingSubscriptions_.end()));
        pendingSubscriptions_.clear();
        cancelledSubscriptions_.clear();
    }

    void EventBus::Shutdown()
    {
        emissionDepth_ = 0;
        for (Subscription& subscription : subscriptions_)
        {
            ReleaseSubscription(subscription);
        }
        for (Subscription& subscription : pendingSubscriptions_)
        {
            ReleaseSubscription(subscription);
        }
        subscriptions_.clear();
        pendingSubscriptions_.clear();
        cancelledSubscriptions_.clear();
        engine_ = nullptr;
        diagnostics_ = {};
        nextSubscriptionId_ = 1;
    }
}
