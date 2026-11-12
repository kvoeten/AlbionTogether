# Quest phase policy catalog

Status: campaign-wide runtime catalog, 2026-11-12. The three catalog fragments
under `Client/Game/Quest/Policy/Catalog` cover all 200 unique `AddQuest`
registrations in the installed Anniversary data exactly once. The runtime uses
those rows to decide whether a lifecycle edge is shared, Hero-local, host-only,
or blocked. A compile-time row count plus an order-independent retail-name
fingerprint prevents a same-sized but incomplete catalog from passing tests.
World-command, decision, transition, and reward columns document
the intended boundary for the next native command; they are not permission to
guess an unverified native hook. Rows marked `NeedsTrace` remain explicit test
targets and should be refined from front-to-back playthrough evidence.

## Policy vocabulary

| Policy | Intended meaning | Late join |
| --- | --- | --- |
| `SharedSimulation` | One authority advances a phase whose world actors, objectives, and consequences are deliberately shared. Replicas observe; do not independently run consequential logic. | Send phase/objective and relevant world-entity baseline; do not replay one-shot effects. |
| `PrivateParallel` | Each Hero independently runs a phase with private targets, counters, or tutorial interactions. No shared world mutation occurs unless a separately authorized request commits it. | Join the current shared world, but do not inherit another Hero's private phase or Hero-owned state. |
| `InitiatorOnly` | Only the initiating Hero runs a tutorial, minigame, interaction, or Hero-local reward path; shared actors/effects are separately mediated. | Do not start or replay the phase for a late peer; show current public outcome only. |
| `SharedDecisionConversation` | A quest-affecting conversation is presented to every player participating in that quest context. Their quest machines pause at the same decision barrier, but only the initiating Hero can choose; the host commits that choice and releases every participant together. | Join as a read-only observer while the barrier is unresolved, or receive the committed branch without replaying the conversation once it has resolved. |
| `GlobalTransition` | A phase changes map, chapter, population, cutscene, or other shared world branch. The host serializes and commits the transition. | Receive the committed destination/world branch and seed it; never replay the transition or cutscene. |
| `NeedsResearch` | Evidence is insufficient or mixed. Keep vanilla progression disabled or conservatively host-gated until the exact phase is traced. | Snapshot committed shared state only; omit private Hero state and do not replay phase entry. |

Policy attaches to a *phase and side effect*, not permanently to a quest name.
One quest can have an initiator-only choice, shared combat, a global map
transition, and private reward settlement. Host authority is not permission to
mutate a guest Hero's XP, money, inventory, appearance, age, or other save-owned
fields. Those must be applied only to the owning Hero (or through a reviewed,
idempotent owner request).

## Evidence and limits

Sources inspected:

- Shipped Anniversary `WellingtonGame/FableData/Build/Data/Levels/FinalAlbion.qst`
  and `GlobalQuests.qst`, plus level names under `Levels/Gameplay` and
  `Levels/FinalAlbion`. The former enumerates `AddQuest` and `AddTestQuest`
  names, labels, initial HSPs, and some INI/END/card references. These are
  registration/authoring clues, not complete phase logic.
- `.analysis/fork-review-20261112/docs/initial-hook-analysis.md` and
  `docs/quest-state-authority.md`: questless-world hazards and the current
  opaque `CQuestManager` snapshot approach. Snapshot transfer is bounded,
  validated, late-peer-republished, and applied before world construction; it
  is not a per-Hero policy catalog and does not establish safe concurrent
  quest execution.
- `.analysis/third-party/FableDecomp/docs/QUEST_VM_RE.md`,
  `ghidra_out/quest_registry_table.tsv`, quest activation/manager decomp, and
  the RTTI/PDB-name corpus. The corpus supports a compiled name-to-allocator
  quest registry and name-keyed activation; reflected names can be misleading
  (including allocator reuse/mislabeling), so do not infer quest behavior from
  class names alone.
- Existing `docs/quest-state-authority.md` describes the earlier snapshot
  design. It should be read alongside the reviewed-fork copy, not as proof of
  per-phase semantics.

