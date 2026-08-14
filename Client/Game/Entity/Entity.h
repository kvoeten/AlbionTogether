#pragma once

#include "../Math/Vector3.h"
#include "../Native/ScriptTypes.h"

#include <atomic>
#include <string>

namespace fable::game
{
    class EntityService;
    class ScriptControl;

    enum class AiPriority : int
    {
        Lowest = 0,
        Lower = 1,
        Normal = 2,
        Higher = 3,
        High = 4,
        Highest = 5,
    };

    class Entity final
    {
    public:
        Entity(EntityService& service, native::ScriptThing handle);

        void AddRef() noexcept;
        void Release() noexcept;

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] bool IsAlive() const;
        [[nodiscard]] bool IsDead() const;
        [[nodiscard]] bool IsSneaking() const;
        [[nodiscard]] bool IsAwareOfHero() const;
        [[nodiscard]] bool IsUnconscious() const;
        [[nodiscard]] bool IsUsable() const;
        [[nodiscard]] bool IsOpenDoor() const;
        [[nodiscard]] bool IsSummonedCreature() const;
        [[nodiscard]] std::string GetName() const;
        [[nodiscard]] std::string GetDefinitionName() const;
        [[nodiscard]] std::string GetDataString() const;
        [[nodiscard]] std::string GetCurrentMapName() const;
        [[nodiscard]] std::string GetHomeMapName() const;
        [[nodiscard]] bool GetActivationTriggerStatus() const;
        [[nodiscard]] int GetScriptCounter() const;
        [[nodiscard]] Vector3 GetPosition() const;
        [[nodiscard]] float GetFacing() const;

        bool Teleport(const Vector3& position, float facing, bool effect = false);
        bool SetAttackable(bool enabled);
        bool SetDamageable(bool enabled);
        bool SetCollidable(bool enabled);
        bool SetDrawable(bool enabled);
        bool SetDataString(const std::string& value);
        bool SetUsable(bool enabled);
        bool SetFriendsWithEverything(bool enabled);
        bool SetActivationTriggerStatus(bool enabled);
        bool SetKillOnLevelUnload(bool enabled);
        bool RequestDestroy(bool immediate = false);
        bool UpdateAttachment();
        bool IncrementScriptCounter();
        bool DecrementScriptCounter();
        bool Attack(Entity* target, bool stopCurrentAction = true, bool unsheathe = true);
        ScriptControl* AcquireControl(AiPriority priority = AiPriority::Highest);

        [[nodiscard]] const native::ScriptThing& NativeHandle() const noexcept;

    private:
        ~Entity();

        std::atomic_uint referenceCount_{1};
        EntityService* service_ = nullptr;
        native::ScriptThing handle_ = {};
    };
}
