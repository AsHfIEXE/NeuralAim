#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <Windows.h>

#include <tchar.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <chrono>
#include <cmath>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "overlay.h"
#include "overlay/draw_settings.h"
#include "overlay/config_dirty.h"
#include "include/other_tools.h"
#include "config.h"
#include "keycodes.h"
#include "keyboard_listener.h"

#ifdef USE_CUDA
#include "trt_detector.h"
#endif

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3d11.lib")

ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
IDXGISwapChain1* g_pSwapChain = NULL;
IDCompositionDevice* g_dcompDevice = NULL;
IDCompositionTarget* g_dcompTarget = NULL;
IDCompositionVisual* g_dcompVisual = NULL;
ID3D11RenderTargetView* g_mainRenderTargetView = NULL;
HWND g_hwnd = NULL;

extern Config config;
extern std::mutex configMutex;
extern std::atomic<bool> shouldExit;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

ID3D11BlendState* g_pBlendState = nullptr;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

const int BASE_OVERLAY_WIDTH = 860;
const int BASE_OVERLAY_HEIGHT = 526;
static const int MIN_EDITOR_OPACITY = 252;

int overlayWidth = 0;
int overlayHeight = 0;

static const int DRAG_BAR_HEIGHT_PX = 34;
static const int MIN_OVERLAY_W = 560;
static const int MIN_OVERLAY_H = 340;
static const int RESIZE_BORDER_PX = 8;
static const int WORKAREA_MARGIN_PX = 20;

static bool g_autoResizeEnabled = true;
static ImGuiStyle g_baseStyle{};
static bool g_baseStyleReady = false;
static float g_runtimeUiScale = -1.0f;
static bool g_overlayVisible = false;
static bool g_renderingOverlayFrame = false;
static bool g_manualWindowDragActive = false;
static WPARAM g_manualWindowDragHit = HTNOWHERE;
static POINT g_manualWindowDragStartPoint{};
static RECT g_manualWindowDragStartRect{};
static int g_activeOverlayTab = 0;
static bool g_pendingOverlayGeometryDirty = false;

std::vector<std::string> availableModels;
std::vector<std::string> key_names;
std::vector<const char*> key_names_cstrs;

ID3D11ShaderResourceView* body_texture = nullptr;

static UINT GetDpiForWindowSafe(HWND hwnd);
static RECT GetOverlayWorkArea(HWND hwnd);
static void ClampOverlayToWorkArea(HWND hwnd, int& x, int& y, int& w, int& h);
static void EnsureOverlayInsideWorkArea(HWND hwnd, bool persistGeometry = false);
static bool StoreOverlayWindowGeometry(HWND hwnd, bool markDirty);
static bool ResizeOverlayBackBuffer(UINT width, UINT height);
static HRESULT RenderOverlayFrame(bool allowAutoResize, bool allowConfigSave);

void load_body_texture();
void release_body_texture();
std::vector<std::string> getAvailableModels();

static inline int ClampInt(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

struct OverlayThreadConfigSnapshot
{
    std::vector<std::string> buttonOpenOverlay;
    bool excludeFromCapture = true;
};

static OverlayThreadConfigSnapshot SnapshotOverlayThreadConfig()
{
    std::lock_guard<std::mutex> lock(configMutex);
    OverlayThreadConfigSnapshot snapshot;
    snapshot.buttonOpenOverlay = config.button_open_overlay;
    snapshot.excludeFromCapture = config.overlay_exclude_from_capture;
    return snapshot;
}

static float ComputeRuntimeUiScale()
{
    return std::clamp(config.overlay_ui_scale, 0.85f, 1.35f);
}

static void ApplyRuntimeUiScale()
{
    if (!g_baseStyleReady)
        return;

    const float targetScale = ComputeRuntimeUiScale();
    ImGuiStyle& style = ImGui::GetStyle();
    if (std::fabs(targetScale - g_runtimeUiScale) > 0.01f)
    {
        style = g_baseStyle;
        style.ScaleAllSizes(targetScale);
        g_runtimeUiScale = targetScale;
    }
    style.FontScaleMain = targetScale;
}

static void TryAutoResizeOverlay(float extraContentWidth)
{
    IM_UNUSED(extraContentWidth);
    if (!g_hwnd || !g_autoResizeEnabled)
        return;

    // Keep the editor size stable. Long model names and combo popups should clip/scroll,
    // not grow the overlay frame across the screen.
}

void Overlay_SetOpacity(int opacity255)
{
    if (!g_hwnd) return;

    opacity255 = ClampInt(opacity255, MIN_EDITOR_OPACITY, 255);

    LONG exStyle = GetWindowLong(g_hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0)
        SetWindowLong(g_hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)opacity255, LWA_ALPHA);
}

static void Overlay_SetDisplayAffinity(HWND hwnd, bool excludeFromCapture)
{
    if (!hwnd)
        return;

    const DWORD wanted = excludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    if (SetWindowDisplayAffinity(hwnd, wanted))
        return;

    if (excludeFromCapture)
    {
        const DWORD err = GetLastError();
        std::cerr << "[OverlayUI] SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed, err=" << err
                  << ". Trying WDA_MONITOR fallback." << std::endl;
        if (!SetWindowDisplayAffinity(hwnd, WDA_MONITOR))
        {
            std::cerr << "[OverlayUI] SetWindowDisplayAffinity(WDA_MONITOR) failed, err="
                      << GetLastError() << std::endl;
        }
    }
}

void Overlay_ApplyCaptureExclusion()
{
    Overlay_SetDisplayAffinity(g_hwnd, config.overlay_exclude_from_capture);
}

static inline ImVec4 RGBA(int r, int g, int b, int a = 255)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

