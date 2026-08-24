#pragma once

#include "../../PeerHarness.h"

#include <cstdint>
#include <string>

namespace fable::launcher::multiplayer::combat
{
enum class CombatPhaseResult
{
    Ready,
    ManualLeaveRunning,
    Failed
};

struct CombatScenarioState final
{
    Peer *host = nullptr;
    Peer *guest = nullptr;
    unsigned int timeoutSeconds = 0;
    bool interactive = false;
    bool heroWill = false;
    std::uint64_t hostActorId = 0;
    std::uint64_t guestActorId = 0;
    std::size_t guestPlayerVitalsBeforeAttack = 0;
    std::size_t hostRemoteVitalsBeforeAttack = 0;
    std::string guestActor;
    std::string guestCombatOwner;
    std::string guestVitalsOwner;
    std::string guestPlayerVitals;
    std::string guestRemoteVitals;
    std::string guestRemoteCompanion;
};
} // namespace fable::launcher::multiplayer::combat
