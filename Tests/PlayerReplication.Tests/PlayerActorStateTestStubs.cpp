#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/HeroPawn/Abilities/Hooks/PillarAbilityLifecycleHook.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/PlayerActionReplication.h"

namespace
{
    const fable::multiplayer::PlayerState* g_currentState = nullptr;
    bool g_actionWorldReady = false;
    bool g_remoteActionLifecycleReady = false;
    std::string g_actionMapName;
    std::uint32_t g_performedAbilityCount = 0;
    std::uint32_t g_lastPerformedAnimationId = 0;
}

namespace fable::multiplayer::replication::testing
{
    void SetLocalHeroState(const PlayerState* state) noexcept
    {
        g_currentState = state;
    }
}

extern "C" void ConfigureRemoteActionPresentationForTest(
    const bool worldReady,
    const char* const mapName,
    const bool lifecycleReady) noexcept
{
    g_actionWorldReady = worldReady;
    g_actionMapName = mapName != nullptr ? mapName : "";
    g_remoteActionLifecycleReady = lifecycleReady;
    g_performedAbilityCount = 0;
    g_lastPerformedAnimationId = 0;
}

extern "C" std::uint32_t PerformedAbilityCountForTest() noexcept
{
    return g_performedAbilityCount;
}

extern "C" std::uint32_t LastPerformedAnimationIdForTest() noexcept
{
    return g_lastPerformedAnimationId;
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

    bool LocalHeroReplication::IsWorldReady() const noexcept
    {
        return g_actionWorldReady;
    }

    const std::string& LocalHeroReplication::MapName() const noexcept
    {
        return g_actionMapName;
    }

    std::uint64_t LocalHeroReplication::LastEquipmentMutationAt() const
        noexcept
    {
        return 0;
    }

    void LocalHeroReplication::MarkEquipmentTransition(
        const std::uint64_t,
        const protocol::SessionTimeMs,
        const std::uint32_t,
        const std::uint16_t,
        const std::uint16_t) noexcept
    {
    }

    LocalEquipmentTransition LocalHeroReplication::EquipmentTransition()
        noexcept
    {
        return {};
    }

}

namespace fable::multiplayer::presentation
{
    bool RemotePlayerRegistry::IsLifecycleActive(
        const std::uint64_t,
        const std::uint32_t,
        const std::uint32_t) const noexcept
    {
        return g_remoteActionLifecycleReady;
    }

    bool RemotePlayerRegistry::PerformAbility(
        const std::uint64_t,
        const game::creature::equipment::CreatureWeaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t,
        const float,
        void*,
        const std::string&,
        const std::uint32_t resolvedAnimationId)
    {
        ++g_performedAbilityCount;
        g_lastPerformedAnimationId = resolvedAnimationId;
        return g_remoteActionLifecycleReady;
    }

    bool RemotePlayerRegistry::EndRangedAim(const std::uint64_t) noexcept
    {
        return false;
    }

    bool RemotePlayerRegistry::PerformHeroAbility(
        const std::uint64_t,
        const game::hero_pawn::abilities::HeroAbility,
        const game::hero_pawn::abilities::HeroAbilityCommand,
        const std::int32_t,
        void*)
    {
        return false;
    }

    bool RemotePlayerRegistry::PerformExpression(
        const std::uint64_t,
        const std::string&,
        void*,
        const std::string&,
        const std::uint32_t,
        const std::int32_t,
        const std::int32_t)
    {
        return false;
    }
}

namespace fable::multiplayer::entities
{
    const LiveEntityRegistry& EntityPresenceReplication::LiveEntities() const
        noexcept
    {
        static const LiveEntityRegistry empty;
        return empty;
    }
}

namespace fable::multiplayer::combat
{
    void* PlayerCombatantDirectory::FindCreature(
        const std::uint64_t) const noexcept
    {
        return nullptr;
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

    bool CreatureCombatService::AddHealthMutationSink(
        HealthMutationSink,
        void*) noexcept
    {
        return true;
    }

    void CreatureCombatService::RemoveHealthMutationSink(
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

    bool CreatureCombatService::ApplyOwnedCombatHealing(
        void*,
        float) noexcept
    {
        return false;
    }

    bool CreatureCombatService::SetReplicaHealthProtection(
        void*,
        bool) noexcept
    {
        return true;
    }
}

namespace fable::game::creature::locomotion
{
    bool CreatureModeManagerObserver::IsInstalled() const noexcept
    {
        return true;
    }

    bool CreatureModeManagerObserver::AddModeSourceEventSink(
        ModeSourceEventSink,
        void*) noexcept
    {
        return true;
    }

    void CreatureModeManagerObserver::RemoveModeSourceEventSink(
        ModeSourceEventSink,
        void*) noexcept
    {
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

    std::uint64_t WorldEntityDirectory::LatestWorldRevision()
        const noexcept
    {
        return 0;
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

    std::uint64_t LiveEntityRegistry::Revision() const noexcept
    {
        return 0;
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
