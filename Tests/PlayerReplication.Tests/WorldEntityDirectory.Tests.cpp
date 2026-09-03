#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/World/MapIdentityRegistry.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    int mapIdentityConflictEvents = 0;

    void CaptureMapIdentityEvent(const char* state, const char*)
    {
        if (state != nullptr && std::strcmp(
                state, "MultiplayerMapIdentityConflict") == 0)
        {
            ++mapIdentityConflictEvents;
        }
    }

    bool Check(
        const char* test,
        const bool condition)
    {
        if (condition)
        {
            return true;
        }
        std::cerr << "FAIL: " << test << "\n";
        return false;
    }
}

int RunWorldEntityDirectoryTests()
{
    constexpr const char* test = "canonical world mutation revision and low-sim merge";
    fable::multiplayer::entities::WorldEntityDirectory directory;
    fable::multiplayer::protocol::EntityLifecycleMessage authoritative;
    authoritative.operation = fable::multiplayer::protocol::
        EntityLifecycleOperation::AuthoritativeUpsert;
    authoritative.flags = fable::multiplayer::protocol::entity_lifecycle_flag::
        GamePersistent |
        fable::multiplayer::protocol::entity_lifecycle_flag::Creature |
        fable::multiplayer::protocol::entity_lifecycle_flag::Live |
        fable::multiplayer::protocol::entity_lifecycle_flag::Available |
        fable::multiplayer::protocol::entity_lifecycle_flag::HasTransform;
    authoritative.entityUid = 7001;
    authoritative.entityGeneration = 3;
    authoritative.worldRevision = 1;
    authoritative.simulationOwnerActorId = 42;
    authoritative.mapEpoch = 9;
    authoritative.mapId = 7;
    authoritative.position = {1.0f, 2.0f, 3.0f};
    authoritative.facing = 0.25f;
    authoritative.mapName = "Greatwood";
    if (!Check(test, directory.ApplyAuthoritative(authoritative)))
    {
        return 1;
    }

    const std::uint64_t beforeMovement = directory.LatestWorldRevision();
    fable::multiplayer::protocol::EntityMovementMessage movement;
    movement.entityUid = 7001;
    movement.entityGeneration = authoritative.entityGeneration;
    movement.ownerActorId = 42;
    movement.mapEpoch = 9;
    movement.mapName = "Greatwood";
    movement.position = {8.0f, 9.0f, 10.0f};
    movement.facing = 0.75f;
    bool changed = false;
    if (!Check(test, directory.HostAcceptMovement(movement)) ||
        !Check(test, directory.LatestWorldRevision() > beforeMovement))
    {
        return 1;
    }

    fable::game::npc::simulation::DummyVillagerState lowSimulation;
    lowSimulation.recreationDay = 12;
    lowSimulation.recreationFrame = 34;
    lowSimulation.creatureUid = 99001;
    lowSimulation.respawnable = true;
    lowSimulation.guard = true;
    lowSimulation.componentPresent = true;
    if (!Check(test, directory.HostApplyLowSimulation(
            7001,
            authoritative.entityGeneration,
            "Greatwood",
            42,
            9,
            lowSimulation,
            5,
            changed)) || !Check(test, changed))
    {
        return 1;
    }
    const auto* record = directory.Find(7001);

    // A delayed structural snapshot must not regress the newer canonical
    // low-sim row already retained by the host.
    fable::multiplayer::protocol::EntityLifecycleMessage staleSnapshot =
        authoritative;
    staleSnapshot.worldRevision = directory.LatestWorldRevision() + 1;
    staleSnapshot.lowSimulationRevision = 4;
    staleSnapshot.lowSimulation.recreationDay = 1;
    staleSnapshot.lowSimulation.recreationFrame = 2;
    staleSnapshot.lowSimulation.componentPresent = true;
    staleSnapshot.lowSimulationFlags = 0x04u;
    if (!Check(test, directory.ApplyAuthoritative(staleSnapshot)))
    {
        return 1;
    }
    record = directory.Find(7001);
    if (!Check(test, record != nullptr) ||
        !Check(test, record->lowSimulationRevision == 5) ||
        !Check(test, record->lowSimulation.recreationDay == 12))
    {
        return 1;
    }

    record = directory.Find(7001);
    if (!Check(test, record != nullptr) ||
        !Check(test, record->hasLowSimulation) ||
        !Check(test, record->lowSimulationRevision == 5) ||
        !Check(test, record->lowSimulation.recreationDay == 12) ||
        !Check(test, record->lowSimulation.recreationFrame == 34) ||
        !Check(test, record->lowSimulation.respawnable) ||
        !Check(test, record->lowSimulation.guard) ||
        !Check(test, record->lowSimulation.creatureUid == 99001))
    {
        return 1;
    }

    auto conflictingLowSimulation = lowSimulation;
    conflictingLowSimulation.recreationFrame = 99;
    if (!Check(test, !directory.HostApplyLowSimulation(
            7001,
            authoritative.entityGeneration,
            "Greatwood",
            42,
            9,
            conflictingLowSimulation,
            5,
            changed)) ||
        !Check(test, !directory.HostApplyLowSimulation(
            7001,
            authoritative.entityGeneration,
            "Greatwood",
            42,
            9,
            conflictingLowSimulation,
            4,
            changed)) ||
        !Check(test, directory.Find(7001)->lowSimulationRevision == 5) ||
        !Check(test,
            directory.Find(7001)->lowSimulation.recreationFrame == 34))
    {
        return 1;
    }

    // Structural lifecycle snapshots do not carry the typed low-sim row.
    // Applying one must retain the row for both ordinary updates and a
    // host-issued cross-map handoff.
    fable::multiplayer::protocol::EntityLifecycleMessage update =
        fable::multiplayer::entities::WorldEntityDirectory::ToMessage(
            *record,
            fable::multiplayer::protocol::EntityLifecycleOperation::
                AuthoritativeUpsert);
    update.worldRevision = 10;
    update.mapId = 8;
    update.mapName = "Oakshire";
    if (!Check(test, directory.ApplyAuthoritative(update)))
    {
        return 1;
    }
    record = directory.Find(7001);
    if (!Check(test, record != nullptr) ||
        !Check(test, record->mapName == "Oakshire") ||
        !Check(test, record->hasLowSimulation) ||
        !Check(test, record->lowSimulation.creatureUid == 99001) ||
        !Check(test, record->lowSimulation.recreationDay == 12))
    {
        return 1;
    }

    fable::multiplayer::protocol::EntityLifecycleMessage transfer =
        fable::multiplayer::entities::WorldEntityDirectory::ToMessage(
            *record,
            fable::multiplayer::protocol::EntityLifecycleOperation::
                ObserveTransfer);
    transfer.sourceMapName = record->mapName;
    transfer.sourceMapEpoch = record->mapEpoch;
    transfer.worldRevision = 0;
    transfer.simulationOwnerActorId = 0;
    transfer.mapEpoch = 0;
    transfer.mapId = 9;
    transfer.position = {18.0f, 19.0f, 20.0f};
    transfer.facing = 0.5f;
    transfer.lowSimulationRevision = 6;
    transfer.lowSimulation.recreationDay = 13;
    transfer.lowSimulation.recreationFrame = 35;
    transfer.lowSimulation.respawnable = false;
    transfer.lowSimulation.guard = false;
    transfer.lowSimulation.componentPresent = true;
    transfer.lowSimulationFlags = 0x04u;
    transfer.flags |= fable::multiplayer::protocol::entity_lifecycle_flag::
        HasTransform;
    transfer.flags &= static_cast<std::uint8_t>(
        ~fable::multiplayer::protocol::entity_lifecycle_flag::Live);
    fable::multiplayer::protocol::EntityLifecycleMessage authoritativeTransfer;
    if (!Check(test, directory.HostTransfer(
            transfer,
            42,
            "Bowerstone",
            84,
            10,
            authoritativeTransfer,
            changed)))
    {
        return 1;
    }
    record = directory.Find(7001);
    if (!Check(test, record != nullptr) ||
        !Check(test, record->mapName == "Bowerstone") ||
        !Check(test, record->mapId == 9) ||
        !Check(test, record->lowSimulation.creatureUid == 99001) ||
        !Check(test, record->lowSimulation.recreationFrame == 35) ||
        !Check(test, record->lowSimulationRevision == 6))
    {
        return 1;
    }

    // The transfer carries the final typed row on the ordered lifecycle
    // stream, so exercise the actual wire round-trip as well as the merge.
    std::array<std::uint8_t, fable::multiplayer::protocol::
        MaximumDatagramBytes> encoded = {};
    std::size_t encodedSize = 0;
    if (!Check(test, fable::multiplayer::protocol::
            EncodeEntityLifecycleMessage(
                transfer,
                encoded.data(),
                encoded.size(),
                encodedSize)))
    {
        return 1;
    }
    fable::multiplayer::protocol::EntityLifecycleMessage decoded;
    if (!Check(test, fable::multiplayer::protocol::
            DecodeEntityLifecycleMessage(
                encoded.data(), encodedSize, decoded)) ||
        !Check(test, decoded.lowSimulationRevision == 6) ||
        !Check(test, decoded.lowSimulation.recreationDay == 13) ||
        !Check(test, decoded.lowSimulation.recreationFrame == 35) ||
        !Check(test, !decoded.lowSimulation.guard) ||
        !Check(test, decoded.lowSimulation.componentPresent))
    {
        return 1;
    }
    auto staleTransfer = transfer;
    staleTransfer.sourceMapName = "Bowerstone";
    staleTransfer.sourceMapEpoch = 10;
    staleTransfer.mapId = 10;
    staleTransfer.lowSimulationRevision = 4;
    staleTransfer.lowSimulation.recreationDay = 2;
    staleTransfer.lowSimulation.recreationFrame = 3;
    if (!Check(test, !directory.HostTransfer(
            staleTransfer,
            84,
            "LookoutPoint",
            91,
            11,
            authoritativeTransfer,
            changed)) ||
        !Check(test, directory.Find(7001)->lowSimulationRevision == 6))
    {
        return 1;
    }

    // Numeric map identity owns roster state. A corrected canonical label for
    // the same retail map replaces the diagnostic name without creating a
    // second roster, while a delayed lower-revision label cannot regress it.
    fable::multiplayer::protocol::EntityLifecycleMessage roster;
    if (!Check(test, directory.HostCompleteMapRoster(
            "GuildWoods",
            83,
            42,
            12,
            roster,
            changed)) || !Check(test, changed) ||
        !Check(test, roster.mapId == 83))
    {
        return 1;
    }
    const auto staleRoster = roster;
    if (!Check(test, directory.HostCompleteMapRoster(
            "FrescoCaves",
            83,
            42,
            12,
            roster,
            changed)) || !Check(test, changed) ||
        !Check(test, directory.CompletedMapRosters().size() == 1) ||
        !Check(test,
            directory.CompletedMapRosters().front().mapName ==
                "FrescoCaves") ||
        !Check(test, directory.HasMapRoster(83)) ||
        !Check(test, directory.IsMapRosterComplete(83, 12)))
    {
        return 1;
    }
    fable::multiplayer::entities::WorldEntityDirectory replica;
    if (!Check(test, replica.ApplyAuthoritative(roster)) ||
        !Check(test, !replica.ApplyAuthoritative(staleRoster)) ||
        !Check(test,
            replica.CompletedMapRosters().front().mapName ==
                "FrescoCaves"))
    {
        return 1;
    }
    encodedSize = 0;
    if (!Check(test, fable::multiplayer::protocol::
            EncodeEntityLifecycleMessage(
                roster,
                encoded.data(),
                encoded.size(),
                encodedSize)) ||
        !Check(test, fable::multiplayer::protocol::
            DecodeEntityLifecycleMessage(
                encoded.data(), encodedSize, decoded)) ||
        !Check(test, decoded.mapId == 83) ||
        !Check(test, decoded.mapName == "FrescoCaves"))
    {
        return 1;
    }

    fable::multiplayer::world::MapIdentityRegistry maps;
    mapIdentityConflictEvents = 0;
    maps.Initialize(
        fable::multiplayer::PeerRole::Host,
        42,
        {nullptr, &CaptureMapIdentityEvent});
    fable::multiplayer::PlayerState initialMap;
    initialMap.actorId = 42;
    initialMap.role = fable::multiplayer::PeerRole::Host;
    initialMap.mapId = 70;
    initialMap.mapName = "HeroGuildComplex";
    maps.Reconcile(&initialMap, {});
    if (!Check(test, maps.ObserveAuthoritative("GuildWoods", 84)))
    {
        return 1;
    }
    fable::multiplayer::PlayerState staleScriptMap = initialMap;
    staleScriptMap.mapId = 84;
    maps.Reconcile(&staleScriptMap, {});
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        maps.Reconcile(&staleScriptMap, {});
    }
    const std::string* const guild = maps.FindName(70);
    const std::string* const woods = maps.FindName(84);
    if (!Check(test, guild != nullptr && *guild == "HeroGuildComplex") ||
        !Check(test, woods != nullptr && *woods == "GuildWoods") ||
        !Check(test, maps.FindId("HeroGuildComplex") == 70) ||
        !Check(test, maps.FindId("GuildWoods") == 84) ||
        !Check(test, mapIdentityConflictEvents == 1))
    {
        return 1;
    }
    return 0;
}
