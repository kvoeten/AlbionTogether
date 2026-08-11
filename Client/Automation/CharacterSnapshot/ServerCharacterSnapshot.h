#pragma once

#include <string>

namespace fable::automation::character_snapshot
{
    struct ServerCharacterSnapshot final
    {
        int schemaVersion = 0;
        std::string serverCharacterId;
        std::string displayName;
        std::string bootstrapSave;
        int regionIndex = 0;
        float position[3] = {};
        float facingAngle = 0.0f;
        float combatHealth = 0.0f;
    };

    class ServerCharacterSnapshotLoader final
    {
    public:
        static bool Load(
            const wchar_t* path,
            ServerCharacterSnapshot& snapshot,
            std::string& failure);
    };
}
