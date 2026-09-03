# AngelScript UI

AlbionTogether renders script-owned windows through Dear ImGui. Native C++ owns
only the verified DX9 bridge, input forwarding, and game API bindings; window
layout and tool behavior stay in `.as` modules.

Place a script anywhere below the deployed `scripts/` directory and define an
`OnGui` callback:

```angelscript
string definition = "CREATURE_BS_GUARD";

void OnGui()
{
    ImGui::SetNextWindowSize(420.0f, 220.0f);
    if (ImGui::Begin("My tools"))
    {
        definition = ImGui::InputText("Definition", definition);
        if (ImGui::Button("Spawn"))
        {
            DevTools::SpawnEntity(definition, 0.0f, 0.0f, 0.0f);
        }
    }
    ImGui::End();
}
```

Press F8 to show or hide script UI. `OnGui` is dispatched only during a valid
ImGui frame; UI calls made from other script callbacks are ignored. Native game
mutations are queued through `DevTools` and drained on the normal gameplay
thread, so rendering never invokes game hooks directly.

The built-in example is `Client/Scripts/Debug/DeveloperTools.as`. It uses the
same public bindings available to additional script modules.
