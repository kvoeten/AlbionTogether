#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>

struct IDirect3DDevice9;

namespace fable::scripting { class ScriptHost; }

namespace fable::ui::imgui
{
    class ImGuiDx9Runtime final
    {
    public:
        ImGuiDx9Runtime() noexcept = default;
        ~ImGuiDx9Runtime() noexcept;

        ImGuiDx9Runtime(const ImGuiDx9Runtime&) = delete;
        ImGuiDx9Runtime& operator=(const ImGuiDx9Runtime&) = delete;

        bool Install(
            HMODULE gameModule,
            scripting::ScriptHost& scripts,
            const core::Diagnostics& diagnostics) noexcept;
        bool Shutdown() noexcept;

        void Toggle() noexcept;
        void Hide() noexcept;
        bool HandleWindowMessage(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam) noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool IsVisible() const noexcept;

        // Called only by the verified DX9 call-site detours.
        void RenderFrame(IDirect3DDevice9* device) noexcept;
        void BeforeDeviceReset() noexcept;
        void AfterDeviceReset(long result) noexcept;

    private:
        bool EnsureContext(IDirect3DDevice9& device) noexcept;
        void DestroyContext() noexcept;
        void ApplyFableStyle() noexcept;

        core::hooking::CodePatch endScenePatch_;
        core::hooking::CodePatch resetPatch_;
        scripting::ScriptHost* scripts_ = nullptr;
        IDirect3DDevice9* device_ = nullptr;
        HWND window_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        mutable std::recursive_mutex imguiMutex_;
        std::atomic<bool> visible_{false};
        std::atomic<bool> shuttingDown_{false};
        bool win32BackendReady_ = false;
        bool dx9BackendReady_ = false;
        bool contextReady_ = false;
        bool installed_ = false;
    };
}
