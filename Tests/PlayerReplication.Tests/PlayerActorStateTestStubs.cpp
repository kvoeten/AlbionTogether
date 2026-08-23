#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/HeroPawn/Abilities/Hooks/PillarAbilityLifecycleHook.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"

namespace
{
    const fable::multiplayer::PlayerState* g_currentState = nullptr;
}

namespace fable::multiplayer::replication::testing
{
    void SetLocalHeroState(const PlayerState* state) noexcept
    {
        g_currentState = state;
    }
}

namespace fable::multiplayer::replication
{
    const PlayerState* LocalHeroReplication::CurrentState() const noexcept
    {
        // This standalone test binary does not link the native Hero reader.
        // Its service tests drive the real replication process loop through
        // a deterministic current-state seam owned entirely by the test.
        return g_currentState;
    }

    void* LocalHeroReplication::NativeHero() const noexcept
    {
        return nullptr;
    }

    bool PlayerActionReplication::ReplayPending()
    {
        return true;
    }
}

namespace fable::game::creature::actions
{
    bool CreatureActionLifecycleObserver::IsInstalled() const noexcept
    {
        return true;
    }

    bool CreatureActionLifecycleObserver::AddEventSink(
        EventSink,
        void*) noexcept
    {
        return true;
    }

    void CreatureActionLifecycleObserver::RemoveEventSink(
        EventSink,
        void*) noexcept
    {
    }
}

namespace fable::game::creature::combat
{
    CreatureCombatService::~CreatureCombatService() = default;

    bool CreatureCombatService::AddAbilitySink(
        AbilitySink,
        void*) noexcept
    {
        return true;
    }

    void CreatureCombatService::RemoveAbilitySink(
        AbilitySink,
        void*) noexcept
    {
    }

    void CreatureCombatService::SetHealthMutationSink(
        HealthMutationSink,
        void*) noexcept
    {
    }

    bool CreatureCombatService::ReadCombatHealth(
        void*,
        float&,
        float&) const noexcept
    {
        return false;
    }

    bool CreatureCombatService::ApplyAuthoritativeCombatHealth(
        void*,
        float,
        float) noexcept
    {
        return false;
    }
}

namespace fable::game::hero_pawn::abilities
{
    bool HeroWillAbilityService::AddEventSink(EventSink, void*) noexcept
    {
        return true;
    }

    void HeroWillAbilityService::RemoveEventSink(EventSink, void*) noexcept
    {
    }
}

namespace fable::multiplayer::authority
{
    bool AuthorityReplication::HandleReliableMessage(
        const TransportMessage&)
    {
        return true;
    }

    bool AuthorityReplication::IsEntityPublisher(
        const EntityAuthorityKey&,
        const std::string&,
        std::uint64_t,
        std::uint32_t) const noexcept
    {
        return false;
    }
}

namespace fable::multiplayer::entities
{
    bool EntityLifecycleReplication::HandleReliableMessage(
        const TransportMessage&)
    {
        return true;
    }

    const WorldEntityDirectory& EntityLifecycleReplication::Directory()
        const noexcept
    {
        static const WorldEntityDirectory directory;
        return directory;
    }

    const WorldEntityRecord* WorldEntityDirectory::Find(
        const std::uint64_t) const noexcept
    {
        return nullptr;
    }

    std::uint64_t EntityNetworkIdentityRegistry::Canonicalize(
        const std::uint64_t uid) const noexcept
    {
        return uid;
    }

    std::uint64_t EntityNetworkIdentityRegistry::
        CanonicalizeLocalObservation(const std::uint64_t uid) const noexcept
    {
        return uid;
    }

    const LiveEntityRecord* LiveEntityRegistry::Find(
        const std::uint64_t) const noexcept
    {
        return nullptr;
    }

    std::vector<LiveEntityRecord> LiveEntityRegistry::Snapshot() const
    {
        return {};
    }

    bool LiveEntityRegistry::IsReplicable(
        const LiveEntityRecord&) noexcept
    {
        return false;
    }
}

namespace fable::game::hero_pawn::abilities::hooks
{
    PillarAbilityLifecycleHook::~PillarAbilityLifecycleHook() = default;
}

namespace fable::game::hero_pawn::equipment::native
{
    bool HeroWeaponComponent::Capture(
        void*,
        HeroEquipmentState&) noexcept
    {
        return false;
    }
}

namespace fable::multiplayer::combat
{
    std::uint64_t PlayerCombatantDirectory::FindActor(void*) const noexcept
    {
        return 0;
    }
}

namespace fable::multiplayer::presentation
{
    bool RemotePlayerRegistry::ApplyHealth(
        std::uint64_t,
        float,
        float,
        std::uint32_t)
    {
        return true;
    }
}
