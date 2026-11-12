FABLE_QUEST_POLICY("ChapterAndSceneManager", HostOnly, MapOwner, None, Shared, None, NeedsTrace) // Central chapter and scene transitions must have one world owner.
FABLE_QUEST_POLICY("CreatureGenerators", Shared, MapOwner, None, None, None, NeedsTrace) // The current map owner may advance generators and merge their actors into host world state.
FABLE_QUEST_POLICY("CS_OakValeRevisited", LocalOnly, MapOwner, None, Shared, None, NeedsTrace) // Cutscene playback is local, but scene transition may affect everyone.
FABLE_QUEST_POLICY("CS_PlayCutscene", LocalOnly, None, None, None, None, Inferred) // Generic cutscene driver should not replay for remote Heroes.
FABLE_QUEST_POLICY("DummyQuestForHeroLevels", Blocked, None, None, None, HeroLocal, Inferred) // Test entries directly alter the local Hero's level.
FABLE_QUEST_POLICY("DummyQuestForScarletRoseStatue", Blocked, MapOwner, None, None, None, NeedsTrace) // Debug statue mutator has unverified world side effects.
FABLE_QUEST_POLICY("Expression_Dig", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Dig input and treasure result are Hero-scoped; ground treasure needs owner mediation.
FABLE_QUEST_POLICY("Expression_Fish", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fishing input/progress is private; catch/world item ownership needs trace.
FABLE_QUEST_POLICY("Expression_Follow", LocalOnly, None, None, None, None, Inferred) // Follow expression is an individual interaction sequence.
FABLE_QUEST_POLICY("Expression_Picklock", LocalOnly, MapOwner, None, None, None, NeedsTrace) // Lock attempt is Hero-local, while a successful door opening is shared.
FABLE_QUEST_POLICY("Expression_Pickpocket", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Pickpocket attempt is private; NPC inventory and reward need owner mediation.
FABLE_QUEST_POLICY("Expression_Steal", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Stealing is Hero-directed but may remove a shared world object.
FABLE_QUEST_POLICY("Expression_Wait", LocalOnly, None, None, None, None, Inferred) // Wait command controls only the initiating Hero.
FABLE_QUEST_POLICY("ForceKHGGatesOpen", Blocked, MapOwner, None, None, None, NeedsTrace) // Gate-forcing debug hook writes shared traversal state.
FABLE_QUEST_POLICY("Gameflow", HostOnly, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Main progression drives shared chapter state plus save-owned rewards.
FABLE_QUEST_POLICY("GameflowAssistance", LocalOnly, None, None, None, None, NeedsTrace) // Assistance prompts are presentation and input state, not shared progression.
FABLE_QUEST_POLICY("Global_DebugCycleThroughSpeech", Blocked, None, None, None, None, Inferred) // Debug speech cycling has no safe multiplayer behavior.
FABLE_QUEST_POLICY("Global_GiveHeroItemsFromRewardChest", LocalOnly, None, None, None, HeroLocal, Inferred) // Reward-chest grant must affect only its owning Hero.
FABLE_QUEST_POLICY("Global_OpenChest", Shared, MapOwner, None, None, SharedWorld, NeedsTrace) // Either Hero may open a chest; map owner commits shared chest/loot state.
FABLE_QUEST_POLICY("Global_TeleportToHeroGuild", LocalOnly, None, None, Shared, None, NeedsTrace) // Teleport targets one Hero, though resulting map transition needs coordination.
FABLE_QUEST_POLICY("Global_ToggleTimeDisplay", LocalOnly, None, None, None, None, Inferred) // Time display toggle is local presentation state.
FABLE_QUEST_POLICY("Global_WatchForHeroDeath", Blocked, MapOwner, None, None, None, NeedsTrace) // Global death watcher can trigger world resets; exact ownership is unknown.
FABLE_QUEST_POLICY("HeroBoasts", LocalOnly, MapOwner, None, None, None, NeedsTrace) // Boast choice is Hero-driven; crowd/NPC reactions are world-owned.
FABLE_QUEST_POLICY("Hook_BalverinesInKHGPreWhiteBalv", Shared, MapOwner, None, None, None, NeedsTrace) // The current map owner may advance this shared Balverine population hook.
FABLE_QUEST_POLICY("Hook_BoastingCrowd", Shared, MapOwner, None, None, None, NeedsTrace) // Either Hero may provoke the crowd; map owner controls NPC responses.
FABLE_QUEST_POLICY("Hook_BowerstoneTeleportTutorial", LocalOnly, None, None, None, None, Inferred) // Teleport tutorial prompts and teaches only its initiating Hero.
FABLE_QUEST_POLICY("Hook_Fresco_01_EvilEpilogue", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_02_GoodEpilogue", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_03_JacksObsession", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_04_Kraken", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_05_MothersStory", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_06_Prison", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_07_OakValeRaid", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_08_SistersStory", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_09_TimePassing", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_10_UneasyAlliance", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_11_Wedding", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_12_KilledDragon", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("Hook_Fresco_13_KilledScorpion", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Fresco discovery is Hero progress; any world presentation is owner-gated.
FABLE_QUEST_POLICY("LadyGreyWifeManager", Shared, MapOwner, InitiatorShared, Shared, SharedWorld, NeedsTrace) // Marriage/NPC outcome changes shared population and requires a single choice owner.
FABLE_QUEST_POLICY("MarkStealthTest", Blocked, None, None, None, None, Inferred) // Test-only stealth probe has no player-facing replication contract.
FABLE_QUEST_POLICY("NPCDeath", Shared, MapOwner, None, None, SharedWorld, NeedsTrace) // NPC death cleanup may originate on a guest-owned map and must merge into host world state.
FABLE_QUEST_POLICY("OracleMinigameTest", LocalOnly, None, None, None, HeroLocal, Inferred) // Minigame testbed is an individual interaction with local results.
FABLE_QUEST_POLICY("PersonalScript_GlobalThings", HostOnly, MapOwner, None, Shared, None, NeedsTrace) // Global script collection may set world flags and transitions; trace before enabling.
FABLE_QUEST_POLICY("PersonalScriptMain", HostOnly, MapOwner, None, Shared, None, NeedsTrace) // Main personal scripts mix Hero state with world progression; audit each branch.
FABLE_QUEST_POLICY("Q__OakValeIntro_PostAttack", LocalOnly, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Post-attack objective/target sequence is Hero-private; massacre aftermath and age transition are shared.
FABLE_QUEST_POLICY("Q_AmbushTraders", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Either Hero may advance the encounter; map owner controls spawns and combat.
FABLE_QUEST_POLICY("Q_AmbushTraders_ActivatePhase1", Shared, MapOwner, None, None, None, NeedsTrace) // Shared encounter phase activation controls map-owned ambush actors.
FABLE_QUEST_POLICY("Q_AmbushTraders_ActivatePhase2", Shared, MapOwner, None, None, None, NeedsTrace) // Shared encounter phase activation controls map-owned ambush actors.
FABLE_QUEST_POLICY("Q_AmbushTraders_ActivatePhase3", Shared, MapOwner, None, None, None, NeedsTrace) // Shared encounter phase activation controls map-owned ambush actors.
FABLE_QUEST_POLICY("Q_Arena", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Either challenger may advance shared rounds; map owner controls arena state.
FABLE_QUEST_POLICY("Q_ArenaHoldingScript", Shared, MapOwner, None, None, None, NeedsTrace) // Either Hero may advance arena support; map owner controls shared actors.
FABLE_QUEST_POLICY("Q_AwakeningTheOracle", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Either Hero may advance Oracle access; map owner commits world transition.
FABLE_QUEST_POLICY("Q_AwakeningTheOracleMinigame", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Oracle test is a private sequence; minigame effects need separate world ownership.
FABLE_QUEST_POLICY("Q_BanditCamp", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Either Hero may advance infiltration; map owner controls guards and shared props.
FABLE_QUEST_POLICY("Q_BanditCamp_Barriers", Shared, MapOwner, None, None, None, NeedsTrace) // Shared quest progression controls map-owner camp barriers.
FABLE_QUEST_POLICY("Q_BanditCampBossBattle", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Either Hero may advance the boss encounter; reward remains Hero-local.
FABLE_QUEST_POLICY("Q_BanditCampHoldingScript", Shared, MapOwner, None, None, None, NeedsTrace) // Either Hero may advance camp support; map owner controls shared population.
FABLE_QUEST_POLICY("Q_BountyHunt", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Either Hero may advance hunt progress; target death is map-owner state.
FABLE_QUEST_POLICY("Q_BowerstoneTownLifeIntro", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Town-life tutorial steps are private; NPC/world consequences need owner gating.
FABLE_QUEST_POLICY("Q_BreakSiege", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Either Hero may advance the siege; map owner governs barriers and combat.
FABLE_QUEST_POLICY("Q_BreakSiege_Blockage", Shared, MapOwner, None, None, None, NeedsTrace) // Shared siege progression controls a map-owner obstruction.
FABLE_QUEST_POLICY("Q_CinemaTest", Blocked, None, None, None, None, Inferred) // Cinema test is a debug quest and should not run in production replication.
FABLE_QUEST_POLICY("Q_DragonBossFight", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Either Hero may advance the shared fight; map owner commits exit transition.
FABLE_QUEST_POLICY("Q_EndGame", Shared, MapOwner, InitiatorShared, Shared, HeroLocal, NeedsTrace) // Either Hero may advance endgame; initiating Hero chooses and receives reward.
FABLE_QUEST_POLICY("Q_EndGame_Barriers", Shared, MapOwner, None, None, None, NeedsTrace) // Shared endgame progression controls map-owner barriers.
FABLE_QUEST_POLICY("Q_EndGameBossBattle", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Either Hero may advance Jack's battle; map owner commits aftermath.
FABLE_QUEST_POLICY("Q_EndGameBossBattleScenery", Shared, MapOwner, None, None, None, NeedsTrace) // Shared boss progression controls map-owner scenery actors.
FABLE_QUEST_POLICY("Q_EndGameFocalSites", Shared, MapOwner, None, None, None, NeedsTrace) // Either Hero may advance focal-site objectives; world effects are map-owned.
FABLE_QUEST_POLICY("Q_FireHeart", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Either Hero may advance Fireheart access; claimant receives Hero-local reward.
FABLE_QUEST_POLICY("Q_GuildTraining", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Arrival lessons and personal progression are private; training props remain map-owner controlled.
FABLE_QUEST_POLICY("Q_GuildTrainingDeparture", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Departure prompt/target sequence is private; avoid replaying it for peers.
FABLE_QUEST_POLICY("Q_GuildTrainingMelee", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Melee practice targets one Hero; shared training dummies need owner control.
FABLE_QUEST_POLICY("Q_GuildTrainingPreMelee", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Pre-melee instruction and objective sequence is Hero-local.
FABLE_QUEST_POLICY("Q_GuildTrainingSkill", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Skill practice is a private target/objective sequence.
FABLE_QUEST_POLICY("Q_GuildTrainingWill", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Will tutorial targets one Hero and teaches save-owned progression.
FABLE_QUEST_POLICY("Q_GuildTrainingWoodsDeparture", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Woods departure objective is private; route/world setup is map-owned.
FABLE_QUEST_POLICY("Q_GuildTrainingWoodsMelee", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Woods melee practice is a private target sequence.
FABLE_QUEST_POLICY("Q_GuildTrainingWoodsWill", LocalOnly, MapOwner, None, None, HeroLocal, NeedsTrace) // Woods will lesson is private; shared effects remain map-owner governed.
FABLE_QUEST_POLICY("ShowTargetedThingHealth", LocalOnly, None, None, None, None, Inferred) // Target-health display is local HUD presentation.
FABLE_QUEST_POLICY("TonyTestScripts", Blocked, None, None, None, None, Inferred) // Test script/card inventory behavior has no multiplayer contract.
