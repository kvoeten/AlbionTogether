#include "RegionExitFunctions.h"

#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/World/Travel/Native/WorldTravelFunctions.h"

namespace
{
    constexpr std::size_t SourceThingOffset = 0x04;
    constexpr std::size_t ConnectedThingReferenceOffset = 0x26;
    constexpr std::size_t ThingUidOffset = 0x14;
    constexpr std::size_t ThingMapIdOffset = 0x9A;
}

namespace fable::game::world::travel::native
{
    bool RegionExitFunctions::Describe(
        void* nativeThing,
        const std::uint64_t exitUid,
        HMODULE gameModule,
        RegionExitDescriptor& descriptor) noexcept
    {
        descriptor = {};
        if (nativeThing == nullptr || exitUid == 0 || gameModule == nullptr)
        {
            return false;
        }

        void* const component =
            ::fable::game::entity::native::ThingComponentAccess::Find(
            nativeThing,
            ::fable::game::entity::native::ThingComponentType::RegionExit);
        WorldTravelFunctions::ResolveConnectedThingPointer resolveConnected =
            nullptr;
        if (component == nullptr ||
            !WorldTravelFunctions::ResolveConnectedThing(
                gameModule, resolveConnected))
        {
            return false;
        }

        bool valid = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(component);
            void* const sourceThing = *reinterpret_cast<void**>(
                bytes + SourceThingOffset);
            void* const destinationThing = resolveConnected(
                bytes + ConnectedThingReferenceOffset);
            if (sourceThing == nativeThing && destinationThing != nullptr)
            {
                descriptor.exitUid = exitUid;
                descriptor.destinationEntranceUid =
                    *reinterpret_cast<const std::uint64_t*>(
                        static_cast<const std::uint8_t*>(destinationThing) +
                        ThingUidOffset);
                descriptor.sourceMapId =
                    *reinterpret_cast<const std::uint16_t*>(
                        static_cast<const std::uint8_t*>(sourceThing) +
                        ThingMapIdOffset);
                descriptor.destinationMapId =
                    *reinterpret_cast<const std::uint16_t*>(
                        static_cast<const std::uint8_t*>(destinationThing) +
                        ThingMapIdOffset);
                valid = descriptor.destinationEntranceUid != 0 &&
                    descriptor.sourceMapId != 0 &&
                    descriptor.destinationMapId != 0 &&
                    descriptor.sourceMapId != descriptor.destinationMapId;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            descriptor = {};
        }
        return valid;
    }

}