static void ApplyTheme_Windows11Dark()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.48f;

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.PopupRounding = 5.0f;
    style.FrameRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ImageRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 8.0f;
    style.ScrollbarPadding = 2.0f;
    style.GrabMinSize = 12.0f;
    style.IndentSpacing = 18.0f;
    style.ColumnsMinSpacing = 10.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.SeparatorSize = 1.0f;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = style.Colors;

    const ImVec4 surfaceBase = RGBA(32, 32, 32, 248);
    const ImVec4 surfaceRaised = RGBA(39, 39, 39, 250);
    const ImVec4 control = RGBA(45, 45, 45, 255);
    const ImVec4 controlHover = RGBA(55, 55, 55, 255);
    const ImVec4 controlActive = RGBA(65, 65, 65, 255);
    const ImVec4 stroke = RGBA(82, 82, 82, 150);
    const ImVec4 strokeHi = RGBA(112, 112, 112, 190);
    const ImVec4 accent = RGBA(96, 205, 255, 245);
    const ImVec4 accentActive = RGBA(0, 120, 212, 255);
    const ImVec4 accentSoft = RGBA(0, 120, 212, 92);

    const ImVec4 text = RGBA(245, 245, 245, 255);
    const ImVec4 textDim = RGBA(188, 188, 188, 255);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;

    c[ImGuiCol_WindowBg] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = RGBA(38, 39, 43, 255);

    c[ImGuiCol_Border] = stroke;
    c[ImGuiCol_BorderShadow] = RGBA(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = control;
    c[ImGuiCol_FrameBgHovered] = controlHover;
    c[ImGuiCol_FrameBgActive] = controlActive;

    c[ImGuiCol_TitleBg] = surfaceRaised;
    c[ImGuiCol_TitleBgActive] = surfaceRaised;
    c[ImGuiCol_TitleBgCollapsed] = surfaceBase;
    c[ImGuiCol_MenuBarBg] = surfaceRaised;

    c[ImGuiCol_ScrollbarBg] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = RGBA(112, 112, 112, 100);
    c[ImGuiCol_ScrollbarGrabHovered] = RGBA(132, 132, 132, 155);
    c[ImGuiCol_ScrollbarGrabActive] = accent;

    c[ImGuiCol_CheckMark] = RGBA(255, 255, 255, 255);
    c[ImGuiCol_CheckboxSelectedBg] = accentActive;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = RGBA(122, 214, 255, 255);

    c[ImGuiCol_Button] = control;
    c[ImGuiCol_ButtonHovered] = controlHover;
    c[ImGuiCol_ButtonActive] = controlActive;

    c[ImGuiCol_Header] = RGBA(38, 39, 43, 230);
    c[ImGuiCol_HeaderHovered] = RGBA(43, 45, 50, 238);
    c[ImGuiCol_HeaderActive] = accentSoft;

    c[ImGuiCol_Separator] = stroke;
    c[ImGuiCol_SeparatorHovered] = strokeHi;
    c[ImGuiCol_SeparatorActive] = accent;

    c[ImGuiCol_Tab] = RGBA(43, 43, 43, 245);
    c[ImGuiCol_TabHovered] = RGBA(55, 55, 55, 255);
    c[ImGuiCol_TabSelected] = RGBA(62, 62, 62, 255);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = RGBA(36, 36, 36, 235);
    c[ImGuiCol_TabDimmedSelected] = RGBA(50, 50, 50, 245);
    c[ImGuiCol_TabDimmedSelectedOverline] = RGBA(96, 205, 255, 170);

    c[ImGuiCol_ResizeGrip] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = RGBA(0, 0, 0, 0);

    c[ImGuiCol_InputTextCursor] = accent;
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = RGBA(122, 214, 255, 255);
    c[ImGuiCol_PlotHistogram] = RGBA(252, 225, 115, 255);
    c[ImGuiCol_PlotHistogramHovered] = RGBA(255, 235, 150, 255);

    c[ImGuiCol_TableHeaderBg] = surfaceRaised;
    c[ImGuiCol_TableBorderStrong] = stroke;
    c[ImGuiCol_TableBorderLight] = RGBA(70, 70, 70, 115);
    c[ImGuiCol_TableRowBg] = RGBA(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = RGBA(255, 255, 255, 10);

    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_NavWindowingHighlight] = accentSoft;
    c[ImGuiCol_NavWindowingDimBg] = RGBA(0, 0, 0, 120);

    c[ImGuiCol_TextLink] = accent;
    c[ImGuiCol_TextSelectedBg] = RGBA(0, 120, 212, 120);
    c[ImGuiCol_TreeLines] = RGBA(100, 100, 100, 115);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_DragDropTargetBg] = RGBA(0, 120, 212, 70);
    c[ImGuiCol_UnsavedMarker] = RGBA(252, 225, 115, 255);
    c[ImGuiCol_ModalWindowDimBg] = RGBA(0, 0, 0, 140);
}

enum class SidebarIconKind
{
    Camera,
    Chip,
    Layers,
    Crosshair,
    Move,
    Curve,
    Spark,
    User,
    Mouse,
    Keyboard,
    Sliders,
    Monitor,
    Palette,
    Image,
    Bars,
    Debug
};

struct OverlayTabItem
{
    const char* label;
    const char* group;
    const char* description;
    void (*draw)();
    SidebarIconKind icon;
};

