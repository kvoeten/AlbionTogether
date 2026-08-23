#pragma once

#include "Core/Bootstrap/ClientRuntimeState.h"

namespace fable::automation::character_snapshot
{
    bool InitializeCharacterSnapshot();
    bool ReadCharacterState(
        fable::core::bootstrap::GameScriptInterface* gameInterface,
        fable::core::bootstrap::ScriptThing& hero,
        fable::core::bootstrap::CharacterState& state,
        const char*& failure);
    bool CharacterSnapshotBaselineIsStable(
        const fable::core::bootstrap::CharacterState& state);
    bool ApplyCharacterSnapshot(
        fable::core::bootstrap::GameScriptInterface* gameInterface,
        fable::core::bootstrap::ScriptThing& hero,
        const fable::core::bootstrap::CharacterState& before,
        const char*& failure);
    bool CharacterSnapshotMatches(
        const fable::core::bootstrap::CharacterState& state,
        const char*& failure);
    void ObserveBootstrapHeroReadiness();
}