Important boundary: shipped `.qst` registration and compiled executable
registration are distinct evidence. Static `AddTestQuest` metadata does not
prove that its attached `.ini`/`.end` contains the quest state machine; the
reverse-engineering corpus says quest logic is compiled and cutscene/region
command lists are data-driven. The `FinalAlbion.qst` catalog also includes
legacy/test/debug entries. `GlobalQuests.qst` is background/system behavior,
not a list of player story phases.

## Registered main/story quest families

The names below are the `Q_*`, `QS_*`, and `QR_*` quest registrations found in
`FinalAlbion.qst` (including helpers, debug/legacy entries, and phases). They
are grouped for navigation; family policy is only a starting hypothesis.

| Family / likely policy | Registered names | Hazards to resolve | Exact next tracing |
| --- | --- | --- | --- |
| Guild Training & childhood/intro — `InitiatorOnly` for tutorials, `NeedsResearch` for shared world effects | `Q_GuildTraining`, `Q_GuildTrainingDeparture`, `Q_GuildTrainingPreMelee`, `Q_GuildTrainingMelee`, `Q_GuildTrainingSkill`, `Q_GuildTrainingWill`, `Q_GuildTrainingWoodsDeparture`, `Q_GuildTrainingWoodsMelee`, `Q_GuildTrainingWoodsWill`, `Q_NewOakValeIntro`, `Q_NewOakValeIntro_PreAttack`, `Q__OakValeIntro_PostAttack`, `Q_BowerstoneTownLifeIntro` | Player-target tutorials and training reward/stat setters; phase split; childhood-to-adult time/age transition; scripted attackers/population; opening and Bowerstone cutscenes; map/scene warp; Hero-local XP/items. | Trace registration-to-allocator and each `Main/Init/OnPersist`; record every `SetObjective`, target/action command, `CreateCreature`, reward call, cutscene, scene/region switch, and Hero setter. Compare host/guest on a fresh save; verify adult character initialization and late join during each phase. |
| Orchard Farm raid / escort — `NeedsResearch` (likely `SharedSimulation` encounter, choice private) | `Q_OrchardFarmRaid`, `Q_OrchardFarmRaidGood`, `Q_OrchardFarmRaidEvil`, `Q_OrchardFarm_Barricade`, `QR_EscortTrader`, `QR_EscortTrader_Manager`, `Q_TraderEscort`, `Q_TraderEscortForceField`, `Q_TraderPath`, `Q_AmbushTraders`, `Q_AmbushTraders_ActivatePhase1`, `Q_AmbushTraders_ActivatePhase2`, `Q_AmbushTraders_ActivatePhase3` | Good/evil vote/choice; trader and guard escorts; barriers and gates; potentially generated bandits/guards; NPC scripted movement/targets; quest-key items; map switches; rewards to accepting Hero. `QR_*` helper/manager may double-run the same escort. | Trace quest acceptance through both branch allocators and escort manager: identify choice owner and commit point, spawn provenance/IDs, command ownership while pathing, failure/death conditions, barricade/forcefield door writes, and item/XP recipients. Run two Heroes accepting opposite branches and a late join during combat. |
| Arena — `SharedSimulation` combat under one owner; entry/tutorial and rewards `InitiatorOnly` | `Q_Arena`, `Q_ArenaHoldingScript`, `Q_HeroSoulsArena`, `Q_HeroSoulsPermanentArena` | Arena rounds are generated waves; scripted opponent/target selection; crowd/cutscene locks; gate transitions; arena-local versus global state; Champion trophy and Hero-local payout; possible debug/holding helpers. | Trace round loop and generator calls, exact spawn seeds, arena door/gate state, target/action submissions, round completion, exit warp, trophy/XP/gold ownership. Join mid-round and disconnect/handoff during a wave; verify no duplicate waves or reward. |
| Bandit, siege, bounty, and encounter quests — `NeedsResearch`, usually `SharedSimulation` for spawned encounter | `Q_BanditCamp`, `Q_BanditCampBossBattle`, `Q_BanditCamp_Barriers`, `Q_BanditCampHoldingScript`, `Q_OakValeBanditRaid`, `Q_BreakSiege`, `Q_BreakSiege_Blockage`, `Q_BountyHunt`, `Q_MinionCamp`, `Q_MinionClifftopChase`, `Q_MinionClifftopChase_Barriers`, `Q_WaspBoss`, `Q_WhiteBalverineKnotholeGlade`, `Q_WhiteBalverineWW`, `Q_HobbeCave`, `Q_HobbeCaveBarrier`, `Q_HobbeCaveGranny`, `Q_HobbeToothContest`, `Q_UndeadRising`, `Q_OpeningGraveyardSecretPassage`, `Q_SecretPassage` | Generated enemies and boss phases; hidden barriers/gates; target selection; hostage/escort NPCs; chest/key ownership; optional path/choice; boss/cinema; secret-passage map warp; local and world population consequences. | For each compiled quest allocator, trace spawn sites and generator enablement; map quest commands to native target and door/key mutation; enumerate success/fail/abort paths and owner-only rewards. Test spawn replication, death/rebirth, and repeated activation on two clients. |
| Prison / Bowerstone Jail — `GlobalTransition` for escape/scene changes, otherwise `NeedsResearch` | `Q_PrisonEscapeRescueMother`, `Q_PrisonEscapeFromCell`, `Q_PrisonRace`, `Q_PrisonWardenGame`, `Q_HangingTreeGood`, `Q_HangingTreeEvil`, `Q_MinionClifftopChase`, `Q_TentacleKrakenBossFight`, `QS_MeetSister`, `Hook_Fresco_06_Prison` | Cell/key state, guard factions and patrol scripts, warden minigame, timed race, mother rescue, prison-to-Kraken/Hook Coast transition, death/failure/retry, hanging-tree moral choice, cutscenes, cell keys and Hero-local rewards. | Trace prison state flags, quest-key checks, `SetDoorOpen`/barrier writes, map/region transition calls, prisoner/guard spawn and script-control lifetime, choice dispatch, and failure cleanup. Test late join in prison, at escape transition, and after destination load; confirm no duplicate cutscene or Hero reward. |
| Bargate / Hook Coast / endgame — `GlobalTransition` for chapter/map/ending commits; phases remain `NeedsResearch` | `Q_EndGame`, `Q_EndGame_Barriers`, `Q_EndGameFocalSites`, `Q_EndGameBossBattle`, `Q_EndGameBossBattleScenery`, `Q_DragonBossFight`, `Q_SummoningTheShip`, `Q_WizardBattle`, `Q_FireHeart`, `Q_AwakeningTheOracle`, `Q_AwakeningTheOracleMinigame`, `Q_HeroSouls`, `Q_HeroSoulsThunder`, `Q_HeroSoulsBriar`, `Q_HeroSoulsBriarMagicBarrier`, `Q_HeroSoulsMother`, `Q_HeroSoulsGuildmaster`, `Q_HeroSoulsNostro`, `Q_HerosOldHouse`, `Q_RansomVictim`, `Q_RansomVictimChiefsHouse`, `Q_RansomVictimStarted`, `Q_RansomVictimStartedNature`, `Q_RansomVictimRevenge`, `Q_RansomVictimNaturesRevenge`, `Q_TraderConflictGood`, `Q_TraderConflictGood_Extras`, `Q_TraderConflictEvil`, `Q_WhiteBalverineKnotholeGlade`, `Q_TraderPath` | Bargate gate/keys and transition into prison; ship summon and map warp; multi-stage Jack/Dragon boss with generated enemies; focal-site barrier; final weapon/ending choice (retain/destroy sword); cutscenes and ending lock; soul/weapon/trophy rewards; branch-specific village/population state; bespoke death/retry. | Trace chapter unlock predicates and exact transition commit; identify every world branch write and whether save persists it; trace boss actor birth and phase/target handoff; locate final choice and reward calls. Test host transfer/reconnect before and after each irreversible transition, and late join into each final world branch. |
| Choice, escort, and moral-branch family — `SharedDecisionConversation` when dialogue changes the shared quest branch, `SharedSimulation` after commit | `Q_TraderConflictGood`, `Q_TraderConflictGood_Extras`, `Q_TraderConflictEvil`, `Q_OrchardFarmRaidGood`, `Q_OrchardFarmRaidEvil`, `Q_RansomVictim`, `Q_RansomVictimRevenge`, `Q_RansomVictimNaturesRevenge`, `Q_HangingTreeGood`, `Q_HangingTreeEvil`, `Q_TraderEscort`, `QR_EscortTrader`, `QR_EscortTrader_Manager` | The initiating Hero exclusively owns the answer, while all participating peers must see the conversation and pause at the same barrier. Moral alignment or rewards can remain Hero-local even when the selected branch changes shared NPC/world state. Escorts and victims are shared actors; manager/helper registrations risk duplicate progression. | Trace conversation start, UI answer receipt, branch commit and barrier release separately. Identify which peers are participating, mirror the conversation read-only, reject non-initiator answers, then trace the recipient for each alignment/stat/reward call. Test simultaneous interaction attempts and late join during an unresolved choice. |
| Oracle / sister / clue and supporting state quests — `NeedsResearch` | `QS_GuardianSisterInfo`, `QS_GuardianSisterInfo2_SisterInBanditCamp`, `QS_GuardianTrophyDealerInfo`, `QS_MeetSister`, `QS_ScytheInfo`, `Q_HeroSouls`, `Q_HeroSoulsPermanentArena` | Information can be Hero-private, but gates the shared main path; flags can unlock characters, doors, or later scene branches; trophy/item ownership and sister/NPC spawn state. | Trace readers/writers of each flag, first activation sites, downstream quest/card unlocks, and whether any flag affects common population/doors. Keep clue delivery owner-only until proven otherwise. |
| Story/cutscene/debug and residual named quest registrations — `NeedsResearch` or disabled | `Q_CinemaTest`, `Q_PlayCutscene` (not present as `AddQuest`; only `CS_PlayCutscene` test registration), `CS_OakValeRevisited`, `CS_PlayCutscene`, `Hook_Fresco_01_EvilEpilogue`, `Hook_Fresco_02_GoodEpilogue`, `Hook_Fresco_03_JacksObsession`, `Hook_Fresco_04_Kraken`, `Hook_Fresco_05_MothersStory`, `Hook_Fresco_07_OakValeRaid`, `Hook_Fresco_08_SistersStory`, `Hook_Fresco_09_TimePassing`, `Hook_Fresco_10_UneasyAlliance`, `Hook_Fresco_11_Wedding`, `Hook_Fresco_12_KilledDragon`, `Hook_Fresco_13_KilledScorpion`, `Hook_BalverinesInKHGPreWhiteBalv`, `Hook_BowerstoneTeleportTutorial`, `OracleMinigameTest`, `MarkStealthTest`, `ShowTargetedThingHealth`, `TonyTestScripts`, `ForceKHGGatesOpen`, `DummyQuestForHeroLevels`, `DummyQuestForScarletRoseStatue` | Cutscene camera/input lock; scripted movement and map/camera warp; debug commands may mutate stats, gates, or population; hooked frescoes can reveal global story branch. | Keep test/debug entries unavailable in production. Trace every cutscene request to an owning Hero/host and a replicated completion marker; inspect each `Hook_*` trigger and world write before treating it as cosmetic. |

