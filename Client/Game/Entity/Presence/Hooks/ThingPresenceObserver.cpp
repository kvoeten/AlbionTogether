#include "ThingPresenceObserver.h"

#include "Game/Entity/Presence/TransientEntityCreationScope.h"

#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"
#include "Game/Creature/Look/Native/CreatureLookFunctions.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fable::game::entity::presence
{
    ThingPresenceObserver* ThingPresenceObserver::active_ = nullptr;

    bool ThingPresenceObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: Thing presence observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another Thing presence observer is already active.");
            return false;
        }
        if (active_ == this ||
            originalRegister_ != nullptr || originalUpdate_ != nullptr ||
            originalUnregister_ != nullptr || originalDestructor_ != nullptr ||
            registerDetour_.IsInstalled() || updateDetour_.IsInstalled() ||
            unregisterDetour_.IsInstalled() || destructorDetour_.IsInstalled())
        {
            diagnostics_.Log(
                "Hook: Thing presence installation is partially active; shutdown is required before retrying.");
            return false;
        }

        std::uint8_t* registerTarget = nullptr;
        std::uint8_t* updateTarget = nullptr;
        std::uint8_t* unregisterTarget = nullptr;
        std::uint8_t* destructorTarget = nullptr;
        if (!native::MapwhoFunctions::ResolveRegister(
                gameModule,
                registerTarget) ||
            !native::MapwhoFunctions::ResolveUpdate(
                gameModule,
                updateTarget) ||
            !native::MapwhoFunctions::ResolveUnregister(
                gameModule,
                unregisterTarget) ||
            !native::MapwhoFunctions::ResolveDestructor(
                gameModule,
                destructorTarget))
        {
            diagnostics_.Log(
                "Hook: CTCMapwho presence definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                registerTarget,
                reinterpret_cast<void*>(&ThingPresenceObserver::ObserveRegister),
                registerDetour_))
        {
            active_ = nullptr;
            gameModule_ = nullptr;
            diagnostics_.Log(
                "Hook: CTCMapwho registration detour installation failed.");
            return false;
        }
        originalRegister_ =
            reinterpret_cast<native::MapwhoFunctions::RegisterPointer>(
                registerDetour_.Original());

        if (!InstallDetour(
                updateTarget,
                reinterpret_cast<void*>(&ThingPresenceObserver::ObserveUpdate),
                updateDetour_))
        {
            const bool rollbackRestored = RestoreDetour(registerDetour_);
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: CTCMapwho registration rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalRegister_ = nullptr;
            active_ = nullptr;
            gameModule_ = nullptr;
            diagnostics_.Log(
                "Hook: CTCMapwho position-update detour installation failed.");
            return false;
        }
        originalUpdate_ =
            reinterpret_cast<native::MapwhoFunctions::RegisterPointer>(
                updateDetour_.Original());

        if (!InstallDetour(
                unregisterTarget,
                reinterpret_cast<void*>(&ThingPresenceObserver::ObserveUnregister),
                unregisterDetour_))
        {
            bool rollbackRestored = true;
            rollbackRestored = RestoreDetour(updateDetour_) && rollbackRestored;
            rollbackRestored = RestoreDetour(registerDetour_) && rollbackRestored;
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: CTCMapwho position-update rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalUpdate_ = nullptr;
            originalRegister_ = nullptr;
            active_ = nullptr;
            gameModule_ = nullptr;
            diagnostics_.Log(
                "Hook: CTCMapwho unregistration detour installation failed.");
            return false;
        }
        originalUnregister_ =
            reinterpret_cast<native::MapwhoFunctions::UnregisterPointer>(
                unregisterDetour_.Original());

        if (!InstallDetour(
                destructorTarget,
                reinterpret_cast<void*>(&ThingPresenceObserver::ObserveDestructor),
                destructorDetour_))
        {
            bool rollbackRestored = true;
            rollbackRestored = RestoreDetour(unregisterDetour_) && rollbackRestored;
            rollbackRestored = RestoreDetour(updateDetour_) && rollbackRestored;
            rollbackRestored = RestoreDetour(registerDetour_) && rollbackRestored;
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: CTCMapwho unregistration rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalUnregister_ = nullptr;
            originalUpdate_ = nullptr;
            originalRegister_ = nullptr;
            active_ = nullptr;
            gameModule_ = nullptr;
            diagnostics_.Log(
                "Hook: CTCMapwho destructor detour installation failed.");
            return false;
        }
        originalDestructor_ =
            reinterpret_cast<native::MapwhoFunctions::DestructorPointer>(
                destructorDetour_.Original());

        char detail[640] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "register=%p register_trampoline=%p update=%p update_trampoline=%p unregister=%p unregister_trampoline=%p destructor=%p destructor_trampoline=%p event_limit=%u",
            registerTarget,
            registerDetour_.Original(),
            updateTarget,
            updateDetour_.Original(),
            unregisterTarget,
            unregisterDetour_.Original(),
            destructorTarget,
            destructorDetour_.Original(),
            DiagnosticEventLimit);
        diagnostics_.Log(
            "Hook: native CTCMapwho presence-boundary observation installed.");
        diagnostics_.Event("ThingPresenceObserverReady", detail);
        return true;
