# Scripted creature puppet

The current appearance experiment preserves Fable's real player object rather
than replacing or converting it. Number-row `1` runs an ordinary AngelScript
module through the public framework; the older `TurnCreatureInto` experiment is
available only through the explicit `--transform-probe` unsafe mode.

## Runtime contract

1. `Player::GetHero()` resolves and retains the authoritative
   `CThingPlayerCreature`.
2. `NPC::Spawn()` creates a complete native creature with its own locomotion,
   animation, combat, perception, and AI component stack.
3. The puppet is made non-attackable and non-damageable but keeps its real
   collision/physics stack. The authoritative Hero is made non-collidable and
   `HeroPawn::SetVisible(Hero, false)` hides only its presentation.
4. `NPC::TakeControl()` obtains Fable's native scripted-control object.
5. The first guard clears scripted actions and binds
   `Creature::RoutePlayerFrameInput(Hero, Puppet)`. Finite displacement from
   the Hero's verified player frame-update slot becomes a native next-position
   request for the guard's own `CTCPhysicsNavigator`. The guard's retail
   `CThingCreature` frame update derives its own actual displacement and feeds
   the normal idle/walk/run evaluator. There is no per-frame teleport. Visible
   gait and player-owned facing are separate acceptance gates.
6. Cycling clears the current native action, hides the retired puppet, and
   releases the scripted-control handle. The retired creature handle remains
   guarded because native destruction is still deferred until a verified
   regional teardown path is mapped.
7. `Creature::RoutePlayerCombat(Hero, Puppet)` arms a fail-closed native hook at
   `CThingCreature::SubmitAbility`. It routes only calls whose return address is
   Fable's resolved `CREATURE_ABILITY_ATTACK` player-command handler. When that
   handler submits the hidden Hero as the creature, the hook substitutes the
   controlled NPC and calls the original engine function. No mouse state or
   synthetic target is read by the injected client; the NPC keeps its native
   attack animation, weapon sweep, hit detection, and target selection.
8. `Shift+1` clears control, retires the puppet, and restores Hero visibility.

The creature retains its native component stack, while highest-priority scripted
control owns decisions and the current friendly policy prevents the proxy form
from inheriting hostile world behavior. Full faction, affinity, aggro, and
server-authoritative combat policy remain separate services.

## Controls

- Number-row `1`: spawn and control the next creature definition.
- `Shift+1`: restore the authoritative Hero.
- F5: rebuild all deployed AngelScript modules.

## Automated proof

Run:

```powershell
.\bin\Release\FableTogether.Launcher.exe --automation appearance_cycle
```

The launcher copies the bundled adult Bowerstone North save into a fresh,
run-specific Documents tree. The scenario loads the exact `AutoSave`, verifies
the adult Hero and current region, then exercises guard, Bowerstone villager,
hobbe, and Hero restoration through the public AngelScript services.

Run `20260811-143557-839-33304` passed with:

- typed module state restored from a prior process and flushed again;
- one-shot and repeating scheduler callbacks plus in-callback cancellation;
- named `WorldReady` event delivery plus in-callback unsubscription;
- native health and interaction-state reads for all three forms;
- definition, script name, current-map, and home-map metadata for all forms;
- active, registered, completed, and failed quest-state reads against the
  disposable adult save;
- Hero frame displacement routed into native guard navigator requests, followed
  by guard-owned `CThing` and physics-navigator displacement;
- nonzero guard motion fields consumed by the retail locomotion evaluator;
- bounded animation-state memory activity, without treating it as visible gait
  proof;
- stable Hero identity, region, and combat health through restoration;
- native ability command `0x16` observed at `CGamePlayerInterface::PollCommand`;
- `CGamePlayer_ProcessAttackAbilityCommand` calling the hooked creature ability
  boundary with ability ID `1101`;
- exact source substitution from hidden Hero `88ABC800` to guard proxy
  `88446E00`, followed by the original native ability function;
- run-scoped clean shutdown and no `ClientFailed` event.

## Current limitation

The current proof establishes native navigation ownership, visible gait,
player-owned facing, hidden-Hero shadow follow, and player ATTACK-to-NPC native
ability routing for the supported proxy forms. It does not yet establish the
NPC's final weapon-selection policy, damage attribution against a live target,
combo/flourish/block routing, or server-authoritative hit results. Production
masquerade still needs server reconciliation, equipment suppression/restoration,
broader combat abilities, faction/aggro policy, map-transition teardown, and a
stable server entity identity.