### Other registrations and completeness

`FinalAlbion.qst` also registers the core/global names `Q_SunnyvaleMaster`,
`ChapterAndSceneManager`, `PersonalScriptMain`, `PersonalScript_GlobalThings`,
`NPCDeath`, `HeroBoasts`, `Gameflow`, `GameflowAssistance`, `Hook_BoastingCrowd`,
`CreatureGenerators`, and `LadyGreyWifeManager`. These are system/master/background
logic, not player quest phases; classify `NeedsResearch` and audit separately
before any blanket activation suppression. `GlobalQuests.qst` specifically
contains background scripts for death, expressions, item reward, chest opening,
and debug travel; these can mutate the same world/hero domains as story quests.

The 60 `V_*` vignette/sidequest registrations are included in the runtime
catalog rather than receiving a blanket policy. `AddTestQuest` metadata is not
treated as production campaign registration: test-only names are blocked when
they also appear in `AddQuest`, while executable-only or future modded names
resolve to the fail-closed unknown policy until reviewed.

## Demon doors and vignettes requiring explicit treatment

`V_DemonDoors` is registered in `FinalAlbion.qst` and must not be mistaken for
one global quest phase: each door has a local predicate, speech/interaction,
world-state condition, unlock/open effect, and often an item reward. Likely
policy: `InitiatorOnly` for satisfying/displaying the riddle and owner-only
reward; `SharedSimulation` for a committed physical door opening only if that
door is intended to be shared; otherwise `NeedsResearch`. Trace each door's
script/region trigger, condition read, exact door/object write, quest key/item
recipient, and save persistence. Test a late join after each door state change
and verify it neither reopens nor duplicates a reward.