static const OverlayTabItem kOverlayTabs[] = {
    { "Capture",       "Vision",  "Frame source, monitor/window selection and preview.", draw_capture_settings,        SidebarIconKind::Camera },
    { "AI Model",      "Vision",  "Model, backend and detector thresholds.",             draw_ai,                      SidebarIconKind::Chip },
    { "Depth",         "Vision",  "Depth inference, masks and depth debug overlay.",     draw_depth,                   SidebarIconKind::Layers },

    { "Target",        "Aim",     "Target selection and aim point offsets.",             draw_target,                  SidebarIconKind::Crosshair },
    { "Tracker",       "Aim",     "Current target identity lock status.",                draw_tracker,                 SidebarIconKind::Crosshair },
    { "Movement",      "Aim",     "FOV, speed, target correction and motion profile.",   draw_mouse_movement,          SidebarIconKind::Move },
    { "Prediction",    "Aim",     "Prediction points and Kalman filter tuning.",         draw_mouse_prediction,        SidebarIconKind::Curve },
    { "Assist",        "Aim",     "Auto shoot, recoil compensation and assist toggles.", draw_mouse_assist,            SidebarIconKind::Spark },
    { "Profiles",      "Aim",     "Per-game sensitivity and profile management.",        draw_mouse_profiles,          SidebarIconKind::User },

    { "Input Device",  "Control", "Mouse backend, device connection and reconnect data.",draw_mouse_input,             SidebarIconKind::Mouse },
    { "Hotkeys",       "Control", "Bindings for aiming, shooting and runtime actions.",  draw_buttons,                 SidebarIconKind::Keyboard },
    { "Overlay",       "Control", "Overlay appearance and privacy options.",             draw_overlay,                 SidebarIconKind::Sliders },

    { "Game Render",   "Visuals", "In-game overlay lifetime, FPS and render toggles.",   draw_game_overlay_general,    SidebarIconKind::Monitor },
    { "Render Style",  "Visuals", "Boxes, capture frame and future point styling.",      draw_game_overlay_visuals,    SidebarIconKind::Palette },
    { "Icon Overlay",  "Visuals", "Per-target icon image, size, anchor and class filter.",draw_game_overlay_icon,      SidebarIconKind::Image },

    { "Stats",         "Monitor", "Performance, capture source and timing graphs.",      draw_stats,                   SidebarIconKind::Bars },
    { "Debug",         "Monitor", "Screenshots, data collection and diagnostics.",        draw_debug,                   SidebarIconKind::Debug },
};

static void DrawMainPanelBackground(const ImVec2& pos, const ImVec2& size)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    draw->AddRectFilled(pos, max, IM_COL32(29, 30, 33, 252), 10.0f);
    draw->AddRectFilledMultiColor(
        ImVec2(pos.x + 1.0f, pos.y + 1.0f),
        ImVec2(max.x - 1.0f, max.y - 1.0f),
        IM_COL32(42, 45, 52, 80),
        IM_COL32(32, 33, 36, 34),
        IM_COL32(24, 25, 27, 60),
        IM_COL32(35, 38, 45, 58));
    draw->AddRect(pos, max, IM_COL32(92, 92, 92, 128), 10.0f, 0, 1.0f);
}

