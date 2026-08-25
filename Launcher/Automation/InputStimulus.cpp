#include "InputStimulus.h"

namespace fable::launcher::automation
{
    ScopedSyntheticKey::ScopedSyntheticKey(UINT virtualKey)
        : scanCode_(static_cast<WORD>(
              MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC)))
    {
    }

    ScopedSyntheticKey::~ScopedSyntheticKey()
    {
        Release();
    }

    bool ScopedSyntheticKey::Press()
    {
        if (down_)
        {
            return true;
        }
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scanCode_;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        down_ = SendInput(1, &input, sizeof(input)) == 1;
        return down_;
    }

    bool ScopedSyntheticKey::Release()
    {
        if (!down_)
        {
            return true;
        }
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scanCode_;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        const bool released = SendInput(1, &input, sizeof(input)) == 1;
        if (released)
        {
            down_ = false;
        }
        return released;
    }

    bool ScopedSyntheticKey::down() const
    {
        return down_;
    }

    ScopedSyntheticMouseButton::~ScopedSyntheticMouseButton()
    {
        Release();
    }

    bool ScopedSyntheticMouseButton::Press(HWND targetWindow)
    {
        if (down_)
        {
            return true;
        }
        if (targetWindow == nullptr)
        {
            return false;
        }
        RECT bounds = {};
        if (!GetClientRect(targetWindow, &bounds))
        {
            return false;
        }
        const int x = (bounds.right - bounds.left) / 2;
        const int y = (bounds.bottom - bounds.top) / 2;
        point_ = MAKELPARAM(x, y);
        window_ = targetWindow;
        down_ = PostMessageW(
            window_, WM_LBUTTONDOWN, MK_LBUTTON, point_) != FALSE;
        return down_;
    }

    bool ScopedSyntheticMouseButton::Release()
    {
        if (!down_)
        {
            return true;
        }
        const bool released = window_ != nullptr &&
            PostMessageW(window_, WM_LBUTTONUP, 0, point_) != FALSE;
        if (released)
        {
            down_ = false;
            window_ = nullptr;
        }
        return released;
    }

    bool ScopedSyntheticMouseButton::down() const
    {
        return down_;
    }
}
