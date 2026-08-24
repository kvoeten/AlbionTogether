#pragma once

#include <Windows.h>

namespace fable::launcher::automation
{
    class ScopedSyntheticKey
    {
    public:
        explicit ScopedSyntheticKey(UINT virtualKey);
        ~ScopedSyntheticKey();

        ScopedSyntheticKey(const ScopedSyntheticKey&) = delete;
        ScopedSyntheticKey& operator=(const ScopedSyntheticKey&) = delete;

        bool Press();
        bool Release();
        [[nodiscard]] bool down() const;

    private:
        WORD scanCode_ = 0;
        bool down_ = false;
    };

    class ScopedSyntheticMouseButton
    {
    public:
        ScopedSyntheticMouseButton() = default;
        ~ScopedSyntheticMouseButton();

        ScopedSyntheticMouseButton(const ScopedSyntheticMouseButton&) = delete;
        ScopedSyntheticMouseButton& operator=(const ScopedSyntheticMouseButton&) = delete;

        bool Press(HWND targetWindow);
        bool Release();
        [[nodiscard]] bool down() const;

    private:
        HWND window_ = nullptr;
        LPARAM point_ = 0;
        bool down_ = false;
    };
}