static void DrawSidebarTitle()
{
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

static void DrawSidebarIcon(ImDrawList* draw, SidebarIconKind icon, const char* group, const ImVec2& pos, bool selected)
{
    const ImU32 color = selected ? IM_COL32(96, 205, 255, 255) :
        (std::strcmp(group, "Vision") == 0 ? IM_COL32(84, 182, 255, 230) :
         std::strcmp(group, "Aim") == 0 ? IM_COL32(255, 189, 92, 230) :
         std::strcmp(group, "Control") == 0 ? IM_COL32(71, 214, 190, 230) :
         std::strcmp(group, "Visuals") == 0 ? IM_COL32(178, 143, 255, 230) :
         IM_COL32(205, 213, 224, 230));

    const float x = pos.x;
    const float y = pos.y;
    const float s = 18.0f;
    const ImVec2 c(x + s * 0.5f, y + s * 0.5f);
    const float stroke = 1.8f;
    const ImU32 soft = (color & IM_COL32_A_MASK) ? (color & 0x88FFFFFFu) : color;

    switch (icon)
    {
    case SidebarIconKind::Camera:
        draw->AddRect(ImVec2(x + 3.0f, y + 5.5f), ImVec2(x + 15.0f, y + 13.5f), color, 2.5f, 0, stroke);
        draw->AddRectFilled(ImVec2(x + 6.0f, y + 3.8f), ImVec2(x + 10.0f, y + 5.8f), color, 1.0f);
        draw->AddCircle(ImVec2(c.x + 1.0f, c.y + 0.5f), 2.4f, color, 20, stroke);
        break;
    case SidebarIconKind::Chip:
        draw->AddRect(ImVec2(x + 4.5f, y + 4.5f), ImVec2(x + 13.5f, y + 13.5f), color, 2.0f, 0, stroke);
        draw->AddCircleFilled(c, 2.0f, soft, 16);
        for (int i = 0; i < 3; ++i)
        {
            const float p = y + 5.5f + i * 3.5f;
            draw->AddLine(ImVec2(x + 2.0f, p), ImVec2(x + 4.5f, p), color, stroke);
            draw->AddLine(ImVec2(x + 13.5f, p), ImVec2(x + 16.0f, p), color, stroke);
        }
        break;
    case SidebarIconKind::Layers:
    {
        const ImVec2 points[] = { ImVec2(c.x, y + 3.0f), ImVec2(x + 15.0f, y + 7.0f), ImVec2(c.x, y + 11.0f), ImVec2(x + 3.0f, y + 7.0f) };
        draw->AddPolyline(points, 4, color, ImDrawFlags_Closed, stroke);
        draw->AddLine(ImVec2(x + 4.0f, y + 11.5f), ImVec2(c.x, y + 15.0f), color, stroke);
        draw->AddLine(ImVec2(x + 14.0f, y + 11.5f), ImVec2(c.x, y + 15.0f), color, stroke);
        break;
    }
    case SidebarIconKind::Crosshair:
        draw->AddCircle(c, 5.8f, color, 28, stroke);
        draw->AddLine(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.0f, c.y), color, stroke);
        draw->AddLine(ImVec2(c.x + 4.0f, c.y), ImVec2(c.x + 8.0f, c.y), color, stroke);
        draw->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y - 4.0f), color, stroke);
        draw->AddLine(ImVec2(c.x, c.y + 4.0f), ImVec2(c.x, c.y + 8.0f), color, stroke);
        break;
    case SidebarIconKind::Move:
        draw->AddLine(ImVec2(c.x, y + 3.0f), ImVec2(c.x, y + 15.0f), color, stroke);
        draw->AddLine(ImVec2(x + 3.0f, c.y), ImVec2(x + 15.0f, c.y), color, stroke);
        draw->AddTriangleFilled(ImVec2(c.x, y + 2.0f), ImVec2(c.x - 2.5f, y + 5.0f), ImVec2(c.x + 2.5f, y + 5.0f), color);
        draw->AddTriangleFilled(ImVec2(c.x, y + 16.0f), ImVec2(c.x - 2.5f, y + 13.0f), ImVec2(c.x + 2.5f, y + 13.0f), color);
        break;
    case SidebarIconKind::Curve:
        draw->AddBezierCubic(ImVec2(x + 3.0f, y + 13.5f), ImVec2(x + 6.5f, y + 5.0f), ImVec2(x + 11.5f, y + 14.0f), ImVec2(x + 15.0f, y + 4.0f), color, stroke, 18);
        draw->AddCircleFilled(ImVec2(x + 15.0f, y + 4.0f), 2.0f, color, 12);
        break;
    case SidebarIconKind::Spark:
        draw->AddLine(ImVec2(c.x, y + 3.0f), ImVec2(c.x, y + 15.0f), color, stroke);
        draw->AddLine(ImVec2(x + 3.0f, c.y), ImVec2(x + 15.0f, c.y), color, stroke);
        draw->AddLine(ImVec2(x + 5.0f, y + 5.0f), ImVec2(x + 13.0f, y + 13.0f), color, stroke);
        draw->AddLine(ImVec2(x + 13.0f, y + 5.0f), ImVec2(x + 5.0f, y + 13.0f), color, stroke);
        break;
    case SidebarIconKind::User:
        draw->AddCircle(ImVec2(c.x, y + 6.0f), 3.0f, color, 18, stroke);
        draw->AddBezierCubic(ImVec2(x + 4.0f, y + 15.0f), ImVec2(x + 5.0f, y + 10.5f), ImVec2(x + 13.0f, y + 10.5f), ImVec2(x + 14.0f, y + 15.0f), color, stroke, 14);
        break;
    case SidebarIconKind::Mouse:
        draw->AddRect(ImVec2(x + 5.0f, y + 2.5f), ImVec2(x + 13.0f, y + 15.5f), color, 4.0f, 0, stroke);
        draw->AddLine(ImVec2(c.x, y + 3.0f), ImVec2(c.x, y + 7.0f), color, stroke);
        break;
    case SidebarIconKind::Keyboard:
        draw->AddRect(ImVec2(x + 2.5f, y + 5.0f), ImVec2(x + 15.5f, y + 13.0f), color, 2.0f, 0, stroke);
        for (int i = 0; i < 3; ++i)
            draw->AddCircleFilled(ImVec2(x + 5.0f + i * 4.0f, y + 8.0f), 0.9f, color, 8);
        draw->AddLine(ImVec2(x + 5.0f, y + 11.0f), ImVec2(x + 13.0f, y + 11.0f), color, stroke);
        break;
    case SidebarIconKind::Sliders:
        for (int i = 0; i < 3; ++i)
        {
            const float yy = y + 5.0f + i * 4.0f;
            draw->AddLine(ImVec2(x + 3.0f, yy), ImVec2(x + 15.0f, yy), color, stroke);
            draw->AddCircleFilled(ImVec2(x + 6.0f + i * 3.0f, yy), 1.8f, color, 14);
        }
        break;
    case SidebarIconKind::Monitor:
        draw->AddRect(ImVec2(x + 3.0f, y + 4.0f), ImVec2(x + 15.0f, y + 12.0f), color, 2.0f, 0, stroke);
        draw->AddLine(ImVec2(c.x, y + 12.0f), ImVec2(c.x, y + 15.0f), color, stroke);
        draw->AddLine(ImVec2(x + 6.0f, y + 15.0f), ImVec2(x + 12.0f, y + 15.0f), color, stroke);
        break;
    case SidebarIconKind::Palette:
        draw->AddCircle(c, 6.5f, color, 24, stroke);
        draw->AddCircleFilled(ImVec2(x + 6.0f, y + 7.0f), 1.1f, color, 8);
        draw->AddCircleFilled(ImVec2(x + 9.0f, y + 5.5f), 1.1f, color, 8);
        draw->AddCircleFilled(ImVec2(x + 12.0f, y + 8.0f), 1.1f, color, 8);
        break;
    case SidebarIconKind::Image:
    {
        draw->AddRect(ImVec2(x + 3.0f, y + 4.0f), ImVec2(x + 15.0f, y + 14.0f), color, 2.0f, 0, stroke);
        draw->AddCircleFilled(ImVec2(x + 6.0f, y + 7.0f), 1.3f, color, 10);
        const ImVec2 points[] = { ImVec2(x + 4.0f, y + 13.0f), ImVec2(x + 8.0f, y + 9.0f), ImVec2(x + 11.0f, y + 12.0f), ImVec2(x + 14.0f, y + 9.0f) };
        draw->AddPolyline(points, 4, color, 0, stroke);
        break;
    }
    case SidebarIconKind::Bars:
        draw->AddRectFilled(ImVec2(x + 4.0f, y + 10.0f), ImVec2(x + 6.5f, y + 15.0f), color, 1.0f);
        draw->AddRectFilled(ImVec2(x + 8.0f, y + 6.5f), ImVec2(x + 10.5f, y + 15.0f), color, 1.0f);
        draw->AddRectFilled(ImVec2(x + 12.0f, y + 3.5f), ImVec2(x + 14.5f, y + 15.0f), color, 1.0f);
        break;
    case SidebarIconKind::Debug:
        draw->AddRect(ImVec2(x + 5.0f, y + 5.0f), ImVec2(x + 13.0f, y + 13.5f), color, 3.0f, 0, stroke);
        draw->AddLine(ImVec2(x + 3.0f, y + 8.0f), ImVec2(x + 15.0f, y + 8.0f), color, stroke);
        draw->AddLine(ImVec2(x + 3.0f, y + 11.5f), ImVec2(x + 15.0f, y + 11.5f), color, stroke);
        break;
    }
}

