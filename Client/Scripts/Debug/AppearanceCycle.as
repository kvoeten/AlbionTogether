const uint NumberRowOne = 0x31;

array<string> CreatureDefinitions = {
    "CREATURE_BS_GUARD",
    "CREATURE_BS_VILLAGER_FEMALE",
    "CREATURE_HOBBE_GRUNT",
    "CREATURE_BS_GUARD_CROSSBOW",
    "CREATURE_PRISON_GUARD",
    "CREATURE_KN_GUARD",
    "CREATURE_BS_VILLAGER_MALE",
    "CREATURE_TRADER_01",
    "CREATURE_BANDIT_GRUNT",
    "CREATURE_RIVAL_HERO_WHISPER",
    "CREATURE_RIVAL_HERO_THUNDER",
    "CREATURE_BALVERINE_EASY"
};

array<Entity@> RetainedCreatures;
Entity@ Hero;
Entity@ Puppet;
CreatureControl@ PuppetControl;
uint CreatureIndex = 0;
Vector3 PuppetStartPosition;
float PuppetStartFacing = 0.0f;
Vector3 PuppetPhysicsStartPosition;
uint PuppetAnimationStartHash = 0;
float PuppetActiveSeconds = 0.0f;
float LocomotionSampleSeconds = 0.0f;
bool PuppetTranslated = false;
bool PuppetRotated = false;
bool PuppetPhysicsAdvanced = false;
bool PuppetAnimationAdvanced = false;
bool PuppetWorldPositionMirrored = false;
bool NativeLocomotionStackReported = false;
bool LocomotionReported = false;
bool CurrentPuppetUsesPlayerFrameInput = false;
bool PlayerFrameInputMovementReported = false;
uint SchedulerRepeatingTask = 0;
uint SchedulerTickCount = 0;
bool SchedulerOneShotObserved = false;
bool SchedulerProbeReported = false;
uint WorldReadySubscription = 0;

void ReportSchedulerProbeIfReady()
{
    if (!SchedulerProbeReported && SchedulerOneShotObserved && SchedulerTickCount >= 2)
    {
        SchedulerProbeReported = true;
        Debug::Event("SchedulerProbePassed", "one-shot, repeating, and in-callback cancellation completed");
    }
}

void OnSchedulerOneShot()
{
    SchedulerOneShotObserved = true;
    ReportSchedulerProbeIfReady();
}

void OnSchedulerRepeat()
{
    SchedulerTickCount++;
    if (SchedulerTickCount >= 2)
    {
        Scheduler::Cancel(SchedulerRepeatingTask);
    }
    ReportSchedulerProbeIfReady();
}

void OnFrameworkEvent(const string &in eventName, const string &in detail)
{
    if (eventName == "WorldReady")
    {
        Events::Unsubscribe(WorldReadySubscription);
        Debug::Event("EventBusProbePassed", eventName + " detail=" + detail);
    }
}

