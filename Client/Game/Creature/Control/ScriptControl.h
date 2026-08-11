#pragma once

#include "Game/Entity/Entity.h"
#include "Game/Native/ScriptTypes.h"

#include <atomic>
#include <string>

namespace fable::game
{
    class EntityService;

    enum class MoveType : int
    {
        Walk = 0,
        Run = 1,
        Sneak = 2,
    };

    class ScriptControl final
    {
    public:
        static ScriptControl* Create(
            EntityService& service,
            const native::ScriptThing& entity,
            AiPriority priority);

        void AddRef() noexcept;
        void Release() noexcept;

        bool MoveToPosition(
            const Vector3& position,
            float radius,
            MoveType moveType,
            bool avoidDynamicObstacles = true,
            bool ignorePathPreferability = false);
        bool MoveToEntity(
            Entity* target,
            float radius,
            MoveType moveType,
            bool avoidDynamicObstacles = true,
            bool ignorePathPreferability = false,
            bool faceMovement = true);
        bool Follow(
            Entity* target,
            float distance,
            bool avoidDynamicObstacles = true);
        bool StopFollowing(Entity* target);
        bool FireProjectileAt(Entity* target);
        bool PlayAnimation(
            const std::string& animation,
            bool waitForFinish = false,
            bool stayOnLastFrame = false,
            bool allowLooking = true);
        bool PlayCombatAnimation(
            const std::string& animation,
            bool waitForFinish = false,
            bool allowLooking = true);
        bool PlayLoopingAnimation(
            const std::string& animation,
            int loops,
            bool useMovement = true,
            bool allowLooking = true);
        bool UnsheatheWeapons();
        bool ClearCommands();
        bool ClearAllActions(bool includeLoopingAnimations = true);
        bool ReleaseControl();
        [[nodiscard]] bool IsBusy() const;
        [[nodiscard]] bool IsValid() const;

    private:
        ScriptControl(EntityService& service, native::ScriptControlHandle handle);
        ~ScriptControl();

        [[nodiscard]] void** ExpertVtable() const;
        bool PlayAnimationNative(
            const char* animation,
            bool waitForFinish,
            bool stayOnLastFrame,
            bool allowLooking);
        bool PlayCombatAnimationNative(
            const char* animation,
            bool waitForFinish,
            bool allowLooking);
        bool PlayLoopingAnimationNative(
            const char* animation,
            int loops,
            bool useMovement,
            bool allowLooking);

        std::atomic_uint referenceCount_{1};
        EntityService* service_ = nullptr;
        native::ScriptControlHandle handle_ = {};
    };
}
