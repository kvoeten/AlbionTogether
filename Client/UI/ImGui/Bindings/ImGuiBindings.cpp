#include "ImGuiBindings.h"

#include "Game/Math/Vector3.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"

#include <angelscript.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
    thread_local bool g_scriptFrameActive = false;

    bool FrameActive() noexcept
    {
        return g_scriptFrameActive && ImGui::GetCurrentContext() != nullptr;
    }

    bool Begin(const std::string& title) noexcept
    {
        return FrameActive() && ImGui::Begin(title.c_str());
    }

    void End() noexcept
    {
        if (FrameActive()) ImGui::End();
    }

    void SetNextWindowSize(float width, float height) noexcept
    {
        if (FrameActive())
        {
            ImGui::SetNextWindowSize(
                ImVec2(width, height), ImGuiCond_FirstUseEver);
        }
    }

    bool BeginTabBar(const std::string& id) noexcept
    {
        return FrameActive() && ImGui::BeginTabBar(id.c_str());
    }

    void EndTabBar() noexcept
    {
        if (FrameActive()) ImGui::EndTabBar();
    }

    bool BeginTabItem(const std::string& label) noexcept
    {
        return FrameActive() && ImGui::BeginTabItem(label.c_str());
    }

    void EndTabItem() noexcept
    {
        if (FrameActive()) ImGui::EndTabItem();
    }

    bool CollapsingHeader(const std::string& label) noexcept
    {
        return FrameActive() && ImGui::CollapsingHeader(label.c_str());
    }

    void Text(const std::string& text) noexcept
    {
        if (FrameActive()) ImGui::TextUnformatted(text.c_str());
    }

    void TextWrapped(const std::string& text) noexcept
    {
        if (FrameActive()) ImGui::TextWrapped("%s", text.c_str());
    }

    void TextDisabled(const std::string& text) noexcept
    {
        if (FrameActive()) ImGui::TextDisabled("%s", text.c_str());
    }

    void Separator() noexcept
    {
        if (FrameActive()) ImGui::Separator();
    }

    void Spacing() noexcept
    {
        if (FrameActive()) ImGui::Spacing();
    }

    void SameLine() noexcept
    {
        if (FrameActive()) ImGui::SameLine();
    }

    void SetNextItemWidth(float width) noexcept
    {
        if (FrameActive()) ImGui::SetNextItemWidth(width);
    }

    bool Button(const std::string& label) noexcept
    {
        return FrameActive() && ImGui::Button(label.c_str());
    }

    bool Checkbox(const std::string& label, bool value) noexcept
    {
        if (FrameActive()) (void)ImGui::Checkbox(label.c_str(), &value);
        return value;
    }

    std::string InputText(
        const std::string& label,
        const std::string& value)
    {
        if (!FrameActive()) return value;
        std::array<char, 512> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        (void)ImGui::InputText(label.c_str(), buffer.data(), buffer.size());
        return buffer.data();
    }

    float InputFloat(const std::string& label, float value) noexcept
    {
        if (FrameActive()) (void)ImGui::InputFloat(label.c_str(), &value);
        return value;
    }

    fable::game::Vector3 InputFloat3(
        const std::string& label,
        const fable::game::Vector3& value) noexcept
    {
        float values[3] = {value.x, value.y, value.z};
        if (FrameActive())
        {
            (void)ImGui::InputFloat3(label.c_str(), values);
        }
        return {values[0], values[1], values[2]};
    }

    std::uint64_t InputUInt64(
        const std::string& label,
        std::uint64_t value) noexcept
    {
        if (FrameActive())
        {
            (void)ImGui::InputScalar(
                label.c_str(), ImGuiDataType_U64, &value);
        }
        return value;
    }

    bool BeginCombo(
        const std::string& label,
        const std::string& preview) noexcept
    {
        return FrameActive() &&
            ImGui::BeginCombo(label.c_str(), preview.c_str());
    }

    void EndCombo() noexcept
    {
        if (FrameActive()) ImGui::EndCombo();
    }

    bool Selectable(const std::string& label, bool selected) noexcept
    {
        return FrameActive() && ImGui::Selectable(label.c_str(), selected);
    }

    void SetItemDefaultFocus() noexcept
    {
        if (FrameActive()) ImGui::SetItemDefaultFocus();
    }

    void BeginDisabled(bool disabled) noexcept
    {
        if (FrameActive()) ImGui::BeginDisabled(disabled);
    }

    void EndDisabled() noexcept
    {
        if (FrameActive()) ImGui::EndDisabled();
    }
}

namespace fable::ui::imgui::bindings
{
    void BeginScriptFrame() noexcept
    {
        g_scriptFrameActive = true;
    }

    void EndScriptFrame() noexcept
    {
        g_scriptFrameActive = false;
    }

    bool RegisterImGuiBindingGroup(
        fable::scripting::bindings::BindingContext& context)
    {
        return RegisterImGuiBindings(context.Engine);
    }

    bool RegisterImGuiBindings(asIScriptEngine& engine)
    {
        int result = engine.SetDefaultNamespace("ImGui");
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool Begin(const string &in title)",
            asFUNCTION(Begin), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void End()", asFUNCTION(End), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void SetNextWindowSize(float width, float height)",
            asFUNCTION(SetNextWindowSize), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool BeginTabBar(const string &in id)",
            asFUNCTION(BeginTabBar), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void EndTabBar()", asFUNCTION(EndTabBar), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool BeginTabItem(const string &in label)",
            asFUNCTION(BeginTabItem), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void EndTabItem()", asFUNCTION(EndTabItem), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool CollapsingHeader(const string &in label)",
            asFUNCTION(CollapsingHeader), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void Text(const string &in text)",
            asFUNCTION(Text), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void TextWrapped(const string &in text)",
            asFUNCTION(TextWrapped), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void TextDisabled(const string &in text)",
            asFUNCTION(TextDisabled), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void Separator()", asFUNCTION(Separator), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void Spacing()", asFUNCTION(Spacing), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void SameLine()", asFUNCTION(SameLine), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void SetNextItemWidth(float width)",
            asFUNCTION(SetNextItemWidth), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool Button(const string &in label)",
            asFUNCTION(Button), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool Checkbox(const string &in label, bool value)",
            asFUNCTION(Checkbox), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "string InputText(const string &in label, const string &in value)",
            asFUNCTION(InputText), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "float InputFloat(const string &in label, float value)",
            asFUNCTION(InputFloat), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "Vector3 InputFloat3(const string &in label, const Vector3 &in value)",
            asFUNCTION(InputFloat3), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "uint64 InputUInt64(const string &in label, uint64 value)",
            asFUNCTION(InputUInt64), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool BeginCombo(const string &in label, const string &in preview)",
            asFUNCTION(BeginCombo), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void EndCombo()", asFUNCTION(EndCombo), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool Selectable(const string &in label, bool selected = false)",
            asFUNCTION(Selectable), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void SetItemDefaultFocus()",
            asFUNCTION(SetItemDefaultFocus), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void BeginDisabled(bool disabled = true)",
            asFUNCTION(BeginDisabled), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "void EndDisabled()", asFUNCTION(EndDisabled), asCALL_CDECL) : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}

FABLE_SCRIPT_BINDING_GROUP(
    ImGui,
    355,
    &fable::ui::imgui::bindings::RegisterImGuiBindingGroup);
