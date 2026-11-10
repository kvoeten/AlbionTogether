# Action replication architecture

AlbionTogether keeps Fable's native action graph responsible for animation,
effects, hit timing, and component state. Multiplayer sends semantic requests
and authority-fenced lifecycle events; it does not serialize animation poses or
native action objects.

## Ownership boundary

- `Client/Game/` contains verified Fable ABI access, native hooks, action
  construction, and synchronous observation. It does not decide multiplayer
  ownership or publish packets.
- `Client/Multiplayer/Replication/` turns native observations into ordered,
  fenced protocol messages and applies authoritative messages.
- `Client/Multiplayer/Presentation/RemoteHero/` owns remote-only Hero actors and
  their appearance, equipment, combat, ability, and expression presentation.
  Native Hero hooks remain under `Client/Game/HeroPawn/`.

## Local action capture

```text
semantic entry point
    -> ScopedActionSubmissionContext(correlation ID, submitted creature)
    -> Fable constructs an action
    -> CThingCreature::SubmitAction
    -> CreatureActionLifecycleObserver records accepted/rejected + correlation
    -> semantic request and accepted native action are paired exactly
    -> ordered reliable action message
```

The scoped correlation is thread-local and synchronous. It deliberately allows
the semantic source and submitted native creature to differ: a Hero command can
be routed to a controlled puppet while remaining the same semantic request.
Zero-correlation matching is retained only for synthetic or legacy producers,
where it is restricted to the same creature and a bounded expiry window.

Rejected native submissions are not published as performed actions. Native
follow-up stages created by an already replicated ability remain inside Fable's
local action graph and are not duplicated over the network.

## Player and world-entity lanes

- `PlayerActionReplication` handles Hero actions on a reliable per-actor stream.
  A newly constructed remote Hero must reach `AppearanceApplied` and then
  `Active` before action playback is allowed.
- `EntityActionReplication` handles NPC/world-creature actions. Capture,
  authority admission, replay, state, semantics, and publication live in the
  `EntityActions/` folder. Messages are fenced by entity generation, map epoch,
  action epoch, and owner actor.
- Movement remains a separate unreliable snapshot lane with smoothing. It never
  carries action ordering or construction state.

## Remote presentation

Reliable messages preserve per-actor order. The receiver validates session,
actor generation, map epoch, and presentation readiness before replay. The
latest authoritative state constructs the actor; realtime actions then drive
the existing native components. Replicated native submissions are marked as
`ReplicatedPresentation` so they cannot be recaptured as new local actions or
produce duplicate authority-side combat effects.

Native replay wrappers use a scoped submission receipt. A receipt records only
the first direct `SubmitAction` reached by that wrapper: nested native actions
and later sibling submissions cannot make a rejected request appear accepted.
Receipt scopes nest and restore in LIFO order, including across SEH cleanup.

## Native layout rule

Raw action data offsets and RTTI reads belong in
`Game/Creature/Actions/Native/CreatureActionInspector`; callable action-vtable
slots belong in `CreatureActionFunctions`. Hooks consume those fail-closed
native helpers instead of repeating ABI offsets. A native action pointer may
cross the callback boundary only as an opaque, local lifecycle identity used to
pair Submit and Finish; it is never dereferenced by multiplayer code,
serialized, or treated as stable after finish/fencing.

## Validation policy

Regression checks must use one of two explicit paths:

- Fully automated code-driven tests that run unattended and produce a clear
  pass/fail report.
- A manual game session performed by the user after they approve its launch.

Do not substitute scripted player input, agent-directed gameplay, or visual
guesswork for either path. Automated checks may build binaries, execute test
programs, and inspect their reports, but they must not take over the user's
mouse or keyboard. Manual clients remain open until the user reports their
result.