void OnStart()
{
    Debug::Log("AlbionTogether general gameplay script started");
    if (!Capabilities::IsAvailable("World.CreateCreature") ||
        !Capabilities::IsAvailable("Creature.Navigation") ||
        !Capabilities::IsAvailable("Creature.Locomotion.State.Read") ||
        !Capabilities::IsVerified("Creature.CombatHealth.Read") ||
        !Capabilities::IsAvailable("Entity.Metadata.Read") ||
        !Capabilities::IsVerified("Entity.Interaction.State") ||
        !Capabilities::IsVerified("Player.Hero") ||
        !Capabilities::IsVerified("NPC.Spawn") ||
        !Capabilities::IsVerified("HeroPawn.Visibility"))
    {
        Debug::Log("required creature scripting capabilities are unavailable");
    }
    if (Capabilities::IsAvailable("Runtime.Scheduler"))
    {
        Scheduler::After(0.05f, @OnSchedulerOneShot);
        SchedulerRepeatingTask = Scheduler::Every(0.02f, @OnSchedulerRepeat);
    }
    if (Capabilities::IsAvailable("Runtime.Events.Bus"))
    {
        WorldReadySubscription = Events::Subscribe("WorldReady", @OnFrameworkEvent);
    }
    if (Capabilities::IsAvailable("Runtime.Persistence"))
    {
        const bool hadPriorLaunch = Storage::GetBoolean("framework.prior_launch", false);
        const int64 launchCount = Storage::GetInteger("framework.launch_count", 0) + 1;
        const bool wrote =
            Storage::SetBoolean("framework.prior_launch", true) &&
            Storage::SetInteger("framework.launch_count", launchCount) &&
            Storage::SetString("framework.string_probe", "AlbionTogether") &&
            Storage::SetNumber("framework.number_probe", 12.5) &&
            Storage::SetBoolean("framework.bool_probe", true) &&
            Storage::SetString("framework.transient_probe", "remove-me");
        const bool removed = Storage::Remove("framework.transient_probe");
        const bool readBack =
            Storage::GetInteger("framework.launch_count", 0) == launchCount &&
            Storage::GetString("framework.string_probe", "") == "AlbionTogether" &&
            Storage::GetNumber("framework.number_probe", 0.0) == 12.5 &&
            Storage::GetBoolean("framework.bool_probe", false) &&
            !Storage::Has("framework.transient_probe");
        if (wrote && removed && readBack && Storage::Flush())
        {
            Debug::Event(
                "PersistenceProbePassed",
                hadPriorLaunch ? "cross-launch state restored" : "first persisted launch initialized");
        }
        else
        {
            Debug::Log("typed per-module persistence probe failed");
        }
    }
}

void OnWorldReady()
{
    Debug::Event("ScriptWorldReady", "AngelScript received the typed world-ready callback");
    if (!Capabilities::IsAvailable("Quest.State.Read"))
    {
        return;
    }

    array<string> questNames = {
        "Q_Arena",
        "Q_BanditCamp",
        "Q_AwakeningTheOracle",
        "Q_AmbushTraders",
        "Q_SunnyvaleMaster"
    };
    bool observedKnownState = false;
    for (uint index = 0; index < questNames.length(); ++index)
    {
        const string questName = questNames[index];
        const bool active = Quest::IsActive(questName);
        const bool registered = Quest::IsRegistered(questName);
        const bool completed = Quest::IsCompleted(questName);
        const bool failed = Quest::IsFailed(questName);
        observedKnownState = observedKnownState || active || registered || completed || failed;
        string state = "";
        if (active) state += "active,";
        if (registered) state += "registered,";
        if (completed) state += "completed,";
        if (failed) state += "failed,";
        if (state.length() == 0) state = "none";
        Debug::Event("QuestStateSample", questName + "=" + state);
    }

    const string missingQuest = "Q_ALBION_TOGETHER_DOES_NOT_EXIST";
    const bool missingState =
        Quest::IsActive(missingQuest) ||
        Quest::IsRegistered(missingQuest) ||
        Quest::IsCompleted(missingQuest) ||
        Quest::IsFailed(missingQuest);
    if (observedKnownState && !missingState)
    {
        Debug::Event("QuestStateReadPassed", "known retail quest state distinguished from an unknown quest");
    }
    else
    {
        Debug::Event("QuestStateReadInconclusive", "adult save exposed no distinguishable state for the sampled quest names");
    }
}

void RetainCurrentPuppet()
{
    Creature::ClearPlayerCombat();
    Creature::ClearHeroShadow();
    Creature::ClearPhysicsWorldPositionMirror();
    Creature::ClearAnimationMotionMirror();
    Creature::ClearPlayerFrameInputRouter();
    Creature::ClearMovementFacing();
    if (PuppetControl !is null)
    {
        PuppetControl.ClearAllActions(true);
        PuppetControl.ReleaseControl();
        @PuppetControl = null;
    }
    if (Puppet !is null)
    {
        Puppet.SetCollidable(false);
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
    }
}

