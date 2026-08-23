#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

class asIScriptEngine;

#pragma section(".ftbind$a", read)
#pragma section(".ftbind$m", read)
#pragma section(".ftbind$z", read)

namespace fable::game { class GameServiceRuntime; }
namespace fable::core { class CapabilityRegistry; }
namespace fable::scripting { class EventBus; class PersistentStore; class Scheduler; }

namespace fable::scripting::bindings
{
    struct BindingContext final
    {
        asIScriptEngine& Engine;
        game::GameServiceRuntime& Services;
        core::CapabilityRegistry& Capabilities;
        const core::Diagnostics& Diagnostics;
        scripting::Scheduler* Scheduler = nullptr;
        scripting::EventBus* Events = nullptr;
        scripting::PersistentStore* Storage = nullptr;
    };

    using BindingGroupFunction = bool (*)(BindingContext& context);

    struct BindingGroupDescriptor final
    {
        const char* Name;
        std::uint32_t Order;
        BindingGroupFunction Function;
    };

    bool RegisterDiscoveredBindings(
        asIScriptEngine& engine,
        game::GameServiceRuntime& services,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics,
        scripting::Scheduler* scheduler = nullptr,
        scripting::EventBus* events = nullptr,
        scripting::PersistentStore* storage = nullptr);
}

extern "C"
{
    __declspec(allocate(".ftbind$a")) __declspec(selectany)
        fable::scripting::bindings::BindingGroupDescriptor
            fableScriptBindingBegin = {nullptr, 0, nullptr};
    __declspec(allocate(".ftbind$z")) __declspec(selectany)
        fable::scripting::bindings::BindingGroupDescriptor
            fableScriptBindingEnd = {nullptr, 0, nullptr};
}

#define FABLE_SCRIPT_BINDING_JOIN_IMPL(left, right) left##right
#define FABLE_SCRIPT_BINDING_JOIN(left, right) FABLE_SCRIPT_BINDING_JOIN_IMPL(left, right)
#define FABLE_SCRIPT_BINDING_STRINGIFY_IMPL(value) #value
#define FABLE_SCRIPT_BINDING_STRINGIFY(value) FABLE_SCRIPT_BINDING_STRINGIFY_IMPL(value)
#define FABLE_SCRIPT_BINDING_FORCE_INCLUDE(id) \
    "/include:_fableScriptBinding_" FABLE_SCRIPT_BINDING_STRINGIFY(id)
#define FABLE_SCRIPT_BINDING_GROUP(id, order, function) \
    __pragma(comment(linker, FABLE_SCRIPT_BINDING_FORCE_INCLUDE(id))) \
    extern "C" __declspec(allocate(".ftbind$m")) __declspec(dllexport) \
        const fable::scripting::bindings::BindingGroupDescriptor \
            FABLE_SCRIPT_BINDING_JOIN(fableScriptBinding_, id) = {#id, order, function}