Other `V_*` entries include activated variants, recurring population scripts,
minigames, morality/choice branches, and quests with Hero-local item/XP rewards.
Until a distinct catalog exists, keep them `NeedsResearch`; do not infer
`PrivateParallel` merely because a quest card appears on one Hero.

## Exact tracing sequence to turn hypotheses into policy

1. Reconcile names across `FinalAlbion.qst`, `GlobalQuests.qst`, `AddTestQuest`,
   and the executable's compiled registry. Resolve allocator identity from
   function/vtable evidence rather than potentially reused RTTI/PDB names.
2. For every prioritized quest, follow compiled `Init`, `Main`, spawned
   functions, persistence, callbacks, and every branch to terminal success,
   failure, cancellation, and map unload. Build a phase graph with objective
   index and durable flags.
3. At each phase, log reads/writes for native quest state, world entities,
   doors/barriers, map/scene/camera, generated actors, script-owned target or
   action, cutscene/input locks, and Hero-owned stats/inventory/rewards.
4. Observe command execution and native callback ownership: does a quest command
   act on the local Hero, a named target, all peers, or only the authority's
   current native world? Track target UID generation and command lifetime.
5. Exercise two instances with distinct saves and simultaneous choices. Record
   both Heroes' quest manager state, world state, actors, and Hero-local deltas.
   Repeat with peer joining mid-phase, owner disconnect, map transition, death,
   and save/load. Snapshot replication alone is not proof of correct semantics.