void SetHeroPuppetMode(bool enabled)
{
    if (Hero is null || !Hero.Valid)
    {
        return;
    }

    // The native NPC owns collision while it is the visible body. Leaving the
    // hidden Hero collidable creates two overlapping physics bodies and stops
    // the controlled movement target after the first small displacement.
    Hero.SetAttackable(!enabled);
    Hero.SetDamageable(!enabled);
    Hero.SetCollidable(!enabled);
    HeroPawn::SetVisible(Hero, !enabled);
}

void RestoreHero()
{
    RetainCurrentPuppet();
    if (Hero is null)
    {
        @Hero = Player::GetHero();
    }
    if (Hero !is null)
    {
        SetHeroPuppetMode(false);
    }
    Debug::Log("authoritative Hero presentation restored");
    Debug::Event("AppearanceHeroRestored", "scripted puppet retired and Hero presentation restored");
}

void CyclePuppet()
{
    RetainCurrentPuppet();
    @Hero = Player::GetHero();
    if (Hero is null || !Hero.Valid)
    {
        Debug::Log("cycle ignored because the Hero is unavailable");
        return;
    }

    const string definition = CreatureDefinitions[CreatureIndex];
    CreatureIndex = (CreatureIndex + 1) % CreatureDefinitions.length();
    Vector3 puppetSpawnPosition = Hero.Position;
    // Every form starts on the Hero and receives the same player-owned native
    // locomotion, facing, and empty scripted-control policy. Releasing these
    // bindings while cycling lets hostile retail brains reacquire targets,
    // rotate toward them, and commit crimes attributed to the proxy.
    @Puppet = NPC::Spawn(
        definition,
        puppetSpawnPosition,
        "SCRIPT_NAME_ALBION_TOGETHER_PUPPET");
    if (Puppet is null || !Puppet.Valid)
    {
        Debug::Log("CreateCreature failed for " + definition);
        @Puppet = null;
        return;
    }
    if (!Creature::IsCreature(Puppet) ||
        Creature::GetHealth(Puppet) < 0.0f ||
        Creature::GetMaximumHealth(Puppet) <= 0.0f ||
        Player::GetHealth() < 0.0f ||
        Player::GetMaximumHealth() <= 0.0f)
    {
        Debug::Log("combat-health read failed for " + definition);
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
        return;
    }
    Debug::Event("CreatureHealthRead", definition);
    if (Puppet.OpenDoor || Puppet.ScriptCounter < 0)
    {
        Debug::Log("entity interaction-state sanity check failed for " + definition);
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
        return;
    }
    const bool interactionStateSample = Puppet.Sneaking ||
        Puppet.AwareOfHero || Puppet.Unconscious || Puppet.Usable ||
        Puppet.SummonedCreature || Puppet.ActivationTriggerActive;
    if (interactionStateSample)
    {
        Debug::Log("entity interaction-state sample contained one or more active flags");
    }
    Debug::Event("EntityInteractionStateRead", definition);
    const string entityDefinition = Puppet.DefinitionName;
    const string entityName = Puppet.Name;
    const string currentMap = Puppet.CurrentMapName;
    const string homeMap = Puppet.HomeMapName;
    if (entityDefinition != definition || entityName.length() == 0 || currentMap.length() == 0)
    {
        Debug::Log(
            "entity metadata sanity check failed: expected=" + definition +
            " actual=" + entityDefinition + " name=" + entityName +
            " current_map=" + currentMap + " home_map=" + homeMap);
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
        return;
    }
    Debug::Event(
        "EntityMetadataRead",
        entityDefinition + " name=" + entityName + " current_map=" + currentMap +
        " home_map=" + homeMap);

    Debug::Log("proxy setup: SetAttackable(false) begin");
    Puppet.SetAttackable(false);
    Debug::Log("proxy setup: SetAttackable(false) complete");
    Debug::Log("proxy setup: SetDamageable(false) begin");
    Puppet.SetDamageable(false);
    Debug::Log("proxy setup: SetDamageable(false) complete");
    if (!Puppet.SetFriendsWithEverything(true))
    {
        Debug::Event(
            "ProxyHostilityPolicyFailed",
            definition + " could not be marked friendly with the retail world");
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
        return;
    }
    // Keep the proxy on its real physics stack. A non-collidable creature can
    // still receive scripted position updates, but its navigator-derived
    // velocity and turn state no longer reliably drive visible locomotion.
    Debug::Log("proxy setup: SetCollidable(true) begin");
    Puppet.SetCollidable(true);
    Debug::Log("proxy setup: SetCollidable(true) complete");
    Debug::Log("proxy setup: SetDrawable(true) begin");
    Puppet.SetDrawable(true);
    Debug::Log("proxy setup: SetDrawable(true) complete");

    @PuppetControl = NPC::TakeControl(Puppet, Highest);
    const bool controlReady =
        PuppetControl !is null &&
        PuppetControl.ClearCommands();
    const bool frameInputReady = controlReady &&
        Creature::RoutePlayerFrameInput(Hero, Puppet);
    const bool movementFacingReady = frameInputReady &&
        Creature::RouteMovementFacing(Puppet);
    const bool heroShadowReady = movementFacingReady &&
        Creature::RouteHeroShadow(Puppet, Hero);
    const bool combatReady = heroShadowReady &&
        Creature::RoutePlayerCombat(Hero, Puppet);
    if (!controlReady || !frameInputReady || !movementFacingReady ||
        !heroShadowReady || !combatReady)
    {
        Creature::ClearPlayerCombat();
        Creature::ClearHeroShadow();
        Creature::ClearPlayerFrameInputRouter();
        Creature::ClearMovementFacing();
        Debug::Event(
            "PlayerFrameInputTraceFailed",
            definition + " could not acquire empty highest-priority control, bind player locomotion/facing, shadow the hidden Hero, or arm NPC combat routing");
        if (PuppetControl !is null)
        {
            PuppetControl.ReleaseControl();
            @PuppetControl = null;
        }
        Puppet.SetDrawable(false);
        RetainedCreatures.insertLast(Puppet);
        @Puppet = null;
        return;
    }
    Debug::Event(
        "ProxyHostilityPolicyApplied",
        definition + " is friendly with the world; highest-priority scripted control owns decisions and has no autonomous actions");
    Debug::Event(
        "HeroProxyPresentationPolicyApplied",
        definition + " shadows the hidden Hero physics body; safe temporary weapon unequip remains pending native validation");
    Debug::Event(
        "PlayerFrameInputTraceReady",
        definition + " spawned on Hero; native scripted control is empty; Hero frame displacement is bound to NPC navigator requests; autonomous look interest is suppressed and the NPC's own motion drives body facing after its retail frame update");
    PuppetWorldPositionMirrored = false;
    PuppetStartPosition = Puppet.Position;
    PuppetStartFacing = Puppet.Facing;
    CreatureLocomotionState@ locomotion = Creature::InspectLocomotion(Puppet);
    if (locomotion !is null && locomotion.Valid)
    {
        PuppetPhysicsStartPosition = locomotion.PhysicsPosition;
        PuppetAnimationStartHash = locomotion.AnimationStateHash;
        PuppetWorldPositionMirrored = false;
        NativeLocomotionStackReported =
            locomotion.HasPhysicsNavigator &&
            locomotion.HasCreatureNavigation &&
            locomotion.HasAnimationComplex;
        if (NativeLocomotionStackReported)
        {
            Debug::Event(
                "NativeLocomotionStackObserved",
                "CTCPhysicsNavigator + CTCCreatureNavigation + CTCAnimationComplex validated");
        }
    }
    else
    {
        PuppetPhysicsStartPosition = PuppetStartPosition;
        PuppetAnimationStartHash = 0;
        PuppetWorldPositionMirrored = false;
        NativeLocomotionStackReported = false;
        Debug::Event(
            "NativeLocomotionStackMissing",
            "typed locomotion snapshot did not validate the full native stack");
    }
    PuppetActiveSeconds = 0.0f;
    LocomotionSampleSeconds = 0.0f;
    PuppetTranslated = false;
    PuppetRotated = false;
    PuppetPhysicsAdvanced = false;
    PuppetAnimationAdvanced = false;
    LocomotionReported = false;
    CurrentPuppetUsesPlayerFrameInput = true;
    PlayerFrameInputMovementReported = false;
    SetHeroPuppetMode(true);
    Debug::Log("native controlled puppet active: " + definition);
    Debug::Event(
        "AppearancePuppetControlReady",
        "verified player-frame input, movement-facing, neutral decisions, Hero shadow-follow, and NPC combat routing active");
    Debug::Event("AppearanceFormReady", definition);
}