#endif
    }

    void ThingPresenceObserver::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(destructorDetour_) && allRestored;
        allRestored = RestoreDetour(unregisterDetour_) && allRestored;
        allRestored = RestoreDetour(updateDetour_) && allRestored;
        allRestored = RestoreDetour(registerDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: Thing presence shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetEventSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalDestructor_ = nullptr;
        originalUnregister_ = nullptr;
        originalUpdate_ = nullptr;
        originalRegister_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void ThingPresenceObserver::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    bool ThingPresenceObserver::RequestUnregister(void* component) noexcept
    {
        if (active_ != this || originalUnregister_ == nullptr ||
            component == nullptr)
        {
            return false;
        }

        const ThingContext before = ReadThingContext(component);
        bool invoked = false;
        __try
        {
            originalUnregister_(component);
            invoked = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        const ThingContext after = ReadThingContext(component);
        if (invoked && before.registered && !after.registered)
        {
            Report(
                ThingPresencePhase::Unregistered,
                component,
                before);
            return true;
        }
        return false;
    }

    bool ThingPresenceObserver::IsInstalled() const noexcept
    {
        return active_ == this &&
            originalRegister_ != nullptr &&
            originalUpdate_ != nullptr &&
            originalUnregister_ != nullptr &&
            originalDestructor_ != nullptr &&
            registerDetour_.IsInstalled() && updateDetour_.IsInstalled() &&
            unregisterDetour_.IsInstalled() && destructorDetour_.IsInstalled();
    }

    unsigned int ThingPresenceObserver::RegistrationCount() const noexcept
    {
        return registrationCount_.load(std::memory_order_acquire);
    }

    unsigned int ThingPresenceObserver::UnregistrationCount() const noexcept
    {
        return unregistrationCount_.load(std::memory_order_acquire);
    }

    bool ThingPresenceObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        core::hooking::InlineHook& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::MapwhoFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr || detour.IsInstalled())
        {
            return false;
        }
        return detour.Install(
            target,
            target,
            displacedBytes,
            replacement,
            displacedBytes);
    }

    bool ThingPresenceObserver::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
    }

    void __fastcall ThingPresenceObserver::ObserveRegister(
        void* component,
        void*,
        const void* worldPosition)
    {
        ThingPresenceObserver* const observer = active_;
        if (observer == nullptr || observer->originalRegister_ == nullptr)
        {
            return;
        }

        const ThingContext before = ReadThingContext(component);
        observer->originalRegister_(component, worldPosition);
        const ThingContext after = ReadThingContext(component);
        if (!before.registered && after.registered)
        {
            observer->Report(
                ThingPresencePhase::Registered,
                component,
                after);
        }
    }

    void __fastcall ThingPresenceObserver::ObserveUnregister(
        void* component,
        void*)
    {
        ThingPresenceObserver* const observer = active_;
        if (observer == nullptr || observer->originalUnregister_ == nullptr)
        {
            return;
        }

        observer->RequestUnregister(component);
    }

    void __fastcall ThingPresenceObserver::ObserveUpdate(
        void* component,
        void*,
        const void* worldPosition)
    {
        ThingPresenceObserver* const observer = active_;
        if (observer == nullptr || observer->originalUpdate_ == nullptr)
        {
            return;
        }

        // This path runs for ordinary movement. Avoid UID/component traversal
        // unless the update actually changes an absent component to present.
        const bool wasRegistered = ReadRegistered(component);
        observer->originalUpdate_(component, worldPosition);
        if (!wasRegistered && ReadRegistered(component))
        {
            const ThingContext context = ReadThingContext(component);
            observer->Report(
                ThingPresencePhase::Registered,
                component,
                context);
        }
    }

    void* __fastcall ThingPresenceObserver::ObserveDestructor(
        void* component,
        void*,
        unsigned int flags)
    {
        ThingPresenceObserver* const observer = active_;
        if (observer == nullptr || observer->originalDestructor_ == nullptr)
        {
            return component;
        }

        const ThingContext before = ReadThingContext(component);
        void* const result = observer->originalDestructor_(component, flags);
        if (before.registered)
        {
            observer->Report(
                ThingPresencePhase::Unregistered,
                component,
                before,
                true);
        }
        return result;
    }

    void ThingPresenceObserver::Report(
        ThingPresencePhase phase,
        void* component,
        const ThingContext& context,
        bool destroyed) noexcept
    {
        const unsigned int ordinal = phase == ThingPresencePhase::Registered
            ? registrationCount_.fetch_add(1, std::memory_order_acq_rel) + 1
            : unregistrationCount_.fetch_add(1, std::memory_order_acq_rel) + 1;

        ThingPresenceEvent event;
        event.phase = phase;
        event.thingUid = context.uid;
        event.villageUid = context.villageUid;
        event.mapId = context.mapId;
        event.definitionIndex = context.definitionIndex;
        event.scriptName = context.scriptName;
        event.position = context.position;
        event.facing = context.facing;
        event.hasTransform = context.hasTransform;
        event.gamePersistent = context.gamePersistent;
        event.levelPersistent = context.levelPersistent;
        event.creature = context.creature;
        event.hasHeroMorph = context.hasHeroMorph;
        event.hasVillageMembership = context.hasVillageMembership;
        event.summonedCreature = context.summonedCreature;
        event.abilityOwnedTransient =
            TransientEntityCreationScope::IsActive();
        event.destroyed = destroyed;
        event.thing = context.thing;
        event.component = component;
        Notify(event);

        const bool networkIdentityBearing = context.creature ||
            context.gamePersistent || context.levelPersistent ||
            context.hasVillageMembership || context.scriptName[0] != '\0';
        if (!networkIdentityBearing || ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "phase=%s ordinal=%u thing_uid=%016llX village_uid=%016llX map_id=%u definition_index=%u script_name=%s game_persistent=%s level_persistent=%s creature=%s hero_presentation=%s summoned=%s ability_transient=%s destroyed=%s thing=%p component=%p context_readable=%s thread=%lu",
            phase == ThingPresencePhase::Registered
                ? "registered"
                : "unregistered",
            ordinal,
            static_cast<unsigned long long>(context.uid),
            static_cast<unsigned long long>(context.villageUid),
            static_cast<unsigned int>(context.mapId),
            static_cast<unsigned int>(context.definitionIndex),
            context.scriptName.data(),
            context.gamePersistent ? "true" : "false",
            context.levelPersistent ? "true" : "false",
            context.creature ? "true" : "false",
            context.hasHeroMorph ? "true" : "false",
            context.summonedCreature ? "true" : "false",
            event.abilityOwnedTransient ? "true" : "false",
            destroyed ? "true" : "false",
            context.thing,
            component,
            context.readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("ThingPresenceChanged", detail);
    }

    ThingPresenceObserver::ThingContext
        ThingPresenceObserver::ReadThingContext(void* component) noexcept
    {
        ThingContext context;
        __try
        {
            if (component != nullptr)
            {
                const auto* const componentBytes =
                    static_cast<const std::uint8_t*>(component);
                context.thing = *reinterpret_cast<void* const*>(
                    componentBytes + 0x04);
                context.registered =
                    (componentBytes[0x50] & 0x01u) != 0;
                if (context.thing != nullptr)
                {
                    const auto* const thingBytes =
                        static_cast<const std::uint8_t*>(context.thing);
                    context.uid = *reinterpret_cast<const std::uint64_t*>(
                        thingBytes + 0x14);
                    context.mapId = *reinterpret_cast<const std::uint16_t*>(
                        thingBytes + 0x9A);
                    context.definitionIndex =
                        *reinterpret_cast<const std::uint16_t*>(
                            thingBytes + 0x98);
                    void* const scriptString =
                        *reinterpret_cast<void* const*>(thingBytes + 0x80);
                    if (scriptString != nullptr)
                    {
                        const char* const text =
                            *reinterpret_cast<const char* const*>(
                                static_cast<const std::uint8_t*>(scriptString) +
                                sizeof(void*));
                        if (text != nullptr)
                        {
                            std::size_t index = 0;
                            while (index + 1 < context.scriptName.size() &&
                                text[index] != '\0')
                            {
                                context.scriptName[index] = text[index];
                                ++index;
                            }
                            context.scriptName[index] = '\0';
                        }
                    }
                    const std::uint8_t persistenceFlags = thingBytes[0x9E];
                    context.gamePersistent =
                        (persistenceFlags & 0x20u) != 0;
                    context.levelPersistent =
                        (persistenceFlags & 0x10u) != 0;
                    context.creature =
                        game::entity::native::ThingComponentAccess::Has(
                            context.thing,
                            game::entity::native::ThingComponentType::
                                CreatureNavigation);
                    context.summonedCreature =
                        game::entity::native::ThingComponentAccess::
                            IsActiveSummonedCreature(context.thing);
                    void* const navigator =
                        game::entity::native::ThingComponentAccess::Find(
                            context.thing,
                            game::entity::native::ThingComponentType::
                                PhysicsNavigator);
                    if (navigator != nullptr)
                    {
                        std::memcpy(
                            &context.position,
                            static_cast<const std::uint8_t*>(navigator) +
                                game::creature::locomotion::native::
                                    PhysicsNavigatorFunctions::
                                        WorldPositionOffset,
                            sizeof(context.position));
                        ThingPresenceObserver* const observer = active_;
                        context.hasTransform = observer != nullptr &&
                            std::isfinite(context.position.x) &&
                            std::isfinite(context.position.y) &&
                            std::isfinite(context.position.z) &&
                            game::creature::look::native::
                                CreatureLookFunctions::ReadNavigatorFacing(
                                    observer->gameModule_,
                                    navigator,
                                    context.facing);
                    }
                    context.hasHeroMorph =
                        game::entity::native::ThingComponentAccess::Has(
                            context.thing,
                            game::entity::native::ThingComponentType::
                                HeroMorph);
                    void* const villageMember =
                        game::entity::native::ThingComponentAccess::Find(
                            context.thing,
                            game::entity::native::ThingComponentType::
                                VillageMember);
                    if (villageMember != nullptr)
                    {
                        context.villageUid =
                            *reinterpret_cast<const std::uint64_t*>(
                                static_cast<const std::uint8_t*>(
                                    villageMember) + 0x18);
                        context.hasVillageMembership =
                            context.villageUid != 0;
                    }
                    context.readable = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            context = {};
        }
        return context;
    }

    bool ThingPresenceObserver::ReadRegistered(void* component) noexcept
    {
        bool registered = false;
        __try
        {
            if (component != nullptr)
            {
                const auto* const bytes =
                    static_cast<const std::uint8_t*>(component);
                registered = (bytes[0x50] & 0x01u) != 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            registered = false;
        }
        return registered;
    }

    void ThingPresenceObserver::Notify(
        const ThingPresenceEvent& event) noexcept
    {
        const EventSink sink = eventSink_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        void* const context =
            eventSinkContext_.load(std::memory_order_acquire);
        sink(context, event);
    }
}