static bool DrawSidebarTabButton(const OverlayTabItem& tab, bool selected)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + style.ItemSpacing.y * 0.22f);
    if (size.x < 1.0f)
        size.x = 1.0f;

    const std::string id = std::string("##nav_") + tab.label;
    const bool pressed = ImGui::InvisibleButton(id.c_str(), size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    ImU32 rowBg = IM_COL32(0, 0, 0, 0);
    if (selected)
        rowBg = IM_COL32(54, 55, 60, 230);
    else if (hovered)
        rowBg = IM_COL32(43, 44, 49, 212);

    if (selected || hovered)
        draw->AddRectFilled(pos, max, rowBg, 5.0f);
    if (selected)
    {
        const float markerY0 = pos.y + 7.0f;
        const float markerY1 = max.y - 7.0f;
        draw->AddRectFilled(ImVec2(pos.x + 3.0f, markerY0), ImVec2(pos.x + 6.0f, markerY1), IM_COL32(96, 205, 255, 255), 3.0f);
        draw->AddRect(pos, max, IM_COL32(255, 255, 255, 18), 5.0f, 0, 1.0f);
    }

    const float textY = pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    const ImU32 textCol = selected ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(238, 238, 238, 255) : IM_COL32(202, 202, 202, 240));
    DrawSidebarIcon(draw, tab.icon, tab.group, ImVec2(pos.x + style.FramePadding.x + 4.0f, pos.y + (size.y - 18.0f) * 0.5f), selected);
    draw->AddText(ImVec2(pos.x + style.FramePadding.x + 31.0f, textY), textCol, tab.label);

    return pressed;
}

static UINT GetDpiForWindowSafe(HWND hwnd)
{
    UINT dpi = 96;
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        auto pGetDpiForWindow = (UINT(WINAPI*)(HWND))::GetProcAddress(user32, "GetDpiForWindow");
        if (pGetDpiForWindow)
            dpi = pGetDpiForWindow(hwnd);
    }
    return dpi;
}

static RECT GetOverlayWorkArea(HWND hwnd)
{
    RECT work{};
    HMONITOR monitor = nullptr;

    if (hwnd)
    {
        monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        POINT pt{};
        ::GetCursorPos(&pt);
        monitor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && ::GetMonitorInfo(monitor, &mi))
        return mi.rcWork;

    work.left = 0;
    work.top = 0;
    work.right = ::GetSystemMetrics(SM_CXSCREEN);
    work.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    return work;
}

static RECT GetOverlayWorkAreaForRect(const RECT& rect)
{
    HMONITOR monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && ::GetMonitorInfo(monitor, &mi))
        return mi.rcWork;

    return GetOverlayWorkArea(nullptr);
}

static void ClampOverlayToWorkArea(HWND hwnd, int& x, int& y, int& w, int& h)
{
    const RECT desiredRect = {
        x,
        y,
        x + OtherTools::MaxInt(1, w),
        y + OtherTools::MaxInt(1, h)
    };
    const RECT work = hwnd ? GetOverlayWorkArea(hwnd) : GetOverlayWorkAreaForRect(desiredRect);
    const UINT dpi = hwnd ? GetDpiForWindowSafe(hwnd) : 96;

    const int minW = ::MulDiv(MIN_OVERLAY_W, (int)dpi, 96);
    const int minH = ::MulDiv(MIN_OVERLAY_H, (int)dpi, 96);

    const int workW = OtherTools::MaxInt(1, static_cast<int>(work.right - work.left - WORKAREA_MARGIN_PX));
    const int workH = OtherTools::MaxInt(1, static_cast<int>(work.bottom - work.top - WORKAREA_MARGIN_PX));

    const int maxW = OtherTools::MaxInt(minW, workW);
    const int maxH = OtherTools::MaxInt(minH, workH);

    w = ClampInt(w, minW, maxW);
    h = ClampInt(h, minH, maxH);

    const int maxX = OtherTools::MaxInt(static_cast<int>(work.left), static_cast<int>(work.right - w));
    const int maxY = OtherTools::MaxInt(static_cast<int>(work.top), static_cast<int>(work.bottom - h));
    x = ClampInt(x, static_cast<int>(work.left), maxX);
    y = ClampInt(y, static_cast<int>(work.top), maxY);
}

static void MarkOverlayGeometryDirty()
{
    if (ImGui::GetCurrentContext())
    {
        OverlayConfig_MarkDirty();
    }
    else
    {
        g_pendingOverlayGeometryDirty = true;
    }
}

static bool StoreOverlayWindowGeometry(HWND hwnd, bool markDirty)
{
    if (!hwnd)
        return false;

    RECT wndRect{};
    if (!::GetWindowRect(hwnd, &wndRect))
        return false;

    const int x = wndRect.left;
    const int y = wndRect.top;
    const int w = wndRect.right - wndRect.left;
    const int h = wndRect.bottom - wndRect.top;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        changed = config.overlay_x != x ||
                  config.overlay_y != y ||
                  config.overlay_width != w ||
                  config.overlay_height != h;

        if (changed)
        {
            config.overlay_x = x;
            config.overlay_y = y;
            config.overlay_width = w;
            config.overlay_height = h;
        }
    }

    if (changed && markDirty)
        MarkOverlayGeometryDirty();

    return changed;
}

static void EnsureOverlayInsideWorkArea(HWND hwnd, bool persistGeometry)
{
    if (!hwnd)
        return;

    RECT wndRect{};
    ::GetWindowRect(hwnd, &wndRect);

    const int oldW = overlayWidth;
    const int oldH = overlayHeight;

    int x = wndRect.left;
    int y = wndRect.top;
    int w = overlayWidth;
    int h = overlayHeight;
    ClampOverlayToWorkArea(hwnd, x, y, w, h);

    overlayWidth = w;
    overlayHeight = h;

    if (x != wndRect.left || y != wndRect.top || w != oldW || h != oldH)
        ::SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);

    if (persistGeometry)
        StoreOverlayWindowGeometry(hwnd, true);
}

