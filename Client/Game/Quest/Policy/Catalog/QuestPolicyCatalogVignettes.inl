FABLE_QUEST_POLICY("V_AmbushScam", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Scam branch affects a shared encounter; evil-ending code also changes Hero morality.
FABLE_QUEST_POLICY("V_AmbushScamEvil", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Evil branch commits encounter outcome while morality and payout remain Hero-owned.
FABLE_QUEST_POLICY("V_AmbushScamGood", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Good branch resolves shared encounter; any Hero reward stays local.
FABLE_QUEST_POLICY("V_ArcheryCompetition", LocalOnly, None, None, None, HeroLocal, Inferred) // Score-driven competition and prize are per-Hero.
FABLE_QUEST_POLICY("V_ArcheryCompetition_Activate", LocalOnly, None, None, None, HeroLocal, Inferred) // Competition activation is a Hero-local minigame entry.
FABLE_QUEST_POLICY("V_AssassinAttacks", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Attacking assassins and their defeat outcome affect shared actors; trace target ownership.
FABLE_QUEST_POLICY("V_AssassinAttacks_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation can spawn the shared assassin encounter; verify duplicate-spawn guards.
FABLE_QUEST_POLICY("V_BanditCampPath", Shared, MapOwner, None, None, None, NeedsTrace) // Path script likely controls shared bandit actors and route state.
FABLE_QUEST_POLICY("V_BanditToll", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Paying or resisting changes the shared bandit encounter; toll/reward recipient needs tracing.
FABLE_QUEST_POLICY("V_BeardyBaldy", LocalOnly, None, None, None, HeroLocal, Inferred) // Barber interaction changes the visiting Hero's appearance or stats only.
FABLE_QUEST_POLICY("V_BeggarAndChild", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Choice involves shared villagers; alignment or reward belongs to the initiating Hero.
FABLE_QUEST_POLICY("V_BodyGuard", Shared, MapOwner, None, None, None, NeedsTrace) // Guard is a shared actor whose following/combat lifecycle needs owner tracing.
FABLE_QUEST_POLICY("V_BookCollecting", LocalOnly, None, None, None, HeroLocal, Inferred) // Book collection and completion rewards are Hero-owned inventory/progression.
FABLE_QUEST_POLICY("V_Bordello", LocalOnly, None, None, None, HeroLocal, Inferred) // Brothel interaction records Hero-specific state; no shared world consequence is established.
FABLE_QUEST_POLICY("V_ChapelOfEvil", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Religious donation can alter shared temple state; donation choice and Hero alignment need tracing.
FABLE_QUEST_POLICY("V_ChapelOfEvil_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation fronts a potentially shared religious-state interaction.
FABLE_QUEST_POLICY("V_ChickenKicking", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Minigame touches physical chicken actors; keep actor motion shared and prize local.
FABLE_QUEST_POLICY("V_DemonDoors", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Door opening is shared map state; riddle choice and any gift are Hero-owned.
FABLE_QUEST_POLICY("V_ExposeMayor", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Mayor outcome changes shared NPC/world state; Hero makes the branch choice.
FABLE_QUEST_POLICY("V_ExposeMayorRhodri", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Rhodri branch resolves shared mayor-side actors while Hero consequences remain local.
FABLE_QUEST_POLICY("V_Fisherman", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Fisherman quest involves a persistent world villager; trace whether the encounter is private.
FABLE_QUEST_POLICY("V_FishingCompetition", LocalOnly, None, None, None, HeroLocal, Inferred) // Competition score and reward are per-Hero.
FABLE_QUEST_POLICY("V_FisticuffsClub", LocalOnly, None, None, None, HeroLocal, Inferred) // Fisticuffs rounds are a Hero-local competition with local settlement.
FABLE_QUEST_POLICY("V_FisticuffsClubOnlyAtNight", LocalOnly, None, None, None, HeroLocal, Inferred) // Night eligibility gates the same Hero-local competition.
FABLE_QUEST_POLICY("V_GhostGrannyNecklace", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Granny/son actors and necklace recovery touch world entities; reward is Hero-owned.
FABLE_QUEST_POLICY("V_GuildMaster", LocalOnly, None, None, None, HeroLocal, Inferred) // Advice/tutorial progress is Hero-specific absent evidence of a world write.
FABLE_QUEST_POLICY("V_HauntedHouse", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // House/ghost state is shared property; any purchase or reward must settle to its Hero.
FABLE_QUEST_POLICY("V_HelpTips", LocalOnly, None, None, None, None, Inferred) // Tip prompts are presentation-only Hero-local guidance.
FABLE_QUEST_POLICY("V_HeroDolls", LocalOnly, None, None, None, HeroLocal, Inferred) // Doll collection and inventory rewards belong to one Hero.
FABLE_QUEST_POLICY("V_HeroDuel", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Duel targets are shared combat actors; prize/stat updates are Hero-owned.
FABLE_QUEST_POLICY("V_HiddenBooty", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Hidden chest/open state is shared while its contents belong to the looting Hero.
FABLE_QUEST_POLICY("V_HiddenBooty_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation may reveal shared treasure state; verify it does not award items itself.
FABLE_QUEST_POLICY("V_IntroductionToTrophies", LocalOnly, None, None, None, HeroLocal, Inferred) // Trophy tutorial and resulting Hero progression are personal.
FABLE_QUEST_POLICY("V_KnotholeGladeGates", Shared, MapOwner, None, None, None, Inferred) // Gate operation changes a physical shared map barrier.
FABLE_QUEST_POLICY("V_LostTrader", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Trader is a shared villager/actor; any quest reward remains Hero-owned.
FABLE_QUEST_POLICY("V_MayorsInvitation", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Invitation quest uses the shared mayor and may change town access; verify exact writes.
FABLE_QUEST_POLICY("V_MazeResearch", LocalOnly, None, None, None, HeroLocal, NeedsTrace) // Research clues appear Hero-specific unless compiled code writes a shared unlock.
FABLE_QUEST_POLICY("V_MurderTwistFinal", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Murder resolution changes shared actor/story state; moral/stat consequences are Hero-local.
FABLE_QUEST_POLICY("V_Oracle", LocalOnly, None, None, None, HeroLocal, NeedsTrace) // Oracle interaction appears informational; verify any shared unlock or world branch.
FABLE_QUEST_POLICY("V_ParanoidWhispers", LocalOnly, None, None, None, HeroLocal, NeedsTrace) // Whispers likely track Hero-specific perception; confirm no shared actor effects.
FABLE_QUEST_POLICY("V_PicnicAreaAfterWaspBoss", Shared, MapOwner, None, None, None, NeedsTrace) // Post-boss picnic involves shared villagers and area state.
FABLE_QUEST_POLICY("V_RandomPopulationSim", Shared, MapOwner, None, None, None, Inferred) // Population simulation mutates map-owned villager/world state and needs one authority.
FABLE_QUEST_POLICY("V_RockTrollFirstEncounter", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Troll encounter and spawned combat actor are shared; Hero rewards are private.
FABLE_QUEST_POLICY("V_RockTrollFirstEncounter_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation may spawn or reveal the shared troll actor.
FABLE_QUEST_POLICY("V_SickChild", Shared, MapOwner, InitiatorShared, None, HeroLocal, Inferred) // Child/villager outcome is shared; cure choice and reward resolve for the initiating Hero.
FABLE_QUEST_POLICY("V_SickChildBarrowFields", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Regional sick-child variant involves a shared villager and Hero-owned resolution.
FABLE_QUEST_POLICY("V_SickChild_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation begins a shared child encounter; trace whether it changes the villager.
FABLE_QUEST_POLICY("V_SingingStones", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Stone activation is physical shared map state; any prize is Hero-local.
FABLE_QUEST_POLICY("V_SingingStones_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation may commit shared stone state but should not settle a Hero reward.
FABLE_QUEST_POLICY("V_StatueMaster", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Statue/chest state and associated actors are world-owned; reward belongs to the Hero.
FABLE_QUEST_POLICY("V_SwordInTheStone", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Stone/object state is shared; extraction and any weapon/stat reward are Hero-local.
FABLE_QUEST_POLICY("V_TalentlessBard", LocalOnly, None, None, None, HeroLocal, Inferred) // Decompiled song handlers award morality/renown to the Hero, not shared world state.
FABLE_QUEST_POLICY("V_TempleOfLight", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Donation changes shared religious state; amount/choice and alignment remain Hero-owned.
FABLE_QUEST_POLICY("V_TempleOfLight_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Activation gates the shared temple donation-state interaction.
FABLE_QUEST_POLICY("V_TourGuide", Shared, MapOwner, None, None, None, NeedsTrace) // Guide/waypoint route controls a shared villager and may drive area transitions.
FABLE_QUEST_POLICY("V_TourGuide_Activate", Shared, MapOwner, None, None, None, NeedsTrace) // Tour activation starts shared guide movement; verify ownership of waypoint commands.
FABLE_QUEST_POLICY("V_TravellingBeggar", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Traveling beggar is a shared population actor; any donation/reward is Hero-specific.
FABLE_QUEST_POLICY("V_TravellingBully", Shared, MapOwner, InitiatorShared, None, HeroLocal, NeedsTrace) // Bully encounter affects shared actors; moral branch and reward belong to the Hero.
FABLE_QUEST_POLICY("V_TravellingHeroes", Shared, MapOwner, None, None, None, NeedsTrace) // Traveling hero group is persistent shared population/actor state.
FABLE_QUEST_POLICY("V_TrophyDealer", Shared, MapOwner, None, None, HeroLocal, NeedsTrace) // Dealer is a world villager but trophy trade and payout must be Hero-owned.