void OnUnload()
{
    RestoreHero();
}

void OnKeyPressed(uint virtualKey, bool shiftPressed)
{
    if (virtualKey != NumberRowOne)
    {
        return;
    }
    if (shiftPressed)
    {
        RestoreHero();
    }
    else
    {
        CyclePuppet();
    }
}

void OnTick(float deltaSeconds)
{
    if (deltaSeconds < 0.0f || Hero is null || Puppet is null)
    {
        return;
    }

    PuppetActiveSeconds += deltaSeconds;
    LocomotionSampleSeconds += deltaSeconds;
    const Vector3 currentPosition = Puppet.Position;
    const float currentFacing = Puppet.Facing;
    if (currentPosition.HorizontalDistanceTo(PuppetStartPosition) >= 0.25f)
    {
        PuppetTranslated = true;
    }
    float facingDelta = currentFacing - PuppetStartFacing;
    if (facingDelta < 0.0f)
    {
        facingDelta = -facingDelta;
    }
    if (facingDelta >= 0.02f)
    {
        PuppetRotated = true;
    }

    if (LocomotionSampleSeconds >= 0.10f)
    {
        LocomotionSampleSeconds = 0.0f;
        CreatureLocomotionState@ locomotion = Creature::InspectLocomotion(Puppet);
        if (locomotion !is null && locomotion.Valid)
        {
            if (!NativeLocomotionStackReported &&
                locomotion.HasPhysicsNavigator &&
                locomotion.HasCreatureNavigation &&
                locomotion.HasAnimationComplex)
            {
                NativeLocomotionStackReported = true;
                Debug::Event(
                    "NativeLocomotionStackObserved",
                    "CTCPhysicsNavigator + CTCCreatureNavigation + CTCAnimationComplex validated");
            }
            if (locomotion.PhysicsPosition.HorizontalDistanceTo(
                    PuppetPhysicsStartPosition) >= 0.25f)
            {
                PuppetPhysicsAdvanced = true;
            }
            if (PuppetAnimationStartHash != 0 &&
                locomotion.AnimationStateHash != 0 &&
                locomotion.AnimationStateHash != PuppetAnimationStartHash)
            {
                PuppetAnimationAdvanced = true;
            }
            PuppetWorldPositionMirrored = PuppetWorldPositionMirrored ||
                Creature::MirroredPhysicsWorldPositionCount() > 0;
        }
    }

    if (!LocomotionReported && PuppetWorldPositionMirrored)
    {
        LocomotionReported = true;
        Debug::Event(
            "PhysicsWorldPositionMirrorObserved",
            "absolute Hero physics position was mirrored; locomotion, heading ownership, and gait remain unproven");
    }

    if (!PlayerFrameInputMovementReported &&
        CurrentPuppetUsesPlayerFrameInput &&
        NativeLocomotionStackReported &&
        PuppetTranslated &&
        PuppetPhysicsAdvanced &&
        PuppetAnimationAdvanced &&
        Creature::RoutedPlayerFrameCount() > 0 &&
        Creature::RoutedMovementFacingCount() > 0)
    {
        PlayerFrameInputMovementReported = true;
        Debug::Event(
            "PlayerFrameInputMovementObserved",
            "verified Hero frame displacement produced native NPC navigator requests; the NPC's own CThingCreature update produced CThing and physics displacement, locomotion-mode input, animation-complex state activity, and movement-derived body facing after autonomous look suppression");
    }
}
