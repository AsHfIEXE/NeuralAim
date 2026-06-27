#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <commdlg.h>
#include <algorithm>
#include <string>
#include <cstring>

#include "imgui/imgui.h"
#include "config.h"
#include "NeuralAim.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"

extern std::string g_iconLastError;

namespace
{
enum class GameOverlaySettingsPage
{
    All,
    General,
    Visuals,
    Icon
};

bool shouldDrawGameOverlayPage(GameOverlaySettingsPage current, GameOverlaySettingsPage wanted)
{
    return current == GameOverlaySettingsPage::All || current == wanted;
}
}

static void draw_game_overlay_page(GameOverlaySettingsPage page)
{
    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::General) &&
        OverlayUI::BeginSection("General", "game_overlay_section_general"))
    {
        if (OverlayUI::CheckboxRow("Enable", &config.game_overlay_enabled))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderIntRow("Overlay Max FPS (0 = uncapped)", &config.game_overlay_max_fps, 0, 256))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Draw Detection Boxes", &config.game_overlay_draw_boxes))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Compensate Overlay Latency", &config.game_overlay_compensate_latency))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Draw Future Positions", &config.game_overlay_draw_future))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Draw Wind Debug Tail", &config.game_overlay_draw_wind_tail))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Show Target Correction", &config.game_overlay_show_target_correction))
            OverlayConfig_MarkDirty();

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("Box Color", "game_overlay_section_box_color"))
    {
        bool colorChanged = false;

        colorChanged |= OverlayUI::SliderIntRow("Alpha", &config.game_overlay_box_a, 0, 255);
        colorChanged |= OverlayUI::SliderIntRow("Red", &config.game_overlay_box_r, 0, 255);
        colorChanged |= OverlayUI::SliderIntRow("Green", &config.game_overlay_box_g, 0, 255);
        colorChanged |= OverlayUI::SliderIntRow("Blue", &config.game_overlay_box_b, 0, 255);

        if (OverlayUI::SliderFloatRow("Box Thickness", &config.game_overlay_box_thickness, 0.5f, 10.0f, "%.1f"))
            OverlayConfig_MarkDirty();

        if (colorChanged)
        {
            config.clampGameOverlayColor();
            OverlayConfig_MarkDirty();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("Capture Frame", "game_overlay_section_capture_frame"))
    {
        if (OverlayUI::CheckboxRow("Draw Capture Frame", &config.game_overlay_draw_frame))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("Draw Circle Guide", &config.game_overlay_draw_circle_fov))
            OverlayConfig_MarkDirty();

        bool frameColorChanged = false;

        frameColorChanged |= OverlayUI::SliderIntRow("Alpha", &config.game_overlay_frame_a, 0, 255);
        frameColorChanged |= OverlayUI::SliderIntRow("Red", &config.game_overlay_frame_r, 0, 255);
        frameColorChanged |= OverlayUI::SliderIntRow("Green", &config.game_overlay_frame_g, 0, 255);
        frameColorChanged |= OverlayUI::SliderIntRow("Blue", &config.game_overlay_frame_b, 0, 255);

        if (OverlayUI::SliderFloatRow("Frame Thickness", &config.game_overlay_frame_thickness, 0.5f, 10.0f, "%.1f"))
            OverlayConfig_MarkDirty();

        if (frameColorChanged)
        {
            config.clampGameOverlayColor();
            OverlayConfig_MarkDirty();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("Future Point Style", "game_overlay_section_future_style"))
    {
        if (OverlayUI::SliderFloatRow("Point Radius", &config.game_overlay_future_point_radius, 1.0f, 20.0f, "%.1f"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("Point Step Alpha Falloff", &config.game_overlay_future_alpha_falloff, 0.1f, 5.0f, "%.2f"))
            OverlayConfig_MarkDirty();

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Icon) &&
        OverlayUI::BeginSection("Icon Overlay", "game_overlay_section_icon"))
    {
        if (OverlayUI::CheckboxRow("Enable Icon Overlay", &config.game_overlay_icon_enabled))
            OverlayConfig_MarkDirty();

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::BeginDisabled();
        }

        static bool pathInit = false;
        static char iconPathBuf[512];

        if (!pathInit)
        {
            pathInit = true;
            memset(iconPathBuf, 0, sizeof(iconPathBuf));
            std::string p = config.game_overlay_icon_path;
            if (p.size() >= sizeof(iconPathBuf)) p = p.substr(0, sizeof(iconPathBuf) - 1);
            memcpy(iconPathBuf, p.c_str(), p.size());
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Icon Path");
            const float browseW = 76.0f;
            const float inputW = std::max(1.0f, row.controlWidth - browseW - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##value", iconPathBuf, IM_ARRAYSIZE(iconPathBuf)))
            {
                config.game_overlay_icon_path = iconPathBuf;
                OverlayConfig_MarkDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse", ImVec2(browseW, 0.0f)))
            {
                char filePath[MAX_PATH] = {};
                OPENFILENAMEA ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = nullptr;
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = sizeof(filePath);
                ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.ico\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn))
                {
                    strncpy_s(iconPathBuf, filePath, sizeof(iconPathBuf) - 1);
                    config.game_overlay_icon_path = iconPathBuf;
                    OverlayConfig_MarkDirty();
                }
            }
            OverlayUI::EndSettingRow(row);
        }

        if (OverlayUI::SliderIntRow("Icon Width", &config.game_overlay_icon_width, 4, 512))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderIntRow("Icon Height", &config.game_overlay_icon_height, 4, 512))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("Icon Offset X", &config.game_overlay_icon_offset_x, -500.0f, 500.0f, "%.1f"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("Icon Offset Y", &config.game_overlay_icon_offset_y, -500.0f, 500.0f, "%.1f"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::InputIntRow("Icon Class (-1 = all)", &config.game_overlay_icon_class))
        {
            if (config.game_overlay_icon_class < -1) config.game_overlay_icon_class = -1;
            OverlayConfig_MarkDirty();
        }

        const char* anchors[] = { "center", "top", "bottom", "head" };
        int currentAnchor = 0;
        for (int i = 0; i < (int)(sizeof(anchors) / sizeof(anchors[0])); ++i)
        {
            if (config.game_overlay_icon_anchor == anchors[i])
            {
                currentAnchor = i;
                break;
            }
        }

        if (OverlayUI::ComboRow("Icon Anchor", &currentAnchor, anchors, IM_ARRAYSIZE(anchors)))
        {
            config.game_overlay_icon_anchor = anchors[currentAnchor];
            OverlayConfig_MarkDirty();
        }

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Enable Icon Overlay to edit settings.");
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Icon) && !g_iconLastError.empty())
    {
        if (OverlayUI::BeginSection("Errors", "game_overlay_section_errors"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
            ImGui::TextWrapped("%s", g_iconLastError.c_str());
            ImGui::PopStyleColor();
            OverlayUI::EndSection();
        }
    }

}

void draw_game_overlay_settings()
{
    draw_game_overlay_page(GameOverlaySettingsPage::All);
}

void draw_game_overlay_general()
{
    draw_game_overlay_page(GameOverlaySettingsPage::General);
}

void draw_game_overlay_visuals()
{
    draw_game_overlay_page(GameOverlaySettingsPage::Visuals);
}

void draw_game_overlay_icon()
{
    draw_game_overlay_page(GameOverlaySettingsPage::Icon);
}
