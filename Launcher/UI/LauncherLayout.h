#pragma once

#include <Windows.h>

namespace fable::launcher::ui
{
    struct LauncherLayout final
    {
        RECT client = {};
        RECT header = {};
        RECT navigation = {};
        RECT footer = {};
        RECT content = {};
        RECT hostPanel = {};
        RECT joinPanel = {};
        RECT diagnosticsPanel = {};
        RECT networkPanel = {};
        RECT settingsPanel = {};

        [[nodiscard]] static LauncherLayout Calculate(
            const RECT& client,
            unsigned int dpi)
        {
            const auto scale = [dpi](const int value)
            {
                return MulDiv(value, static_cast<int>(dpi), 96);
            };
            LauncherLayout layout;
            layout.client = client;
            const int headerHeight = scale(72);
            const int footerHeight = scale(50);
            const int navigationWidth = scale(202);
            layout.header = {0, 0, client.right, headerHeight};
            layout.navigation = {
                0, headerHeight, navigationWidth, client.bottom - footerHeight};
            layout.footer = {
                0, client.bottom - footerHeight, client.right, client.bottom};
            layout.content = {
                navigationWidth,
                headerHeight,
                client.right,
                client.bottom - footerHeight};

            const int left = navigationWidth + scale(38);
            const int top = headerHeight + scale(150);
            const int bottom = client.bottom - footerHeight - scale(40);
            const int right = client.right - scale(34);
            const int gap = scale(18);
            const int diagnosticsWidth = scale(286);
            const int ordinaryWidth =
                (right - left - diagnosticsWidth - gap * 2) / 2;
            layout.hostPanel = {left, top, left + ordinaryWidth, bottom};
            layout.joinPanel = {
                layout.hostPanel.right + gap,
                top,
                layout.hostPanel.right + gap + ordinaryWidth,
                bottom};
            layout.diagnosticsPanel = {
                layout.joinPanel.right + gap,
                top,
                right,
                bottom};
            layout.networkPanel = {
                left, top, right, bottom};
            layout.settingsPanel = {
                left, top, right, bottom};
            return layout;
        }
    };
}
