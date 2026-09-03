#include "Multiplayer/Authority/ActionAuthorityCoordinator.h"

#include <cstdio>

namespace
{
    using namespace fable::multiplayer;

    protocol::EntityActionMessage Intent(
        protocol::EntityActionKind kind, std::uint64_t player)
    {
        protocol::EntityActionMessage message;
        message.phase = protocol::EntityActionPhase::Intent;
        message.kind = kind;
        message.entityUid = 42;
        message.entityGeneration = 3;
        message.actionId = player + 100;
        message.ownerActorId = player;
        message.mapEpoch = 7;
        message.mapName = "BowerstoneSlums_v2";
        message.semanticName = "test-interaction";
        return message;
    }
}

int RunLocalShopAuthorityTests()
{
    int failures = 0;
    const auto expect = [&failures](bool value, const char* description)
    {
        if (!value)
        {
            std::fprintf(stderr, "Local shop authority: %s\n", description);
            ++failures;
        }
    };

    authority::ActionAuthorityCoordinator coordinator;
    coordinator.Initialize(PeerRole::Host, 11, {});
    authority::MapAuthorityLease map;
    map.mapName = "BowerstoneSlums_v2";
    map.mapId = 339;
    map.actorId = 11;
    map.epoch = 7;
    authority::ActionAuthorityLease granted;
    protocol::AuthorityMessage pending;
    const authority::EntityAuthorityKey merchant{42, 3};

    // Two buyers in either order must not create an exclusive merchant lease.
    for (const std::uint64_t player : {11ull, 22ull, 22ull, 11ull})
    {
        const auto shopping = Intent(protocol::EntityActionKind::Trade, player);
        expect(!protocol::RequiresSharedEntityAuthority(shopping.kind),
            "shopping must be local at capture and receive boundaries");
        expect(!coordinator.HostAcquire(shopping, player, map, granted),
            "a private transaction must not acquire shared ownership");
        expect(coordinator.Find(merchant) == nullptr,
            "shopping left a stale merchant lease");
        expect(!coordinator.TakePending(pending),
            "shopping generated a network grant/release");
    }

    const auto quest = Intent(protocol::EntityActionKind::QuestOrCutscene, 11);
    expect(protocol::RequiresSharedEntityAuthority(quest.kind),
        "quest consequences must remain shared");
    expect(coordinator.HostAcquire(quest, 11, map, granted),
        "a real quest must still acquire its fenced lease");
    const std::uint32_t questEpoch = granted.actionEpoch;
    expect(coordinator.TakePending(pending), "missing quest grant");

    expect(!coordinator.HostAcquire(
            Intent(protocol::EntityActionKind::Trade, 22), 22, map, granted),
        "shopping preempted the quest lease");
    const auto* lease = coordinator.Find(merchant);
    expect(lease != nullptr && lease->actionEpoch == questEpoch &&
            lease->actorId == 11,
        "private shopping changed shared quest ownership");
    expect(!coordinator.TakePending(pending),
        "private shopping emitted a quest lease mutation");

    expect(protocol::RequiresSharedEntityAuthority(protocol::EntityActionKind::Combat),
        "combat must remain shared");
    expect(protocol::RequiresSharedEntityAuthority(protocol::EntityActionKind::Movement),
        "movement must remain shared");
    expect(protocol::RequiresSharedEntityAuthority(protocol::EntityActionKind::Conversation),
        "unclassified dialogue must not silently bypass quest authority");
    return failures;
}