bool InitializeBlendState()
{
    D3D11_BLEND_DESC blendDesc;
    ZeroMemory(&blendDesc, sizeof(blendDesc));

    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = g_pd3dDevice->CreateBlendState(&blendDesc, &g_pBlendState);
    if (FAILED(hr))
        return false;

    float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    g_pd3dDeviceContext->OMSetBlendState(g_pBlendState, blendFactor, 0xffffffff);
    return true;
}

bool CreateDeviceD3D(HWND hWnd)
{
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        ARRAYSIZE(featureLevelArray),
        D3D11_SDK_VERSION,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (FAILED(hr))
        return false;

    IDXGIDevice* dxgiDev = nullptr;
    hr = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDev));
    if (FAILED(hr) || !dxgiDev)
        return false;

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDev->GetAdapter(&adapter);
    if (FAILED(hr) || !adapter)
    {
        dxgiDev->Release();
        return false;
    }

    IDXGIFactory2* factory2 = nullptr;
    {
        IDXGIFactory* baseFactory = nullptr;
        hr = adapter->GetParent(IID_PPV_ARGS(&baseFactory));
        if (FAILED(hr) || !baseFactory)
        {
            adapter->Release();
            dxgiDev->Release();
            return false;
        }
        hr = baseFactory->QueryInterface(IID_PPV_ARGS(&factory2));
        baseFactory->Release();
    }

    if (FAILED(hr) || !factory2)
    {
        adapter->Release();
        dxgiDev->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = overlayWidth;
    scd.Height = overlayHeight;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scd.Scaling = DXGI_SCALING_STRETCH;

    hr = factory2->CreateSwapChainForComposition(
        g_pd3dDevice,
        &scd,
        nullptr,
        &g_pSwapChain);

    factory2->Release();
    adapter->Release();

    if (FAILED(hr) || !g_pSwapChain)
    {
        dxgiDev->Release();
        return false;
    }

    hr = DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&g_dcompDevice));
    dxgiDev->Release();
    if (FAILED(hr) || !g_dcompDevice)
        return false;

    hr = g_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &g_dcompTarget);
    if (FAILED(hr) || !g_dcompTarget)
        return false;

    hr = g_dcompDevice->CreateVisual(&g_dcompVisual);
    if (FAILED(hr) || !g_dcompVisual)
        return false;

    hr = g_dcompVisual->SetContent(g_pSwapChain);
    if (FAILED(hr))
        return false;

    hr = g_dcompTarget->SetRoot(g_dcompVisual);
    if (FAILED(hr))
        return false;

    g_dcompDevice->Commit();

    if (!InitializeBlendState())
        return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = NULL;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_pd3dDeviceContext)
    {
        ID3D11RenderTargetView* nullRenderTarget = nullptr;
        g_pd3dDeviceContext->OMSetRenderTargets(1, &nullRenderTarget, NULL);
    }
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

static bool ResizeOverlayBackBuffer(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return false;

    overlayWidth = static_cast<int>(width);
    overlayHeight = static_cast<int>(height);

    if (!g_pd3dDevice || !g_pSwapChain)
        return true;

    CleanupRenderTarget();
    const HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    if (g_dcompDevice)
        g_dcompDevice->Commit();

    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = NULL; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = NULL; }
    if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = NULL; }

    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pBlendState) { g_pBlendState->Release(); g_pBlendState = nullptr; }
}

static bool IsOverlayResizeHit(WPARAM hit)
{
    return hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM ||
           hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT;
}

static bool IsOverlayMoveOrResizeHit(WPARAM hit)
{
    return hit == HTCAPTION || IsOverlayResizeHit(hit);
}

static void BeginManualOverlayWindowDrag(HWND hwnd, WPARAM hit)
{
    if (!hwnd || !IsOverlayMoveOrResizeHit(hit))
        return;

    g_manualWindowDragActive = true;
    g_manualWindowDragHit = hit;
    ::GetCursorPos(&g_manualWindowDragStartPoint);
    ::GetWindowRect(hwnd, &g_manualWindowDragStartRect);
    ::SetCapture(hwnd);

    if (IsOverlayResizeHit(hit))
        g_autoResizeEnabled = false;
}

static void EndManualOverlayWindowDrag(HWND hwnd)
{
    if (!g_manualWindowDragActive)
        return;

    g_manualWindowDragActive = false;
    g_manualWindowDragHit = HTNOWHERE;

    if (::GetCapture() == hwnd)
        ::ReleaseCapture();

    EnsureOverlayInsideWorkArea(hwnd, true);
}

