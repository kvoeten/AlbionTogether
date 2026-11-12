FABLE_QUEST_POLICY("Q_SunnyvaleMaster", HostOnly, MapOwner, None, None, None, NeedsTrace) // Global master script owns the Sunnyvale lifecycle; guests consume its committed state.
FABLE_QUEST_POLICY("Q_HangingTreeGood", Shared, MapOwner, InitiatorShared, Shared, HeroLocal, Inferred) // Decomp exposes prisoner/boss/ambush actors and cutscene; choice is initiator-owned.
FABLE_QUEST_POLICY("Q_HangingTreeEvil", Shared, MapOwner, InitiatorShared, Shared, HeroLocal, Inferred) // Decomp exposes enemy/party spawns and cutscene; choice is initiator-owned.
FABLE_QUEST_POLICY("Q_HeroSouls", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Soul collection coordinates shared world encounters; settlement belongs to the Hero.
FABLE_QUEST_POLICY("Q_HeroSoulsThunder", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp contains Thunder encounter phases; combat shared, settlement Hero-local.
FABLE_QUEST_POLICY("Q_HeroSoulsArena", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp shows generated arena enemy and round monitoring; reward remains Hero-local.
FABLE_QUEST_POLICY("Q_HeroSoulsPermanentArena", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Arena variant likely shares combat space; verify wave/reward distinction.
FABLE_QUEST_POLICY("Q_HeroSoulsBriar", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp shows Briar encounter and magic barrier; keep its actors map-owned.
FABLE_QUEST_POLICY("Q_HeroSoulsBriarMagicBarrier", Shared, MapOwner, None, None, None, Inferred) // Decomp has a dedicated barrier phase; shared door state needs map ownership.
FABLE_QUEST_POLICY("Q_HeroSoulsMother", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp exposes summoner and screamer actors; reward must not cross Hero saves.
FABLE_QUEST_POLICY("Q_HeroSoulsGuildmaster", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp includes guard and acolyte encounter scripts; any soul reward is Hero-local.
FABLE_QUEST_POLICY("Q_HeroSoulsNostro", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp exposes Nostro encounter/hit monitoring; reward belongs to the completing Hero.
FABLE_QUEST_POLICY("Q_HerosOldHouse", Shared, None, None, None, HeroLocal, NeedsTrace) // House quest state may be personal, but ordinary progression remains shared pending trace.
FABLE_QUEST_POLICY("Q_HobbeCave", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp identifies cave/torrent encounter and termination logic; reward stays local.
FABLE_QUEST_POLICY("Q_HobbeCaveBarrier", Shared, MapOwner, None, None, None, NeedsTrace) // Cave barrier helper likely mutates a shared gate; trace exact persistence.
FABLE_QUEST_POLICY("Q_HobbeCaveGranny", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Granny cave branch involves shared NPC state; trace choice and reward recipient.
FABLE_QUEST_POLICY("Q_HobbeToothContest", Shared, None, None, None, HeroLocal, NeedsTrace) // Contest progress is quest state; verify any NPC/world effects separately.
FABLE_QUEST_POLICY("Q_MinionCamp", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp exposes staged minion-wave control; one map owner prevents duplicate spawns.
FABLE_QUEST_POLICY("Q_MinionClifftopChase", Shared, MapOwner, None, Shared, HeroLocal, Inferred) // Chase actors/barrier are shared and the pursuit changes scene state.
FABLE_QUEST_POLICY("Q_MinionClifftopChase_Barriers", Shared, MapOwner, None, None, None, Inferred) // Decomp exposes a graveyard magic-barrier phase; serialize its map mutation.
FABLE_QUEST_POLICY("Q_NewOakValeIntro", LocalOnly, MapOwner, None, Shared, HeroLocal, Inferred) // Child attack/cutscene stays private; reconcile shared world aftermath separately.
FABLE_QUEST_POLICY("Q_NewOakValeIntro_PreAttack", LocalOnly, MapOwner, None, Shared, None, NeedsTrace) // Pre-attack tutorial target state stays private; reconcile shared transition separately.
FABLE_QUEST_POLICY("Q_OakValeBanditRaid", Shared, MapOwner, None, None, HeroLocal, Inferred) // Compiled raid quest controls shared bandit encounter; bounty payout must be Hero-local.
FABLE_QUEST_POLICY("Q_OpeningGraveyardSecretPassage", Shared, MapOwner, None, None, None, NeedsTrace) // Passage opening changes shared gate/map access.
FABLE_QUEST_POLICY("Q_OrchardFarmRaid", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp exposes crate-team raid manager; share the encounter, not Hero rewards.
FABLE_QUEST_POLICY("Q_OrchardFarmRaidEvil", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Branch outcome is chosen by initiator; guards/raid are shared and payout local.
FABLE_QUEST_POLICY("Q_OrchardFarmRaidGood", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Branch outcome is chosen by initiator; farm defense is shared and payout local.
FABLE_QUEST_POLICY("Q_OrchardFarm_Barricade", Shared, MapOwner, None, None, None, NeedsTrace) // Raid barricade helper changes shared traversal state.
FABLE_QUEST_POLICY("Q_PrisonEscapeRescueMother", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Rescue and prison exit affect shared NPCs and chapter/map state.
FABLE_QUEST_POLICY("Q_PrisonEscapeFromCell", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Cell escape changes shared guards/access and leads into a scene transition.
FABLE_QUEST_POLICY("Q_PrisonRace", Shared, None, None, None, HeroLocal, NeedsTrace) // Race quest state is shared; verify participant scoring and prison consequences.
FABLE_QUEST_POLICY("Q_PrisonWardenGame", Shared, None, None, None, HeroLocal, NeedsTrace) // Warden-game quest state is shared; keep participant reward Hero-local.
FABLE_QUEST_POLICY("Q_RansomVictim", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Victim rescue branch depends on an initiating choice; NPC outcome is public.
FABLE_QUEST_POLICY("Q_RansomVictimChiefsHouse", Shared, MapOwner, None, None, None, NeedsTrace) // Chief-house helper gates shared NPC/house progression.
FABLE_QUEST_POLICY("Q_RansomVictimStarted", Shared, MapOwner, None, None, None, NeedsTrace) // Started-state helper likely gates shared victim/hostage actors.
FABLE_QUEST_POLICY("Q_RansomVictimStartedNature", Shared, MapOwner, None, None, None, NeedsTrace) // Nature-branch started helper gates shared victim state.
FABLE_QUEST_POLICY("Q_RansomVictimRevenge", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Branch-specific victim outcome; decision is initiator-owned and reward local.
FABLE_QUEST_POLICY("Q_RansomVictimNaturesRevenge", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Branch-specific victim outcome; decision is initiator-owned and reward local.
FABLE_QUEST_POLICY("Q_SecretPassage", Shared, MapOwner, None, None, None, NeedsTrace) // Secret passage changes shared door/map access.
FABLE_QUEST_POLICY("Q_SummoningTheShip", Shared, MapOwner, None, Shared, HeroLocal, NeedsTrace) // Ship summon is a shared chapter/map transition; any completion reward stays local.
FABLE_QUEST_POLICY("Q_TentacleKrakenBossFight", Shared, MapOwner, None, Shared, HeroLocal, Inferred) // Compiled prison/Kraken quest drives shared boss encounter and destination transition.
FABLE_QUEST_POLICY("Q_TraderConflictEvil", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Moral branch changes public trader/NPC state; initiator chooses, Hero earns reward.
FABLE_QUEST_POLICY("Q_TraderConflictGood", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Moral branch changes public trader/NPC state; initiator chooses, Hero earns reward.
FABLE_QUEST_POLICY("Q_TraderConflictGood_Extras", Shared, MapOwner, None, None, None, NeedsTrace) // Extra good-branch stage affects shared encounter support; trace exact side effects.
FABLE_QUEST_POLICY("Q_TraderEscort", Shared, MapOwner, None, None, HeroLocal, Inferred) // Decomp shows scripted trader/region watching; escort actor is shared, reward local.
FABLE_QUEST_POLICY("Q_TraderEscortForceField", Shared, MapOwner, None, None, None, NeedsTrace) // Escort force-field helper changes shared traversal state.
FABLE_QUEST_POLICY("Q_TraderPath", Shared, MapOwner, None, None, None, NeedsTrace) // Trader route helper controls a shared NPC path.
FABLE_QUEST_POLICY("Q_UndeadRising", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Rising undead implies a shared generated encounter; validate spawn/cleanup ownership.
FABLE_QUEST_POLICY("Q_WaspBoss", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Boss encounter should have one map owner; award completion only to the Hero.
FABLE_QUEST_POLICY("Q_WhiteBalverineKnotholeGlade", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Village boss quest affects public combat/population; reward remains Hero-local.
FABLE_QUEST_POLICY("Q_WhiteBalverineWW", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // White Balverine encounter uses shared actors; verify world aftermath.
FABLE_QUEST_POLICY("Q_WizardBattle", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Battle is shared map combat; keep victory rewards on the completing Hero.
FABLE_QUEST_POLICY("QS_GuardianSisterInfo", LocalOnly, None, None, None, None, NeedsTrace) // Clue delivery is likely Hero-private; trace whether it unlocks shared progression.
FABLE_QUEST_POLICY("QS_GuardianSisterInfo2_SisterInBanditCamp", LocalOnly, None, None, None, None, NeedsTrace) // Sister clue is likely private; shared camp effects require separate authorization.
FABLE_QUEST_POLICY("QS_GuardianTrophyDealerInfo", LocalOnly, None, None, None, None, NeedsTrace) // Trophy-dealer information appears Hero-bound; verify public unlock side effects.
FABLE_QUEST_POLICY("QS_MeetSister", Shared, MapOwner, None, Shared, None, NeedsTrace) // Meeting sister affects shared NPC/story state and may switch scene.
FABLE_QUEST_POLICY("QS_ScytheInfo", LocalOnly, None, None, None, None, NeedsTrace) // Scythe clue delivery is likely Hero-private; trace downstream public gates.
FABLE_QUEST_POLICY("QR_EscortTrader", Shared, MapOwner, None, None, HeroLocal, Inferred) // Compiled escort routines watch region loads; route/escort state is shared.
FABLE_QUEST_POLICY("QR_EscortTrader_Manager", Shared, MapOwner, None, None, None, Inferred) // Compiled manager watches escort lifecycle; single map owner avoids duplicate control.
