string entityDefinition = "CREATURE_BS_GUARD";
Vector3 spawnPosition(0.0f, 0.0f, 0.0f);

uint64 selectedEntityUid = 0;
Vector3 teleportPosition(0.0f, 0.0f, 0.0f);

uint64 regionExitUid = 0;
string questName = "";
int saveSectionIndex = 0;
string lastStatus = "Ready.";

string SaveSectionName()
{
    if (saveSectionIndex == 0) return "Entities";
    if (saveSectionIndex == 1) return "Player";
    if (saveSectionIndex == 2) return "Quests";
    if (saveSectionIndex == 3) return "Regions";
    return "Factions";
}

SaveSection SelectedSaveSection()
{
    if (saveSectionIndex == 0) return SaveSection::Entities;
    if (saveSectionIndex == 1) return SaveSection::Player;
    if (saveSectionIndex == 2) return SaveSection::Quests;
    if (saveSectionIndex == 3) return SaveSection::Regions;
    return SaveSection::Factions;
}

void ReadResults()
{
    string result;
    uint64 resultUid = 0;
    while (DevTools::PollResult(result, resultUid))
    {
        lastStatus = result;
        if (resultUid != 0) selectedEntityUid = resultUid;
    }
}

void DrawWorldTools()
{
    if (ImGui::CollapsingHeader("Spawn entity"))
    {
        entityDefinition = ImGui::InputText("Definition", entityDefinition);
        spawnPosition = ImGui::InputFloat3(
            "Position##spawn", spawnPosition);
        if (ImGui::Button("Spawn"))
        {
            lastStatus = DevTools::SpawnEntity(
                entityDefinition,
                spawnPosition.x,
                spawnPosition.y,
                spawnPosition.z)
                ? "Spawn queued."
                : "Spawn rejected.";
        }
    }

    if (ImGui::CollapsingHeader("Move entity"))
    {
        selectedEntityUid = ImGui::InputUInt64(
            "Entity UID", selectedEntityUid);
        teleportPosition = ImGui::InputFloat3(
            "Position##teleport", teleportPosition);
        if (ImGui::Button("Teleport"))
        {
            lastStatus = DevTools::TeleportEntity(
                selectedEntityUid,
                teleportPosition.x,
                teleportPosition.y,
                teleportPosition.z)
                ? "Teleport queued."
                : "Teleport rejected.";
        }
    }

    if (ImGui::CollapsingHeader("Scripted travel"))
    {
        regionExitUid = ImGui::InputUInt64(
            "Region exit UID", regionExitUid);
        ImGui::TextDisabled(
            "Uses the region exit's normal scripted action and map flow.");
        if (ImGui::Button("Use region exit"))
        {
            lastStatus = DevTools::UseRegionExit(regionExitUid)
                ? "Travel queued."
                : "Travel rejected.";
        }
    }
}

void DrawQuestTools()
{
    questName = ImGui::InputText("Quest name", questName);
    if (ImGui::Button("Query"))
    {
        lastStatus = DevTools::QueryQuest(questName)
            ? "Quest query queued."
            : "Quest query rejected.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Activate"))
    {
        lastStatus = DevTools::ActivateQuest(questName)
            ? "Quest activation queued."
            : "Quest activation rejected.";
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Quest mutations run through AlbionTogether's Quest API. The host "
        "captures and publishes the updated authoritative quest snapshot.");
}

void DrawStateTools()
{
    string preview = SaveSectionName();
    if (ImGui::BeginCombo("Save section", preview))
    {
        for (int index = 0; index < 5; ++index)
        {
            int previous = saveSectionIndex;
            saveSectionIndex = index;
            string name = SaveSectionName();
            saveSectionIndex = previous;
            bool selected = index == saveSectionIndex;
            if (ImGui::Selectable(name, selected)) saveSectionIndex = index;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Inspect authoritative state"))
    {
        lastStatus = DevTools::QuerySaveSection(SelectedSaveSection())
            ? "State query queued."
            : "State query rejected.";
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "This reports the host snapshot revision, size and fingerprint. "
        "It does not expose or edit raw save bytes.");
}

void OnGui()
{
    ReadResults();
    ImGui::SetNextWindowSize(640.0f, 500.0f);
    bool drawContents = ImGui::Begin("AlbionTogether Tools");
    if (drawContents)
    {
        bool host = DevTools::IsHost();
        if (!host)
        {
            ImGui::TextDisabled(
                "Host authority is required for the built-in world tools.");
            ImGui::Separator();
        }

        ImGui::BeginDisabled(!host);
        if (ImGui::BeginTabBar("DeveloperToolTabs"))
        {
            if (ImGui::BeginTabItem("World"))
            {
                DrawWorldTools();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Quests"))
            {
                DrawQuestTools();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("State"))
            {
                DrawStateTools();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Status: " + lastStatus);
        if (DevTools::PendingCount() > 0)
        {
            ImGui::TextDisabled("Native commands pending...");
        }
    }
    ImGui::End();
}