static void UpdateManualOverlayWindowDrag(HWND hwnd)
{
    if (!hwnd || !g_manualWindowDragActive)
        return;

    POINT pt{};
    ::GetCursorPos(&pt);

    const int dx = pt.x - g_manualWindowDragStartPoint.x;
    const int dy = pt.y - g_manualWindowDragStartPoint.y;
    const int startW = g_manualWindowDragStartRect.right - g_manualWindowDragStartRect.left;
    const int startH = g_manualWindowDragStartRect.bottom - g_manualWindowDragStartRect.top;

    int x = g_manualWindowDragStartRect.left;
    int y = g_manualWindowDragStartRect.top;
    int w = startW;
    int h = startH;

    if (g_manualWindowDragHit == HTCAPTION)
    {
        x += dx;
        y += dy;
        ClampOverlayToWorkArea(hwnd, x, y, w, h);
    }
    else
    {
        const UINT dpi = GetDpiForWindowSafe(hwnd);
        const int minW = ::MulDiv(MIN_OVERLAY_W, (int)dpi, 96);
        const int minH = ::MulDiv(MIN_OVERLAY_H, (int)dpi, 96);
        const RECT work = GetOverlayWorkArea(hwnd);
        const int maxW = OtherTools::MaxInt(minW, static_cast<int>((work.right - work.left) - WORKAREA_MARGIN_PX));
        const int maxH = OtherTools::MaxInt(minH, static_cast<int>((work.bottom - work.top) - WORKAREA_MARGIN_PX));

        const bool left = g_manualWindowDragHit == HTLEFT ||
                          g_manualWindowDragHit == HTTOPLEFT ||
                          g_manualWindowDragHit == HTBOTTOMLEFT;
        const bool right = g_manualWindowDragHit == HTRIGHT ||
                           g_manualWindowDragHit == HTTOPRIGHT ||
                           g_manualWindowDragHit == HTBOTTOMRIGHT;
        const bool top = g_manualWindowDragHit == HTTOP ||
                         g_manualWindowDragHit == HTTOPLEFT ||
                         g_manualWindowDragHit == HTTOPRIGHT;
        const bool bottom = g_manualWindowDragHit == HTBOTTOM ||
                            g_manualWindowDragHit == HTBOTTOMLEFT ||
                            g_manualWindowDragHit == HTBOTTOMRIGHT;

        if (left)
        {
            w = ClampInt(startW - dx, minW, maxW);
            x = g_manualWindowDragStartRect.right - w;
        }
        else if (right)
        {
            w = ClampInt(startW + dx, minW, maxW);
        }

        if (top)
        {
            h = ClampInt(startH - dy, minH, maxH);
            y = g_manualWindowDragStartRect.bottom - h;
        }
        else if (bottom)
        {
            h = ClampInt(startH + dy, minH, maxH);
        }

        ClampOverlayToWorkArea(hwnd, x, y, w, h);
    }

    ::SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_NCHITTEST:
        {
            POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
            ::ScreenToClient(hWnd, &pt);

            RECT rc;
            ::GetClientRect(hWnd, &rc);

            const UINT dpi = GetDpiForWindowSafe(hWnd);
            const int border = ::MulDiv(RESIZE_BORDER_PX, (int)dpi, 96);
            const bool left = pt.x < rc.left + border;
            const bool right = pt.x >= rc.right - border;
            const bool top = pt.y < rc.top + border;
            const bool bottom = pt.y >= rc.bottom - border;

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;

            if (pt.y >= rc.top && pt.y < rc.top + DRAG_BAR_HEIGHT_PX)
                return HTCAPTION;

            return HTCLIENT;
        }
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = GetDpiForWindowSafe(hWnd);
            const int minW = ::MulDiv(MIN_OVERLAY_W, (int)dpi, 96);
            const int minH = ::MulDiv(MIN_OVERLAY_H, (int)dpi, 96);
            const RECT work = GetOverlayWorkArea(hWnd);
            const int maxW = OtherTools::MaxInt(minW, static_cast<int>((work.right - work.left) - WORKAREA_MARGIN_PX));
            const int maxH = OtherTools::MaxInt(minH, static_cast<int>((work.bottom - work.top) - WORKAREA_MARGIN_PX));
            mmi->ptMinTrackSize.x = minW;
            mmi->ptMinTrackSize.y = minH;
            if (maxW > 0) mmi->ptMaxTrackSize.x = maxW;
            if (maxH > 0) mmi->ptMaxTrackSize.y = maxH;
            return 0;
        }
        case WM_NCLBUTTONDOWN:
            if (IsOverlayMoveOrResizeHit(wParam))
            {
                BeginManualOverlayWindowDrag(hWnd, wParam);
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (g_manualWindowDragActive)
            {
                UpdateManualOverlayWindowDrag(hWnd);
                return 0;
            }
            break;

        case WM_LBUTTONUP:
        case WM_NCLBUTTONUP:
            if (g_manualWindowDragActive)
            {
                EndManualOverlayWindowDrag(hWnd);
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            if (g_manualWindowDragActive)
            {
                g_manualWindowDragActive = false;
                g_manualWindowDragHit = HTNOWHERE;
                EnsureOverlayInsideWorkArea(hWnd, true);
                return 0;
            }
            break;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_EXITSIZEMOVE:
        g_autoResizeEnabled = false;
        EnsureOverlayInsideWorkArea(hWnd, true);
        return 0;

    case WM_DISPLAYCHANGE:
        EnsureOverlayInsideWorkArea(hWnd, true);
        return 0;

    case WM_DPICHANGED:
        EnsureOverlayInsideWorkArea(hWnd, true);
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            const UINT width = (UINT)LOWORD(lParam);
            const UINT height = (UINT)HIWORD(lParam);

            if (ResizeOverlayBackBuffer(width, height) && g_overlayVisible)
                RenderOverlayFrame(false, false);
        }
        return 0;

    case WM_DESTROY:
        shouldExit = true;
        ::PostQuitMessage(0);
        return 0;

    default:
        return ::DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

void SetupImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontScaleMain = 1.0f;
    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 16.5f, &fontConfig) &&
        !io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\SegUIVar.ttf", 16.5f, &fontConfig) &&
        !io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.5f, &fontConfig))
    {
        io.Fonts->AddFontDefault();
    }

    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ApplyTheme_Windows11Dark();
    g_baseStyle = ImGui::GetStyle();
    g_baseStyleReady = true;
    g_runtimeUiScale = -1.0f;
    load_body_texture();

    if (g_pendingOverlayGeometryDirty)
    {
        g_pendingOverlayGeometryDirty = false;
        OverlayConfig_MarkDirty();
    }
}

bool CreateOverlayWindow()
{
    int overlayX = config.overlay_x;
    int overlayY = config.overlay_y;
    overlayWidth = config.overlay_width > 0 ? config.overlay_width : BASE_OVERLAY_WIDTH;
    overlayHeight = config.overlay_height > 0 ? config.overlay_height : BASE_OVERLAY_HEIGHT;

    {
        int x = overlayX;
        int y = overlayY;
        int w = overlayWidth;
        int h = overlayHeight;
        ClampOverlayToWorkArea(nullptr, x, y, w, h);
        overlayX = x;
        overlayY = y;
        overlayWidth = w;
        overlayHeight = h;
    }

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        GetModuleHandle(NULL),
        NULL,
        NULL,
        NULL,
        NULL,
        _T("Chrome"),
        NULL
    };
    ::RegisterClassEx(&wc);

    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    const DWORD style = WS_POPUP;

    RECT wr = { overlayX, overlayY, overlayX + overlayWidth, overlayY + overlayHeight };
    ::AdjustWindowRectEx(&wr, style, FALSE, exStyle);

    const int wndW = wr.right - wr.left;
    const int wndH = wr.bottom - wr.top;

    g_hwnd = ::CreateWindowEx(
        exStyle,
        wc.lpszClassName, _T("Chrome"),
        style,
        wr.left, wr.top, wndW, wndH,
        NULL, NULL, wc.hInstance, NULL);

    if (g_hwnd == NULL)
        return false;

    EnsureOverlayInsideWorkArea(g_hwnd, true);

    BOOL dwm = FALSE;
    if (SUCCEEDED(DwmIsCompositionEnabled(&dwm)) && dwm)
    {
        MARGINS m = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(g_hwnd, &m);
    }

    if (config.overlay_opacity < MIN_EDITOR_OPACITY)  config.overlay_opacity = MIN_EDITOR_OPACITY;
    if (config.overlay_opacity >= 256) config.overlay_opacity = 255;

    Overlay_SetOpacity(config.overlay_opacity);

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    Overlay_ApplyCaptureExclusion();

    return true;
}

