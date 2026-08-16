#include "ReplicatedActorMovement.h"

#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionState.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/Entity/Entity.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace fable::multiplayer::movement
{
    void ReplicatedActorMovement::Initialize(
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        const core::Diagnostics& diagnostics)
    {
        Detach();
        locomotion_ = &locomotion;
        diagnostics_ = diagnostics;
    }

    void ReplicatedActorMovement::Bind(
        game::Entity& actor,
        void* nativeActor,
        const ReplicatedMovementSample& sample,
        const std::string& localMap)
    {
        Detach();
        std::lock_guard<std::mutex> lock(stateMutex_);
        actor.AddRef();
        actor_ = &actor;
        nativeActor_ = nativeActor;
        samples_[0] = sample;
        sampleCount_ = 1;
        localMap_ = localMap;
        lastPresentationPosition_ = sample.position;
        presentationVelocity_ = {};
        lastPresentationFacing_ = sample.facing;
        presentationAngularVelocity_ = 0.0f;
        lastPresentationAt_ = 0;
        presentationMotionReady_ = false;
        locomotionStartPosition_ = sample.position;
        locomotionStartAnimationHash_ = 0;
        lastObservationAt_ = 0;
        movementCommanded_ = false;
        walkingReported_ = false;
        if (locomotion_ != nullptr)
        {
            if (auto* locomotionState = locomotion_->Inspect(&actor))
            {
                locomotionStartAnimationHash_ =
                    locomotionState->AnimationStateHash();
                locomotionState->Release();
            }
        }
    }

    void ReplicatedActorMovement::BindNative(
        void* nativeActor,
        const ReplicatedMovementSample& sample,
        const std::string& localMap)
    {
        Detach();
        std::lock_guard<std::mutex> lock(stateMutex_);
        nativeActor_ = nativeActor;
        samples_[0] = sample;
        sampleCount_ = 1;
        localMap_ = localMap;
        lastPresentationPosition_ = sample.position;
        presentationVelocity_ = {};
        lastPresentationFacing_ = sample.facing;
        presentationAngularVelocity_ = 0.0f;
        lastPresentationAt_ = 0;
        presentationMotionReady_ = false;
        locomotionStartPosition_ = sample.position;
        locomotionStartAnimationHash_ = 0;
        lastObservationAt_ = 0;
        movementCommanded_ = false;
        walkingReported_ = false;
    }

    void ReplicatedActorMovement::Update(
        const ReplicatedMovementSample& sample,
        const std::string& localMap)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        localMap_ = localMap;
        if (nativeActor_ == nullptr || sample.actorId == 0 ||
            sample.mapName.empty())
        {
            return;
        }
        if (sampleCount_ != 0)
        {
            const ReplicatedMovementSample& latest = samples_[sampleCount_ - 1];
            const bool newAuthority = sample.actorId != latest.actorId ||
                sample.authorityEpoch != latest.authorityEpoch ||
                sample.mapName != latest.mapName;
            if (newAuthority)
            {
                sampleCount_ = 0;
            }
            else if (!IsNewerSequence(sample.sequence, latest.sequence))
            {
                return;
            }
        }
        if (sampleCount_ == samples_.size())
        {
            std::move(
                samples_.begin() + 1,
                samples_.end(),
                samples_.begin());
            --sampleCount_;
        }
        samples_[sampleCount_++] = sample;
    }

    void ReplicatedActorMovement::Detach() noexcept
    {
        game::Entity* retainedActor = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            retainedActor = actor_;
            game::creature::locomotion::CreatureModeManagerObserver::
                ClearReplicatedAnimationMotion(nativeActor_);
            samples_ = {};
            sampleCount_ = 0;
            localMap_.clear();
            actor_ = nullptr;
            nativeActor_ = nullptr;
            lastPresentationPosition_ = {};
            presentationVelocity_ = {};
            lastPresentationFacing_ = 0.0f;
            presentationAngularVelocity_ = 0.0f;
            lastPresentationAt_ = 0;
            presentationMotionReady_ = false;
            locomotionStartPosition_ = {};
            locomotionStartAnimationHash_ = 0;
            lastObservationAt_ = 0;
            movementCommanded_ = false;
            walkingReported_ = false;
        }
        if (retainedActor != nullptr)
        {
            retainedActor->Release();
        }
    }

    bool ReplicatedActorMovement::Provide(
        void* nativeActor,
        NativeInput& input)
    {
        game::Entity* actor = nullptr;
        ReplicatedMovementSample sample;
        const std::uint64_t now = GetTickCount64();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (nativeActor == nullptr || nativeActor != nativeActor_ ||
                sampleCount_ == 0 ||
                samples_[sampleCount_ - 1].mapName.empty() ||
                samples_[sampleCount_ - 1].mapName != localMap_ ||
                !Evaluate(now, sample))
            {
                return false;
            }
            DerivePresentationMotion(now, sample);
            game::creature::locomotion::CreatureModeManagerObserver::
                SetReplicatedAnimationMotion(
                    nativeActor_,
                    sample.velocity,
                    sample.angularVelocity);
            actor = actor_;
            if (actor != nullptr)
            {
                actor->AddRef();
            }
        }
        input.actorId = sample.actorId;
        input.position = sample.position;
        input.velocity = sample.velocity;
        input.facing = sample.facing;
        input.angularVelocity = sample.angularVelocity;
        input.moving = sample.moving;
        input.sampleAgeSeconds = sample.receivedAt == 0 || now <= sample.receivedAt
            ? 0.0f
            : static_cast<float>(now - sample.receivedAt) / 1000.0f;
        if (actor == nullptr)
        {
            return true;
        }
        if (!actor->IsValid())
        {
            actor->Release();
            return false;
        }

        bool reportMovement = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!movementCommanded_ &&
                (sample.moving ||
                    actor->GetPosition().HorizontalDistanceTo(sample.position) >=
                        0.05f))
            {
                movementCommanded_ = true;
                locomotionStartPosition_ = actor->GetPosition();
                if (locomotion_ != nullptr)
                {
                    if (auto* locomotionState = locomotion_->Inspect(actor))
                    {
                        locomotionStartAnimationHash_ =
                            locomotionState->AnimationStateHash();
                        locomotionState->Release();
                    }
                }
                reportMovement = true;
            }
        }
        if (reportMovement)
        {
            diagnostics_.Event(
                "MultiplayerRemoteMovementConsumed",
                "remote creature frame consumed an owner-authored movement property");
        }

        ObserveLocomotion(*actor, now);
        actor->Release();
        return true;
    }

    void ReplicatedActorMovement::DerivePresentationMotion(
        std::uint64_t now,
        ReplicatedMovementSample& sample) noexcept
    {
        constexpr float kMovementSpeedThresholdSquared = 0.0025f;
        // Position remains on the delayed interpolation timeline, but once
        // the newest owner sample says the actor has stopped we can end the
        // locomotion blend before the low-pass velocity tail reaches exact
        // zero. Normal low-speed movement is unaffected while the owner still
        // reports movement.
        constexpr float kOwnerStopAnimationSpeed = 0.75f;
        constexpr float kOwnerStopAnimationSpeedSquared =
            kOwnerStopAnimationSpeed * kOwnerStopAnimationSpeed;
        const bool ownerStopped = sampleCount_ != 0 &&
            !samples_[sampleCount_ - 1].moving;
        const auto finishStoppingAnimation = [this, ownerStopped]() noexcept
        {
            const float planarSpeedSquared =
                presentationVelocity_.x * presentationVelocity_.x +
                presentationVelocity_.y * presentationVelocity_.y;
            if (ownerStopped &&
                planarSpeedSquared < kOwnerStopAnimationSpeedSquared)
            {
                presentationVelocity_ = {};
                return 0.0f;
            }
            if (planarSpeedSquared < kMovementSpeedThresholdSquared)
            {
                presentationVelocity_ = {};
                return 0.0f;
            }
            return planarSpeedSquared;
        };

        if (!presentationMotionReady_ || lastPresentationAt_ == 0 ||
            now <= lastPresentationAt_)
        {
            lastPresentationPosition_ = sample.position;
            lastPresentationAt_ = now;
            presentationMotionReady_ = true;
            presentationVelocity_ = {};
            presentationAngularVelocity_ = 0.0f;
            sample.velocity = {};
            sample.angularVelocity = 0.0f;
            sample.moving = false;
            lastPresentationFacing_ = sample.facing;
            return;
        }

        const std::uint64_t elapsedMilliseconds = now - lastPresentationAt_;
        // The same presentation can be queried more than once during a native
        // frame. Reuse the last derived velocity rather than introducing a
        // false zero between the game thread and the background driver.
        if (elapsedMilliseconds < 4)
        {
            const float planarSpeedSquared = finishStoppingAnimation();
            sample.velocity = presentationVelocity_;
            sample.angularVelocity = presentationAngularVelocity_;
            sample.moving =
                planarSpeedSquared >= kMovementSpeedThresholdSquared;
            return;
        }

        const float elapsedSeconds = static_cast<float>(
            (std::min<std::uint64_t>)(elapsedMilliseconds, 100)) / 1000.0f;
        const game::Vector3 measuredVelocity = {
            (sample.position.x - lastPresentationPosition_.x) / elapsedSeconds,
            (sample.position.y - lastPresentationPosition_.y) / elapsedSeconds,
            (sample.position.z - lastPresentationPosition_.z) / elapsedSeconds,
        };
        float facingDelta = sample.facing - lastPresentationFacing_;
        if (facingDelta > 0.5f)
        {
            facingDelta -= 1.0f;
        }
        else if (facingDelta < -0.5f)
        {
            facingDelta += 1.0f;
        }
        const float measuredAngularVelocity = facingDelta / elapsedSeconds;
        lastPresentationPosition_ = sample.position;
        lastPresentationFacing_ = sample.facing;
        lastPresentationAt_ = now;

        // Smooth the velocity of the already interpolated/extrapolated
        // presentation. This preserves real acceleration while preventing
        // packet cadence or duplicate samples from repeatedly restarting the
        // locomotion blend.
        constexpr float kResponseSeconds = 0.065f;
        const float alpha = 1.0f - std::exp(-elapsedSeconds / kResponseSeconds);
        // The evaluated presentation delta is authoritative here. Packet
        // moving flags only describe the owner capture instant; gating on
        // them can stop an interpolation segment halfway through a rendered
        // stride.
        const game::Vector3 targetVelocity = measuredVelocity;
        presentationVelocity_.x +=
            (targetVelocity.x - presentationVelocity_.x) * alpha;
        presentationVelocity_.y +=
            (targetVelocity.y - presentationVelocity_.y) * alpha;
        presentationVelocity_.z +=
            (targetVelocity.z - presentationVelocity_.z) * alpha;
        presentationAngularVelocity_ +=
            (measuredAngularVelocity - presentationAngularVelocity_) * alpha;

        const float planarSpeedSquared = finishStoppingAnimation();
        sample.velocity = presentationVelocity_;
        sample.angularVelocity = presentationAngularVelocity_;
        sample.moving =
            planarSpeedSquared >= kMovementSpeedThresholdSquared;
    }

    bool ReplicatedActorMovement::Evaluate(
        std::uint64_t now,
        ReplicatedMovementSample& output) const noexcept
    {
        if (sampleCount_ == 0)
        {
            return false;
        }
        const std::uint64_t targetTime = now > InterpolationDelayMilliseconds
            ? now - InterpolationDelayMilliseconds
            : 0;
        const ReplicatedMovementSample& oldest = samples_[0];
        if (sampleCount_ == 1 || targetTime <= oldest.receivedAt)
        {
            output = oldest;
            return true;
        }

        for (std::size_t index = 1; index < sampleCount_; ++index)
        {
            const ReplicatedMovementSample& previous = samples_[index - 1];
            const ReplicatedMovementSample& next = samples_[index];
            if (targetTime > next.receivedAt ||
                next.receivedAt <= previous.receivedAt)
            {
                continue;
            }
            const float alpha = std::clamp(
                static_cast<float>(targetTime - previous.receivedAt) /
                    static_cast<float>(next.receivedAt - previous.receivedAt),
                0.0f,
                1.0f);
            output = next;
            output.position = {
                previous.position.x +
                    (next.position.x - previous.position.x) * alpha,
                previous.position.y +
                    (next.position.y - previous.position.y) * alpha,
                previous.position.z +
                    (next.position.z - previous.position.z) * alpha,
            };
            output.velocity = {
                previous.velocity.x +
                    (next.velocity.x - previous.velocity.x) * alpha,
                previous.velocity.y +
                    (next.velocity.y - previous.velocity.y) * alpha,
                previous.velocity.z +
                    (next.velocity.z - previous.velocity.z) * alpha,
            };
            output.angularVelocity = previous.angularVelocity +
                (next.angularVelocity - previous.angularVelocity) * alpha;
            float facingDelta = next.facing - previous.facing;
            if (facingDelta > 0.5f)
            {
                facingDelta -= 1.0f;
            }
            else if (facingDelta < -0.5f)
            {
                facingDelta += 1.0f;
            }
            output.facing = previous.facing + facingDelta * alpha;
            output.facing -= std::floor(output.facing);
            if (output.facing < 0.0f)
            {
                output.facing += 1.0f;
            }
            output.moving = previous.moving || next.moving;
            return true;
        }

        const ReplicatedMovementSample& latest = samples_[sampleCount_ - 1];
        output = latest;
        const std::uint64_t extrapolationMilliseconds =
            targetTime > latest.receivedAt
                ? (std::min)(
                    targetTime - latest.receivedAt,
                    MaximumExtrapolationMilliseconds)
                : 0;
        const float seconds =
            static_cast<float>(extrapolationMilliseconds) / 1000.0f;
        const float extrapolatedSpeedSquared =
            latest.velocity.x * latest.velocity.x +
            latest.velocity.y * latest.velocity.y +
            latest.velocity.z * latest.velocity.z;
        if (extrapolatedSpeedSquared >= 0.0025f && seconds > 0.0f)
        {
            output.position.x += latest.velocity.x * seconds;
            output.position.y += latest.velocity.y * seconds;
            output.position.z += latest.velocity.z * seconds;
        }
        if (std::fabs(latest.angularVelocity) >= 0.0005f && seconds > 0.0f)
        {
            output.facing += latest.angularVelocity * seconds;
            output.facing -= std::floor(output.facing);
            if (output.facing < 0.0f)
            {
                output.facing += 1.0f;
            }
        }
        output.moving = extrapolatedSpeedSquared >= 0.0025f;
        return true;
    }

    bool ReplicatedActorMovement::IsNewerSequence(
        std::uint32_t candidate,
        std::uint32_t current) noexcept
    {
        return candidate != current &&
            static_cast<std::int32_t>(candidate - current) > 0;
    }

    void ReplicatedActorMovement::ObserveLocomotion(
        game::Entity& actor,
        std::uint64_t now)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!movementCommanded_ || walkingReported_ || locomotion_ == nullptr ||
            (lastObservationAt_ != 0 && now - lastObservationAt_ < 100))
        {
            return;
        }
        lastObservationAt_ = now;
        game::creature::locomotion::CreatureLocomotionState* state =
            locomotion_->Inspect(&actor);
        if (state == nullptr)
        {
            return;
        }
        const bool displaced = state->PhysicsPosition().HorizontalDistanceTo(
            locomotionStartPosition_) >= 0.20f;
        const bool animated = locomotionStartAnimationHash_ != 0 &&
            state->AnimationStateHash() != 0 &&
            state->AnimationStateHash() != locomotionStartAnimationHash_;
        const bool fullStack = state->HasPhysicsNavigator() &&
            state->HasCreatureNavigation() && state->HasAnimationComplex();
        state->Release();
        if (!displaced || !animated || !fullStack)
        {
            return;
        }
        walkingReported_ = true;
        diagnostics_.Event(
            "MultiplayerRemoteAvatarWalking",
            "owner movement property produced remote native physics displacement and animation-complex activity");
    }
}
