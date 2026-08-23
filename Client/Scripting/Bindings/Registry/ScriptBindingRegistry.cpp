#include "ScriptBindingRegistry.h"

#include "Game/Runtime/GameServiceRuntime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fable::scripting::bindings
{
    namespace
    {
        constexpr std::size_t kExpectedBindingGroupCount = 17;
    }

    bool RegisterDiscoveredBindings(
        asIScriptEngine& engine,
        game::GameServiceRuntime& services,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics,
        scripting::Scheduler* scheduler,
        scripting::EventBus* events,
        scripting::PersistentStore* storage)
    {
        const auto* begin = &fableScriptBindingBegin + 1;
        const auto* end = &fableScriptBindingEnd;
        std::vector<const BindingGroupDescriptor*> groups;
        for (const auto* descriptor = begin; descriptor < end; ++descriptor)
        {
            if (descriptor->Name == nullptr || descriptor->Function == nullptr)
            {
                continue;
            }
            groups.push_back(descriptor);
        }
        std::sort(
            groups.begin(),
            groups.end(),
            [](const auto* left, const auto* right)
            {
                if (left->Order != right->Order)
                {
                    return left->Order < right->Order;
                }
                return std::strcmp(left->Name, right->Name) < 0;
            });

        char discoveryDetail[128] = {};
        std::snprintf(
            discoveryDetail,
            sizeof(discoveryDetail),
            "groups=%zu expected=%zu",
            groups.size(),
            kExpectedBindingGroupCount);
        diagnostics.Event("ScriptBindingGroupsDiscovered", discoveryDetail);
        if (groups.size() != kExpectedBindingGroupCount)
        {
            diagnostics.Event("ScriptBindingGroupCountMismatch", discoveryDetail);
            return false;
        }

        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (std::strcmp(groups[previous]->Name, groups[index]->Name) == 0)
                {
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "duplicate AngelScript binding group: %s",
                        groups[index]->Name);
                    diagnostics.Event("ScriptBindingGroupDuplicate", detail);
                    return false;
                }
                if (groups[previous]->Order == groups[index]->Order)
                {
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "duplicate AngelScript binding order %u: %s and %s",
                        groups[index]->Order,
                        groups[previous]->Name,
                        groups[index]->Name);
                    diagnostics.Event("ScriptBindingGroupOrderMismatch", detail);
                    return false;
                }
            }
        }

        BindingContext context{
            engine,
            services,
            capabilities,
            diagnostics,
            scheduler,
            events,
            storage};
        for (const auto* group : groups)
        {
            if (!group->Function(context))
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "AngelScript binding group failed: %s",
                    group->Name);
                diagnostics.Event("ScriptBindingGroupFailed", detail);
                return false;
            }
        }
        return true;
    }
}