static HRESULT RenderOverlayFrame(bool allowAutoResize, bool allowConfigSave)
{
    if (!g_overlayVisible || !g_pSwapChain || !g_pd3dDeviceContext || !g_mainRenderTargetView ||
        !ImGui::GetCurrentContext() || g_renderingOverlayFrame)
    {
        return S_FALSE;
    }

    const float w = static_cast<float>(overlayWidth);
    const float h = static_cast<float>(overlayHeight);
    if (w <= 0.0f || h <= 0.0f)
        return S_FALSE;

    g_renderingOverlayFrame = true;

    ApplyRuntimeUiScale();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const float sidebarWidth = std::clamp(w * 0.27f, 216.0f, 224.0f);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##editor_root", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    DrawMainPanelBackground(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    {
        std::lock_guard<std::mutex> lock(configMutex);

        const int tabCount = static_cast<int>(sizeof(kOverlayTabs) / sizeof(kOverlayTabs[0]));
        if (g_activeOverlayTab < 0 || g_activeOverlayTab >= tabCount)
            g_activeOverlayTab = 0;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##options_nav", ImVec2(sidebarWidth, 0.0f),
            ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar);

        DrawSidebarTitle();

        const char* lastGroup = nullptr;
        for (int i = 0; i < tabCount; ++i)
        {
            const char* group = kOverlayTabs[i].group;
            if (!lastGroup || std::strcmp(lastGroup, group) != 0)
            {
                if (lastGroup)
                    ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(177, 177, 177, 230));
                ImGui::TextUnformatted(group);
                ImGui::PopStyleColor();
            }
            if (DrawSidebarTabButton(kOverlayTabs[i], g_activeOverlayTab == i))
                g_activeOverlayTab = i;
            lastGroup = group;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 12.0f);

        float contentExtraW = 0.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##options_content", ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        kOverlayTabs[g_activeOverlayTab].draw();

        contentExtraW = ImGui::GetScrollMaxX();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        if (allowAutoResize)
            TryAutoResizeOverlay(contentExtraW);

        if (allowConfigSave)
            OverlayConfig_TrySave();
    }

    ImGui::End();
    ImGui::Render();

    const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const HRESULT result = g_pSwapChain->Present(0, 0);
    g_renderingOverlayFrame = false;
    return result;
}

void OverlayThread()
{
    if (!CreateOverlayWindow())
    {
        std::cout << "[Overlay] Can't create overlay window!" << std::endl;
        return;
    }

    SetupImGui();

    bool show_overlay = false;

    for (const auto& pair : KeyCodes::key_code_map)
        key_names.push_back(pair.first);

    std::sort(key_names.begin(), key_names.end());
    key_names_cstrs.reserve(key_names.size());
    for (const auto& name : key_names)
        key_names_cstrs.push_back(name.c_str());

    availableModels = getAvailableModels();

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    OverlayThreadConfigSnapshot overlayCfg = SnapshotOverlayThreadConfig();
    bool lastExcludeFromCapture = overlayCfg.excludeFromCapture;
    bool overlayHotkeyWasDown = false;
    Overlay_SetDisplayAffinity(g_hwnd, lastExcludeFromCapture);

    while (!shouldExit)
    {
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                shouldExit = true;
                break;
            }
        }
        if (shouldExit) break;

        overlayCfg = SnapshotOverlayThreadConfig();

        if (lastExcludeFromCapture != overlayCfg.excludeFromCapture)
        {
            lastExcludeFromCapture = overlayCfg.excludeFromCapture;
            Overlay_SetDisplayAffinity(g_hwnd, lastExcludeFromCapture);
        }

        const bool overlayHotkeyDown = isAnyKeyPressedWin32Only(overlayCfg.buttonOpenOverlay);
        if (overlayHotkeyDown && !overlayHotkeyWasDown)
        {
            show_overlay = !show_overlay;
            g_overlayVisible = show_overlay;

            if (show_overlay)
            {
                g_autoResizeEnabled = true;
                EnsureOverlayInsideWorkArea(g_hwnd, true);
                ShowWindow(g_hwnd, SW_SHOW);
                SetForegroundWindow(g_hwnd);
            }
            else
            {
                StoreOverlayWindowGeometry(g_hwnd, true);
                {
                    std::lock_guard<std::mutex> lock(configMutex);
                    OverlayConfig_SaveNow();
                }
                ShowWindow(g_hwnd, SW_HIDE);
            }
        }
        overlayHotkeyWasDown = overlayHotkeyDown;

        if (!show_overlay)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        HRESULT result = RenderOverlayFrame(true, true);
        if (result == DXGI_STATUS_OCCLUDED || result == DXGI_ERROR_ACCESS_LOST)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        StoreOverlayWindowGeometry(g_hwnd, true);
        std::lock_guard<std::mutex> lock(configMutex);
        OverlayConfig_SaveNow();
    }

    release_body_texture();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClass(_T("Chrome"), GetModuleHandle(NULL));
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPTSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    std::thread overlay(OverlayThread);
    overlay.join();
    return 0;
}

