# Enemy AI, navigation, and locomotion analysis

## Current finding

The sliding puppet is not an animation-blueprint problem in the UE4/5 sense.
Fable's native creature stack separates navigation/physics displacement from a
creature-mode animation selector. Earlier scripted `Follow` tests reached
native action and navigator code but could move the creature while the visible
locomotion mode still saw an idle motion vector.

The source-1 locomotion evaluator reads two floats from its owning `CThing` at
`+0x134` and `+0x138`, computes their planar magnitude, and selects between
idle, slow walk, walk, jog, run, and sprint states. A near-zero magnitude takes
the idle path unless a short motion-hold counter is active. This directly fits
the observed result: world position changes while the actor remains visually
idle.

The natural writers are now mapped as well. Virtual slot 22 on both
`CThingCreature` and `CThingPlayerCreature` calculates current transform minus
the prior-frame transform, then stores X/Y/Z at `+0x134/+0x138/+0x13C` before
the creature-mode evaluator runs. These are actual per-frame displacement
values, not requested speed, navigation intent, or an arbitrary animation
parameter.

## Recovered ownership chain

1. `CAIBrain` selects autonomous behaviors and perception targets.
2. `CTCScriptedControl` (component `0x1F`) owns explicit actions such as
   `Follow`. It is separate from the AI brain.
3. The creature's navigation dispatcher resolves either the scripted action or
   brain intent into a navigation step.
4. `CTCCreatureNavigation` (component `0x07`) computes a bounded next-step
   displacement.
5. `CTCPhysicsNavigator` (component `0x02`) integrates the creature's physical
   position.
6. `CTCCreatureModeManager` (component `0x31`) owns an ordered stack of modes;
   source 1 installs the standard locomotion animation set.
7. The active locomotion mode reads `CThing + 0x134/+0x138` to select gait in
   `CTCAnimationComplex` (component `0x5A`).
8. Look/perception (component `0x43` plus targeting component `0x08`) can still
   rotate the creature toward a perception target independently of Follow.

This explains both user-visible symptoms: translation can work without gait,
and facing can remain owned by perception instead of player movement.

## Current-build native map

| Preferred address | Working name | Evidence |
| --- | --- | --- |
| `0x01AD7F20` | `CAIBrain_SelectAndRunBehavior` | brain decision boundary |
| `0x01F356D0` | `CThingCreature_ApplyNavigationAndLook` | dispatches scripted/brain movement and applies the result |
| `0x01F36F20` | `CThingCreature_UpdateFrame` | vtable slot 22; derives actual creature frame displacement at `+0x134/+0x138/+0x13C` |
| `0x01F60940` | `CThingPlayerCreature_UpdateFrame` | player vtable slot 22; derives the Hero's equivalent frame displacement |
| `0x01F3B800` | `CThingCreature_AdvanceBrainNavigationIntent` | forwards enabled brain intent |
| `0x01F3BBD0` | `CThingCreature_ResolveNavigationStep` | produces bounded navigation displacement |
| `0x01D76B50` | `CTCCreatureNavigation_ComputeStep` | native next-step computation |
| `0x01D74A50` | `CTCCreatureModeManager_AddSource` | inserts a source-specific mode |
| `0x01D73A10` | `CTCCreatureModeManager_RemoveSource` | removes a source-specific mode |
| `0x01C19840` | `CreatureLocomotionMode_Activate` | installs idle/walk/jog/run/sprint handles |
| `0x01C18E70` | `CreatureLocomotionMode_Deactivate` | releases those handles |
| `0x01C1A4B0` | `CreatureLocomotionMode_EvaluateOwnerMotion` | reads owner `+0x134/+0x138` and chooses idle or moving path |
| `0x01C1A060` | `CreatureLocomotionMode_SelectMovingState` | moving gait selection |
| `0x01C19440` | `CreatureLocomotionMode_SelectIdleState` | idle selection |

The targeted IDA database contains these names and comments. Source number 1 is
verified as the standard locomotion family. Source 7 was an early hypothesis
for Follow, but runtime observation did not show a source-7 transition during
the action, so that hypothesis is rejected.

## Verified player-frame input route

The client now watches the exact three script-spawned creature owners. It logs:

- source/mode changes;
- every bounded sample of the source-1 locomotion evaluator;
- owner motion values at `+0x134/+0x138`;
- physics navigator displacement and the same owner motion values before and
  after integration;
- animation-state memory transitions.

`Creature::RoutePlayerFrameInput(sourceHero, targetCreature)` validates the
current executable, both slot-22 functions, both owning vtables, and the target
physics navigator. After the retail Hero frame update derives finite nonzero
displacement, the hook submits that delta as a next-position request through
the NPC's current navigator vtable. The NPC's own physics and
`CThingCreature_UpdateFrame` then remain authoritative for actual displacement
and the `+0x134/+0x138/+0x13C` motion fields consumed by gait selection.

This does not teleport the creature and does not replace its navigation,
physics, animation-complex, or mode stack. Adult-town run
`20260811-105818-185-34352` verified routed requests, target physics and CThing
displacement, nonzero target motion in the retail locomotion evaluator,
animation-state activity, stable Hero identity, and clean restoration. Visible
gait remains a visual gate, and look/perception can still steal facing.

The narrower `Creature::MirrorAnimationMotion` diagnostic remains available
experimentally, but the player-control scenario no longer uses it: native
physical routing naturally causes the NPC's own frame writer to produce the
same gait input, which is the more faithful ownership model.

Acceptance for the next implementation is visual as well as structural:

- puppet translates and rotates from player-owned intent;
- idle/walk/run transitions match speed and stop cleanly;
- the hidden Hero follows without driving presentation;
- the puppet's perception/brain cannot steal facing or submit attacks;
- no direct per-frame teleport is used for ordinary locomotion;
- restoration and map teardown complete without faults.