6. Promote an entry only when duplicate activation, duplicate generated actors,
   reward attribution, transition replay, stale target, and late-join behavior
   are all accounted for. Store trace IDs/logs alongside the row when available.

### Priority order

1. `Q_GuildTraining*` and `Q_NewOakValeIntro*`: tutorial target isolation and
   childhood/age initialization establish the join baseline.
2. `Q_OrchardFarmRaid*`, `Q_TraderEscort`/`QR_EscortTrader*`,
   `Q_TraderConflict*`: choices plus shared moving/spawned actors.
3. `Q_Arena` and `Q_ArenaHoldingScript`: generated waves, scripted target and
   payout idempotency.
4. `Q_Prison*`, `Q_PrisonWardenGame`, `Q_SecretPassage`,
   `Q_TentacleKrakenBossFight`: scripted prison state and map transition.
5. Bargate gate path, `Q_EndGame*`, `Q_DragonBossFight`, `Q_HeroSouls*`,
   `Q_FireHeart`, and ending choice/cutscene branches.
6. `V_DemonDoors`, then other V_ sidequests with doors, choices, escorts, or
   Hero-local rewards.

## Runtime safety rules

`Shared` lifecycle edges use the ordered campaign revision and may be proposed
by either player. `LocalOnly` tutorial/minigame edges are consumed locally and
never enter the campaign stream. `HostOnly` is reserved for true global
composition managers, not ordinary quests or map-owner helpers. `Blocked` and
unknown names are logged but never published.

The host snapshot remains the late-join truth: it installs the committed quest
machine before live mutations are accepted, so one-shot phase entry is not
replayed as history. `World::MapOwner`, `Decision::InitiatorShared`, and
`Transition::Shared` deliberately remain separate from lifecycle ordering.
They require their validated native command boundaries; a policy row must not
turn a generic creature spawn, conversation, reward, or cutscene into a guessed
network operation.
