#pragma once

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/World/Travel/Native/RegionExitFunctions.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fable::automation::multiplayer::transition::native_route
{
    struct Descriptor final
    {
        game::world::travel::native::RegionExitDescriptor exit;
        game::Vector3 destinationPosition = {};
        float destinationFacing = 0.0f;
    };

    // Select the first connected retail region exit on the specified native
    // Hero map. The ordering is stable across peers, which lets the NPC
    // transfer and player transition acceptance share one boundary without
    // inventing a destination or writing an arbitrary world position.
    inline bool SelectFirst(
        game::EntityService& entities,
        ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
        const std::uint16_t sourceMapId,
        Descriptor& selected,
        const std::uint16_t preferredDestinationMapId = 0)
    {
        selected = {};
        if (sourceMapId == 0)
        {
            return false;
        }

        std::vector<Descriptor> routes;
        const auto records = multiplayer.Contexts().entities.entityPresence.
            LiveEntities().Snapshot();
        routes.reserve(records.size());
        for (const auto& record : records)
        {
            if (record.mapId != sourceMapId || record.thing == nullptr)
            {
                continue;
            }
            Descriptor route;
            if (!game::world::travel::native::RegionExitFunctions::Describe(
                    record.thing,
                    record.thingUid,
                    entities.GameModule(),
                    route.exit) ||
                route.exit.destinationMapId == sourceMapId ||
                (preferredDestinationMapId != 0 &&
                    route.exit.destinationMapId !=
                        preferredDestinationMapId))
            {
                continue;
            }

            game::Entity* const destination = entities.FindByUid(
                route.exit.destinationEntranceUid);
            if (destination == nullptr || !destination->IsValid())
            {
                if (destination != nullptr)
                {
                    destination->Release();
                }
                continue;
            }
            route.destinationPosition = destination->GetPosition();
            route.destinationFacing = destination->GetFacing();
            destination->Release();
            if (!std::isfinite(route.destinationPosition.x) ||
                !std::isfinite(route.destinationPosition.y) ||
                !std::isfinite(route.destinationPosition.z) ||
                !std::isfinite(route.destinationFacing))
            {
                continue;
            }
            routes.push_back(route);
        }

        if (routes.empty())
        {
            return false;
        }
        std::sort(
            routes.begin(),
            routes.end(),
            [](const Descriptor& left, const Descriptor& right)
            {
                if (left.exit.destinationMapId !=
                    right.exit.destinationMapId)
                {
                    return left.exit.destinationMapId <
                        right.exit.destinationMapId;
                }
                if (left.exit.destinationEntranceUid !=
                    right.exit.destinationEntranceUid)
                {
                    return left.exit.destinationEntranceUid <
                        right.exit.destinationEntranceUid;
                }
                return left.exit.exitUid < right.exit.exitUid;
            });
        selected = routes.front();
        return true;
    }
}
