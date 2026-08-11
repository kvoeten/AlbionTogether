#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class asIScriptEngine;
class asIScriptFunction;

namespace fable::scripting
{
    class EventBus final
    {
    public:
        EventBus() = default;
        ~EventBus();

        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        void Initialize(asIScriptEngine& engine, const core::Diagnostics& diagnostics);
        std::uint32_t Subscribe(const std::string& eventName, asIScriptFunction* callback);
        bool Unsubscribe(std::uint32_t subscriptionId);
        std::uint32_t Emit(const std::string& eventName, const std::string& detail);
        void UnsubscribeAll();
        void Shutdown();

    private:
        struct Subscription
        {
            std::uint32_t id = 0;
            std::string eventName;
            asIScriptFunction* callback = nullptr;
        };

        bool Execute(
            Subscription& subscription,
            const std::string& eventName,
            const std::string& detail);
        void ReleaseSubscription(Subscription& subscription) noexcept;
        void Compact();

        asIScriptEngine* engine_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::vector<Subscription> subscriptions_;
        std::vector<Subscription> pendingSubscriptions_;
        std::unordered_set<std::uint32_t> cancelledSubscriptions_;
        std::uint32_t nextSubscriptionId_ = 1;
        std::uint32_t emissionDepth_ = 0;
    };
}
