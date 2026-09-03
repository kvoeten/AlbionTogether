#include "ImGuiDx9Runtime.h"

#include "Scripting/Runtime/Host/ScriptHost.h"
#include "UI/ImGui/Bindings/ImGuiBindings.h"

#include <d3d9.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <array>
#include <atomic>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

namespace
{
    constexpr std::uintptr_t PreferredImageBase = 0x00400000U;
    constexpr std::uintptr_t EndSceneSiteVa = 0x015E5902U;
    constexpr std::uintptr_t EndSceneResumeVa = 0x015E5908U;
    constexpr std::uintptr_t ResetSiteVa = 0x015D3B6CU;
    constexpr std::uintptr_t ResetResumeVa = 0x015D3B77U;

    constexpr std::array<std::uint8_t, 6> EndSceneExpected = {
        0x8B, 0x91, 0xA8, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 11> ResetExpected = {
        0x8D, 0x54, 0x24, 0x64, 0x52, 0x50,
        0x8B, 0x41, 0x40, 0xFF, 0xD0};

    std::atomic<fable::ui::imgui::ImGuiDx9Runtime*> g_active{nullptr};
    std::atomic<std::uint32_t> g_callsInFlight{0U};
    void* g_endSceneResume = nullptr;
    void* g_resetResume = nullptr;

    class HookCall final
    {
    public:
        HookCall() noexcept
        {
            g_callsInFlight.fetch_add(1U, std::memory_order_acq_rel);
        }

        ~HookCall() noexcept
        {
            g_callsInFlight.fetch_sub(1U, std::memory_order_acq_rel);
        }
    };

    extern "C" void __stdcall RenderFrameThunk(
        IDirect3DDevice9* device) noexcept
    {
        HookCall call;
        auto* const active = g_active.load(std::memory_order_acquire);
        if (active != nullptr) active->RenderFrame(device);
    }

    extern "C" void __stdcall BeforeResetThunk() noexcept
    {
        g_callsInFlight.fetch_add(1U, std::memory_order_acq_rel);
        auto* const active = g_active.load(std::memory_order_acquire);
        if (active != nullptr) active->BeforeDeviceReset();
    }

    extern "C" void __stdcall AfterResetThunk(long result) noexcept
    {
        auto* const active = g_active.load(std::memory_order_acquire);
        if (active != nullptr) active->AfterDeviceReset(result);
        g_callsInFlight.fetch_sub(1U, std::memory_order_acq_rel);
    }

#if defined(_M_IX86)
    __declspec(naked) void EndSceneDetour()
    {
        __asm
        {
            mov edx, [ecx + 0A8h]
            pushad
            push eax
            call RenderFrameThunk
            popad
            jmp dword ptr [g_endSceneResume]
        }
    }

    __declspec(naked) void ResetDetour()
    {
        __asm
        {
            pushad
            call BeforeResetThunk
            popad

            lea edx, [esp + 64h]
            push edx
            push eax
            mov eax, [ecx + 40h]
            call eax

            push eax
            pushad
            mov edx, [esp + 20h]
            push edx
            call AfterResetThunk
            popad
            pop eax
            jmp dword ptr [g_resetResume]
        }
    }
#else
    void EndSceneDetour() {}
    void ResetDetour() {}
#endif

    bool IsInputMessage(UINT message) noexcept
    {
        return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
            (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            message == WM_CHAR || message == WM_SETCURSOR;
    }
}

namespace fable::ui::imgui
{
    ImGuiDx9Runtime::~ImGuiDx9Runtime() noexcept
    {
        (void)Shutdown();
    }

    bool ImGuiDx9Runtime::Install(
        HMODULE gameModule,
        scripting::ScriptHost& scripts,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)scripts;
        (void)diagnostics;
        return false;
#else
        if (installed_ || gameModule == nullptr ||
            g_active.load(std::memory_order_acquire) != nullptr)
        {
            return false;
        }

        diagnostics_ = diagnostics;
        scripts_ = &scripts;
        shuttingDown_.store(false, std::memory_order_release);
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const endSceneSite = reinterpret_cast<void*>(
            base + EndSceneSiteVa - PreferredImageBase);
        auto* const resetSite = reinterpret_cast<void*>(
            base + ResetSiteVa - PreferredImageBase);
        g_endSceneResume = reinterpret_cast<void*>(
            base + EndSceneResumeVa - PreferredImageBase);
        g_resetResume = reinterpret_cast<void*>(
            base + ResetResumeVa - PreferredImageBase);
        g_active.store(this, std::memory_order_release);

        if (!endScenePatch_.InstallRelativeJump(
                endSceneSite,
                EndSceneExpected.data(),
                EndSceneExpected.size(),
                reinterpret_cast<void*>(&EndSceneDetour),
                EndSceneExpected.size()) ||
            !resetPatch_.InstallRelativeJump(
                resetSite,
                ResetExpected.data(),
                ResetExpected.size(),
                reinterpret_cast<void*>(&ResetDetour),
                ResetExpected.size()))
        {
            (void)resetPatch_.Shutdown();
            (void)endScenePatch_.Shutdown();
            g_active.store(nullptr, std::memory_order_release);
            scripts_ = nullptr;
            diagnostics_.Log(
                "ImGui: verified Fable DX9 call-site hook installation failed.");
            diagnostics_ = {};
            return false;
        }

        installed_ = true;
        diagnostics_.Event(
            "ScriptImGuiReady",
            "Fable DX9 renderer bridge installed; AngelScript OnGui modules own all windows");
        return true;
#endif
    }

    bool ImGuiDx9Runtime::Shutdown() noexcept
    {
        if (!installed_)
        {
            return true;
        }

        shuttingDown_.store(true, std::memory_order_release);
        visible_.store(false, std::memory_order_release);
        const bool resetRestored = resetPatch_.Shutdown();
        const bool endSceneRestored = endScenePatch_.Shutdown();
        if (!resetRestored || !endSceneRestored)
        {
            diagnostics_.Event(
                "ScriptImGuiShutdownDeferred",
                "another hook owns a DX9 call site; renderer state retained to preserve hook chaining");
            return false;
        }

        for (unsigned int attempt = 0;
             attempt < 2'000U &&
                g_callsInFlight.load(std::memory_order_acquire) != 0U;
             ++attempt)
        {
            Sleep(1);
        }
        if (g_callsInFlight.load(std::memory_order_acquire) != 0U)
        {
            diagnostics_.Event(
                "ScriptImGuiShutdownDeferred",
                "DX9 callback did not quiesce; renderer state retained");
            return false;
        }

        g_active.store(nullptr, std::memory_order_release);
        {
            std::lock_guard<std::recursive_mutex> lock(imguiMutex_);
            DestroyContext();
        }
        g_endSceneResume = nullptr;
        g_resetResume = nullptr;
        scripts_ = nullptr;
        installed_ = false;
        diagnostics_ = {};
        return true;
    }

    void ImGuiDx9Runtime::Toggle() noexcept
    {
        if (installed_ && !shuttingDown_.load(std::memory_order_acquire))
        {
            visible_.store(!visible_.load(std::memory_order_acquire),
                std::memory_order_release);
        }
    }

    void ImGuiDx9Runtime::Hide() noexcept
    {
        visible_.store(false, std::memory_order_release);
    }

    bool ImGuiDx9Runtime::HandleWindowMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept
    {
        if (!contextReady_ || !IsVisible() || window != window_)
        {
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock(imguiMutex_);
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
        return IsInputMessage(message);
    }

    bool ImGuiDx9Runtime::IsAvailable() const noexcept
    {
        return installed_ && !shuttingDown_.load(std::memory_order_acquire);
    }

    bool ImGuiDx9Runtime::IsVisible() const noexcept
    {
        return visible_.load(std::memory_order_acquire);
    }

    void ImGuiDx9Runtime::RenderFrame(IDirect3DDevice9* const device) noexcept
    {
        if (device == nullptr || !IsAvailable() || !IsVisible())
        {
            return;
        }
        std::lock_guard<std::recursive_mutex> lock(imguiMutex_);
        if (!EnsureContext(*device) || scripts_ == nullptr)
        {
            return;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = true;
        bindings::BeginScriptFrame();
        scripts_->DispatchGui();
        bindings::EndScriptFrame();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiDx9Runtime::BeforeDeviceReset() noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(imguiMutex_);
        if (contextReady_) ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    void ImGuiDx9Runtime::AfterDeviceReset(const long result) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(imguiMutex_);
        if (contextReady_ && result >= 0)
        {
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }

    bool ImGuiDx9Runtime::EnsureContext(IDirect3DDevice9& device) noexcept
    {
        if (contextReady_ && device_ == &device)
        {
            return true;
        }
        if (contextReady_)
        {
            DestroyContext();
        }

        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (FAILED(device.GetCreationParameters(&parameters)) ||
            parameters.hFocusWindow == nullptr)
        {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        win32BackendReady_ = ImGui_ImplWin32_Init(parameters.hFocusWindow);
        if (!win32BackendReady_)
        {
            DestroyContext();
            return false;
        }
        dx9BackendReady_ = ImGui_ImplDX9_Init(&device);
        if (!dx9BackendReady_)
        {
            DestroyContext();
            return false;
        }

        device_ = &device;
        window_ = parameters.hFocusWindow;
        contextReady_ = true;
        ApplyFableStyle();
        diagnostics_.Event(
            "ScriptImGuiContextReady",
            "Dear ImGui Win32/DX9 context created for AngelScript windows");
        return true;
    }

    void ImGuiDx9Runtime::DestroyContext() noexcept
    {
        if (dx9BackendReady_)
        {
            ImGui_ImplDX9_Shutdown();
        }
        if (win32BackendReady_)
        {
            ImGui_ImplWin32_Shutdown();
        }
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::DestroyContext();
        }
        contextReady_ = false;
        dx9BackendReady_ = false;
        win32BackendReady_ = false;
        device_ = nullptr;
        window_ = nullptr;
    }

    void ImGuiDx9Runtime::ApplyFableStyle() noexcept
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0F;
        style.FrameRounding = 2.0F;
        style.FramePadding = ImVec2(6.0F, 4.25F);
        style.ItemSpacing = ImVec2(7.0F, 5.5F);
        ImVec4* const colors = style.Colors;
        colors[ImGuiCol_FrameBg] = ImVec4(0.34F, 0.25F, 0.04F, 1.00F);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.70F, 0.55F, 0.22F, 1.00F);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.90F, 0.65F, 0.32F, 1.00F);
        colors[ImGuiCol_Button] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.69F, 0.55F, 0.23F, 1.00F);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.62F, 0.48F, 0.18F, 1.00F);
        colors[ImGuiCol_Header] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.60F, 0.47F, 0.18F, 1.00F);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_Tab] = ImVec4(0.51F, 0.40F, 0.15F, 1.00F);
        colors[ImGuiCol_TabHovered] = ImVec4(0.70F, 0.55F, 0.22F, 1.00F);
        colors[ImGuiCol_TabSelected] = ImVec4(0.78F, 0.60F, 0.21F, 1.00F);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.94F, 0.80F, 0.38F, 1.00F);
        colors[ImGuiCol_NavCursor] = ImVec4(0.92F, 0.68F, 0.00F, 1.00F);
    }
}
