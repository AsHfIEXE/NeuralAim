#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "capture.h"
#include "capture/circle_fov.h"
#include "Game_overlay.h"
#include "mouse.h"
#include "other_tools.h"
#include "runtime/thread_loops.h"
#include "NeuralAim.h"

#ifdef USE_CUDA
#include "depth/depth_anything_trt.h"
#include "depth/depth_mask.h"
#include "tensorrt/nvinf.h"
#endif

extern std::string g_iconLastError;

namespace
{
std::string g_lastIconPath;
int g_iconImageId = 0;
std::mutex g_iconMutex;

struct GameOverlayMonitorBounds
{
    RECT rect{};
    int width = 1;
    int height = 1;
};

bool sameGameOverlayMonitorRect(const RECT& a, const RECT& b)
{
    return a.left == b.left &&
        a.top == b.top &&
        a.right == b.right &&
        a.bottom == b.bottom;
}

GameOverlayMonitorBounds resolveGameOverlayMonitorBounds(int overlayMonitorIndex)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);

    HMONITOR hTargetMonitor = GetMonitorHandleByIndex(overlayMonitorIndex);
    if (!hTargetMonitor)
        hTargetMonitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    GameOverlayMonitorBounds bounds{};
    if (hTargetMonitor && GetMonitorInfo(hTargetMonitor, &mi))
    {
        bounds.rect = mi.rcMonitor;
    }
    else
    {
        bounds.rect.left = 0;
        bounds.rect.top = 0;
        bounds.rect.right = GetSystemMetrics(SM_CXSCREEN);
        bounds.rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    bounds.width = std::max(1, static_cast<int>(bounds.rect.right - bounds.rect.left));
    bounds.height = std::max(1, static_cast<int>(bounds.rect.bottom - bounds.rect.top));
    return bounds;
}

void resetGameOverlayIconCache()
{
    std::lock_guard<std::mutex> lk(g_iconMutex);
    g_lastIconPath.clear();
    g_iconImageId = 0;
    g_iconLastError.clear();
}

float rectIou(const cv::Rect& a, const cv::Rect& b)
{
    const cv::Rect intersection = a & b;
    if (intersection.width <= 0 || intersection.height <= 0)
        return 0.0f;

    const float intersectionArea = static_cast<float>(intersection.area());
    const float unionArea = static_cast<float>(a.area() + b.area()) - intersectionArea;
    if (unionArea <= 1e-6f)
        return 0.0f;

    return intersectionArea / unionArea;
}

bool detectionRepresentedByTrack(
    const cv::Rect& box,
    int classId,
    const std::vector<TrackDebugInfo>& tracks)
{
    constexpr float kSameClassIouThreshold = 0.35f;

    for (const auto& t : tracks)
    {
        if (t.classId != classId)
            continue;

        if (rectIou(box, t.box) >= kSameClassIouThreshold)
            return true;
    }

    return false;
}
}
static void draw_target_correction_demo_game_overlay(Game_overlay* overlay, float centerX, float centerY)
{
    if (!overlay)
        return;

    const float scale = 4.0f;
    float near_px = config.nearRadius * scale;
    float snap_px = config.snapRadius * scale;
    near_px = std::max(10.0f, near_px);
    snap_px = std::max(6.0f, std::min(snap_px, near_px - 4.0f));

    overlay->AddCircle({ centerX, centerY, near_px }, ARGB(180, 80, 120, 255), 2.0f);
    overlay->AddCircle({ centerX, centerY, snap_px }, ARGB(180, 255, 100, 100), 2.0f);

    static float dist_px = 0.0f;
    static float vel_px = 0.0f;
    static auto last_t = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last_t).count();
    last_t = now;
    dt = std::max(0.0, std::min(dt, 0.1));

    if (dist_px <= 0.0f || dist_px > near_px)
        dist_px = near_px;

    double dist_units = dist_px / scale;
    double speed_mult;
    if (dist_units < config.snapRadius)
    {
        speed_mult = config.minSpeedMultiplier * config.snapBoostFactor;
    }
    else if (dist_units < config.nearRadius)
    {
        double t = dist_units / config.nearRadius;
        double crv = 1.0 - std::pow(1.0 - t, config.speedCurveExponent);
        speed_mult = config.minSpeedMultiplier +
            (config.maxSpeedMultiplier - config.minSpeedMultiplier) * crv;
    }
    else
    {
        double norm = std::max(0.0, std::min(dist_units / config.nearRadius, 1.0));
        speed_mult = config.minSpeedMultiplier +
            (config.maxSpeedMultiplier - config.minSpeedMultiplier) * norm;
    }

    float max_multiplier = std::max(0.1f, config.maxSpeedMultiplier);
    float demo_duration_s = std::max(0.6f, std::min(2.2f / max_multiplier, 3.0f));
    float base_px_s = near_px / demo_duration_s;
    vel_px = base_px_s * static_cast<float>(speed_mult);
    dist_px -= vel_px * static_cast<float>(dt);
    if (dist_px <= 0.0f)
        dist_px = near_px;

    overlay->FillCircle({ centerX - dist_px, centerY, 4.0f }, ARGB(255, 255, 255, 80));
}

void gameOverlayRenderLoop()
{
#ifdef USE_CUDA
    static depth_anything::DepthAnythingTrt depthDebugModel;
    static std::string depthDebugModelPath;
    static int depthDebugColormap = -1;
    static int depthDebugImageId = 0;
    static int depthMaskImageId = 0;
    static cv::Mat depthDebugFrame;
    static auto lastDepthUpdate = std::chrono::steady_clock::time_point::min();
    static bool lastDepthInferenceEnabled = true;
#endif
    int lastDetectionVersion = -1;
    int lastOverlayMonitorIndex = 0;
    RECT lastOverlayMonitorRect{};
    bool lastOverlayMonitorStateValid = false;

    while (!gameOverlayShouldExit.load())
    {
        if (!config.game_overlay_enabled)
        {
            lastOverlayMonitorStateValid = false;
            if (gameOverlayPtr)
            {
                gameOverlayPtr->Stop();
                delete gameOverlayPtr;
                gameOverlayPtr = nullptr;
                resetGameOverlayIconCache();
            }
#ifdef USE_CUDA
            depthDebugModel.reset();
            depthDebugModelPath.clear();
            depthDebugColormap = -1;
            depthDebugImageId = 0;
            depthMaskImageId = 0;
            depthDebugFrame.release();
            lastDepthUpdate = std::chrono::steady_clock::time_point::min();
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            continue;
        }

        int overlayMonitorIndex = 0;
        {
            std::lock_guard<std::mutex> cfgLock(configMutex);
            overlayMonitorIndex = config.monitor_idx;
        }

        const GameOverlayMonitorBounds overlayBounds = resolveGameOverlayMonitorBounds(overlayMonitorIndex);
        RECT pr = overlayBounds.rect;
        const int pw = overlayBounds.width;
        const int ph = overlayBounds.height;
        const bool overlayMonitorChanged = lastOverlayMonitorStateValid &&
            (overlayMonitorIndex != lastOverlayMonitorIndex ||
                !sameGameOverlayMonitorRect(lastOverlayMonitorRect, pr));

        if (overlayMonitorChanged && gameOverlayPtr)
        {
            gameOverlayPtr->Stop();
            delete gameOverlayPtr;
            gameOverlayPtr = nullptr;
            resetGameOverlayIconCache();
        }

        lastOverlayMonitorIndex = overlayMonitorIndex;
        lastOverlayMonitorRect = pr;
        lastOverlayMonitorStateValid = true;

        if (!gameOverlayPtr)
        {
            gameOverlayPtr = new Game_overlay();
            gameOverlayPtr->SetWindowBounds(pr.left, pr.top, pw, ph);
            gameOverlayPtr->SetMaxFPS(config.game_overlay_max_fps > 0 ? (unsigned)config.game_overlay_max_fps : 0);
            gameOverlayPtr->SetExcludeFromCapture(config.overlay_exclude_from_capture);
            gameOverlayPtr->Start();
        }
        else if (!gameOverlayPtr->IsRunning())
        {
            gameOverlayPtr->SetWindowBounds(pr.left, pr.top, pw, ph);
            gameOverlayPtr->SetMaxFPS(config.game_overlay_max_fps > 0 ? (unsigned)config.game_overlay_max_fps : 0);
            gameOverlayPtr->SetExcludeFromCapture(config.overlay_exclude_from_capture);
            gameOverlayPtr->Start();
        }

        if (!gameOverlayPtr || !gameOverlayPtr->IsRunning())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            continue;
        }

        gameOverlayPtr->SetMaxFPS(config.game_overlay_max_fps > 0 ? (unsigned)config.game_overlay_max_fps : 0);
        gameOverlayPtr->SetExcludeFromCapture(config.overlay_exclude_from_capture);

        const int detRes = config.detection_resolution;
        if (detRes <= 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        int regionW = detRes;
        int regionH = detRes;

        if (regionW > pw) regionW = pw;
        if (regionH > ph) regionH = ph;

        const int baseX = (pw - regionW) / 2;
        const int baseY = (ph - regionH) / 2;

        const float scaleX = detRes > 0 ? (static_cast<float>(regionW) / static_cast<float>(detRes)) : 1.0f;
        const float scaleY = detRes > 0 ? (static_cast<float>(regionH) / static_cast<float>(detRes)) : 1.0f;

        std::vector<cv::Rect> boxesCopy;
        std::vector<int> classesCopy;
        std::chrono::steady_clock::time_point detectionTimestamp{};
        int detectionVersion = lastDetectionVersion;
        {
            std::unique_lock<std::mutex> lk(detectionBuffer.mutex);
            const unsigned fpsCap = (unsigned)config.game_overlay_max_fps;
            const int waitMs = (fpsCap > 0) ? static_cast<int>(std::max(1u, 1000u / fpsCap)) : 8;
            detectionBuffer.cv.wait_for(lk, std::chrono::milliseconds(waitMs), [&] {
                return detectionBuffer.version != lastDetectionVersion || gameOverlayShouldExit.load();
            });
            boxesCopy = detectionBuffer.boxes;
            classesCopy = detectionBuffer.classes;
            detectionTimestamp = detectionBuffer.frameTimestamp;
            detectionVersion = detectionBuffer.version;
        }
        lastDetectionVersion = detectionVersion;

        decltype(globalMouseThread->getFuturePositions()) futurePts;
        if (config.game_overlay_draw_future && globalMouseThread)
            futurePts = globalMouseThread->getFuturePositions();

        std::vector<std::pair<double, double>> windTailPts;
        if (config.game_overlay_draw_wind_tail && globalMouseThread)
            windTailPts = globalMouseThread->getWindDebugTrail();

        std::vector<TrackDebugInfo> trackDebugCopy;
        int lockedTrackId = -1;
        {
            std::lock_guard<std::mutex> lk(g_trackerDebugMutex);
            trackDebugCopy = g_trackerDebugTracks;
            lockedTrackId = g_trackerLockedId;
        }

        const auto overlayNow = std::chrono::steady_clock::now();
        auto projectDetectionBox = [&](
            const cv::Rect& b,
            double velocityX = 0.0,
            double velocityY = 0.0,
            std::chrono::steady_clock::time_point lastUpdate = {}) -> std::optional<OverlayRect>
        {
            if (b.width <= 0 || b.height <= 0)
                return std::nullopt;

            const auto compensationTimestamp =
                (lastUpdate.time_since_epoch().count() != 0) ? lastUpdate : detectionTimestamp;

            double ageSec = 0.0;
            if (compensationTimestamp.time_since_epoch().count() != 0)
            {
                ageSec = std::chrono::duration<double>(overlayNow - compensationTimestamp).count();
                if (!std::isfinite(ageSec) || ageSec < 0.0)
                    ageSec = 0.0;
                ageSec = std::clamp(ageSec, 0.0, 0.35);
            }

            std::pair<double, double> cameraCompensation{ 0.0, 0.0 };
            if (config.game_overlay_compensate_latency && globalMouseThread &&
                compensationTimestamp.time_since_epoch().count() != 0)
            {
                cameraCompensation = globalMouseThread->getMotionCompensationSince(compensationTimestamp);
            }

            double left = static_cast<double>(b.x) + velocityX * ageSec - cameraCompensation.first;
            double top = static_cast<double>(b.y) + velocityY * ageSec - cameraCompensation.second;
            double right = left + static_cast<double>(b.width);
            double bottom = top + static_cast<double>(b.height);

            left = std::clamp(left, 0.0, static_cast<double>(detRes));
            top = std::clamp(top, 0.0, static_cast<double>(detRes));
            right = std::clamp(right, 0.0, static_cast<double>(detRes));
            bottom = std::clamp(bottom, 0.0, static_cast<double>(detRes));

            const double w = right - left;
            const double h = bottom - top;
            if (w <= 0.0 || h <= 0.0)
                return std::nullopt;

            OverlayRect rect{
                static_cast<float>(baseX + left * scaleX),
                static_cast<float>(baseY + top * scaleY),
                static_cast<float>(w * scaleX),
                static_cast<float>(h * scaleY)
            };

            if (rect.x + rect.w < baseX || rect.y + rect.h < baseY ||
                rect.x > baseX + regionW || rect.y > baseY + regionH)
                return std::nullopt;

            return rect;
        };

        if (config.game_overlay_icon_enabled)
        {
            std::lock_guard<std::mutex> lk(g_iconMutex);
            if (config.game_overlay_icon_path != g_lastIconPath)
            {
                if (g_iconImageId != 0)
                {
                    gameOverlayPtr->UnloadImage(g_iconImageId);
                    g_iconImageId = 0;
                }
                g_lastIconPath = config.game_overlay_icon_path;
                std::error_code fsErr;
                std::filesystem::path p;
                try
                {
                    p = std::filesystem::u8path(g_lastIconPath);
                }
                catch (const std::exception&)
                {
                    p = std::filesystem::path(g_lastIconPath);
                }
                const bool hasFile = !g_lastIconPath.empty() && p.has_filename() && std::filesystem::is_regular_file(p, fsErr);
                if (fsErr)
                {
                    g_iconImageId = 0;
                    g_iconLastError = "[GameOverlay] Failed to read icon path: " + g_lastIconPath + " (" + fsErr.message() + ")";
                    std::cerr << g_iconLastError << std::endl;
                }
                else if (hasFile)
                {
                    const std::wstring wpath = p.wstring();
                    g_iconLastError.clear();

                    UINT iw = 0, ih = 0;
                    std::string verr;
                    if (!IsValidImageFile(wpath, iw, ih, verr))
                    {
                        g_iconImageId = 0;
                        g_iconLastError = "[GameOverlay] Invalid image '" + g_lastIconPath + "': " + verr;
                        std::cerr << g_iconLastError << std::endl;
                    }
                    else
                    {
                        try
                        {
                            int id = gameOverlayPtr->LoadImageFromFile(wpath);
                            if (id != 0)
                            {
                                g_iconImageId = id;
                                std::cout << "[GameOverlay] Loaded icon (" << iw << "x" << ih << "): " << g_lastIconPath << std::endl;
                            }
                            else
                            {
                                g_iconImageId = 0;
                                g_iconLastError = "[GameOverlay] Failed to load icon (loader returned 0): " + g_lastIconPath;
                                std::cerr << g_iconLastError << std::endl;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            g_iconImageId = 0;
                            g_iconLastError = std::string("[GameOverlay] Exception while loading icon: ") + e.what();
                            std::cerr << g_iconLastError << std::endl;
                        }
                        catch (...)
                        {
                            g_iconImageId = 0;
                            g_iconLastError = "[GameOverlay] Unknown exception while loading icon.";
                            std::cerr << g_iconLastError << std::endl;
                        }
                    }
                }
                else
                {
                    g_iconImageId = 0;
                    g_iconLastError = "[GameOverlay] Icon file not found: " + g_lastIconPath;
                    std::cerr << g_iconLastError << std::endl;
                }
            }
        }

        gameOverlayPtr->BeginFrame();

#ifdef USE_CUDA
        if (!config.depth_inference_enabled)
        {
            if (lastDepthInferenceEnabled)
            {
                if (gameOverlayPtr)
                {
                    if (depthDebugImageId != 0)
                    {
                        gameOverlayPtr->UnloadImage(depthDebugImageId);
                        depthDebugImageId = 0;
                    }
                    if (depthMaskImageId != 0)
                    {
                        gameOverlayPtr->UnloadImage(depthMaskImageId);
                        depthMaskImageId = 0;
                    }
                }

                depthDebugModel.reset();
                depthDebugModelPath.clear();
                depthDebugColormap = -1;
                depthDebugFrame.release();
                lastDepthUpdate = std::chrono::steady_clock::time_point::min();

                auto& depthMask = depth_anything::GetDepthMaskGenerator();
                depthMask.reset();
            }
            lastDepthInferenceEnabled = false;
        }
        else
        {
            lastDepthInferenceEnabled = true;

            float depthW = 0.0f;
            float depthH = 0.0f;
            float maskW = 0.0f;
            float maskH = 0.0f;
            float maskOpacity = std::clamp(static_cast<float>(config.depth_mask_alpha) / 255.0f, 0.0f, 1.0f);
            bool maskHasBounds = false;
            cv::Rect maskBounds{};

            if (config.depth_debug_overlay_enabled)
            {
                cv::Mat frameCopy;
                {
                    std::lock_guard<std::mutex> lk(frameMutex);
                    if (!latestFrame.empty())
                        latestFrame.copyTo(frameCopy);
                }

                if (config.depth_model_path.empty())
                {
                    if (depthDebugModel.ready())
                        depthDebugModel.reset();
                    depthDebugModelPath.clear();
                }
                else if (depthDebugModelPath != config.depth_model_path || !depthDebugModel.ready())
                {
                    if (depthDebugModel.initialize(config.depth_model_path, gLogger))
                    {
                        depthDebugModelPath = config.depth_model_path;
                    }
                }

                if (config.depth_colormap != depthDebugColormap)
                {
                    depthDebugModel.setColormap(config.depth_colormap);
                    depthDebugColormap = config.depth_colormap;
                }

                if (depthDebugModel.ready() && !frameCopy.empty())
                {
                    auto now = std::chrono::steady_clock::now();
                    bool shouldUpdate = depthDebugFrame.empty();
                    if (config.depth_fps <= 0)
                    {
                        shouldUpdate = true;
                    }
                    else if (!shouldUpdate)
                    {
                        auto interval = std::chrono::milliseconds(1000 / config.depth_fps);
                        shouldUpdate = (now - lastDepthUpdate) >= interval;
                    }
                    if (shouldUpdate)
                    {
                        cv::Mat depthFrame = depthDebugModel.predict(frameCopy);
                        if (!depthFrame.empty())
                        {
                            depthDebugFrame = depthFrame;
                            lastDepthUpdate = now;
                        }
                    }
                }
                if (!depthDebugFrame.empty())
                {
                    cv::Mat depthBGRA;
                    cv::cvtColor(depthDebugFrame, depthBGRA, cv::COLOR_BGR2BGRA);
                    int newId = gameOverlayPtr->UpdateImageFromBGRA(
                        depthBGRA.data,
                        depthBGRA.cols,
                        depthBGRA.rows,
                        static_cast<int>(depthBGRA.step),
                        depthDebugImageId);
                    if (newId != 0)
                        depthDebugImageId = newId;
                    depthW = static_cast<float>(regionW);
                    depthH = static_cast<float>(regionH);
                }
            }
            else if (depthDebugImageId != 0)
            {
                gameOverlayPtr->UnloadImage(depthDebugImageId);
                depthDebugImageId = 0;
            }

            if (config.depth_mask_enabled)
            {
                auto& depthMask = depth_anything::GetDepthMaskGenerator();
                cv::Mat mask = depthMask.getMask();

                if (mask.empty())
                {
                    cv::Mat frameCopy;
                    {
                        std::lock_guard<std::mutex> lk(frameMutex);
                        if (!latestFrame.empty())
                            latestFrame.copyTo(frameCopy);
                    }

                    if (!frameCopy.empty())
                    {
                        depth_anything::DepthMaskOptions maskOptions;
                        maskOptions.enabled = true;
                        maskOptions.fps = config.depth_mask_fps;
                        maskOptions.near_percent = config.depth_mask_near_percent;
                        maskOptions.expand = config.depth_mask_expand;
                        maskOptions.invert = config.depth_mask_invert;

                        depthMask.update(frameCopy, maskOptions, config.depth_model_path, gLogger);
                        mask = depthMask.getMask();

                        if (mask.empty())
                        {
                            if (!config.depth_model_path.empty() &&
                                (depthDebugModelPath != config.depth_model_path || !depthDebugModel.ready()))
                            {
                                if (depthDebugModel.initialize(config.depth_model_path, gLogger))
                                {
                                    depthDebugModelPath = config.depth_model_path;
                                    depthDebugColormap = config.depth_colormap;
                                    depthDebugModel.setColormap(config.depth_colormap);
                                }
                            }

                            if (depthDebugModel.ready())
                            {
                                cv::Mat depthLocal = depthDebugModel.predictDepth(frameCopy);
                                if (!depthLocal.empty())
                                {
                                    const int nearPercent = std::clamp(config.depth_mask_near_percent, 1, 100);
                                    const bool invertMask = config.depth_mask_invert;
                                    const int total = depthLocal.rows * depthLocal.cols;
                                    if (total > 0)
                                    {
                                        int hist[256] = {};
                                        for (int y = 0; y < depthLocal.rows; ++y)
                                        {
                                            const uint8_t* row = depthLocal.ptr<uint8_t>(y);
                                            for (int x = 0; x < depthLocal.cols; ++x)
                                                hist[row[x]]++;
                                        }

                                        const int target = std::max(1, (total * nearPercent) / 100);
                                        int threshold = 0;
                                        if (!invertMask)
                                        {
                                            int count = 0;
                                            for (int i = 0; i < 256; ++i)
                                            {
                                                count += hist[i];
                                                if (count >= target)
                                                {
                                                    threshold = i;
                                                    break;
                                                }
                                            }
                                            cv::compare(depthLocal, threshold, mask, cv::CMP_LE);
                                        }
                                        else
                                        {
                                            int count = 0;
                                            for (int i = 255; i >= 0; --i)
                                            {
                                                count += hist[i];
                                                if (count >= target)
                                                {
                                                    threshold = i;
                                                    break;
                                                }
                                            }
                                            cv::compare(depthLocal, threshold, mask, cv::CMP_GE);
                                        }

                                        const int expand = std::clamp(config.depth_mask_expand, 0, 128);
                                        if (expand > 0)
                                        {
                                            const int kernelSize = 2 * expand + 1;
                                            cv::Mat kernel = cv::getStructuringElement(
                                                cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
                                            cv::dilate(mask, mask, kernel);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (!mask.empty())
                {
                    cv::Mat maskBGRA(mask.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));
                    maskBGRA.setTo(cv::Scalar(20, 90, 255, 255), mask);

                    cv::Mat nonZeroPoints;
                    cv::findNonZero(mask, nonZeroPoints);
                    if (!nonZeroPoints.empty())
                    {
                        maskBounds = cv::boundingRect(nonZeroPoints);
                        maskHasBounds = true;

                    }

                    int newId = gameOverlayPtr->UpdateImageFromBGRA(
                        maskBGRA.data,
                        maskBGRA.cols,
                        maskBGRA.rows,
                        static_cast<int>(maskBGRA.step),
                        depthMaskImageId);
                    if (newId != 0)
                        depthMaskImageId = newId;

                    maskW = static_cast<float>(regionW);
                    maskH = static_cast<float>(regionH);
                }
            }
            else if (depthMaskImageId != 0)
            {
                gameOverlayPtr->UnloadImage(depthMaskImageId);
                depthMaskImageId = 0;
            }

            if (depthDebugImageId != 0 || depthMaskImageId != 0 || (config.depth_debug_overlay_enabled && config.depth_mask_enabled))
            {
                float depthX = static_cast<float>(baseX);
                float depthY = static_cast<float>(baseY);
                float maskX = depthX;
                float maskY = depthY;

                if (depthDebugImageId != 0 && depthW > 0.0f && depthH > 0.0f)
                {
                    const float depthDebugOpacity = config.depth_mask_enabled ? 0.30f : 1.0f;
                    gameOverlayPtr->DrawImage(depthDebugImageId, depthX, depthY, depthW, depthH, depthDebugOpacity);
                    gameOverlayPtr->AddRect({ depthX, depthY, depthW, depthH }, ARGB(120, 255, 255, 255), 1.0f);
                }

                if (depthMaskImageId != 0 && maskW > 0.0f && maskH > 0.0f)
                {
                    gameOverlayPtr->DrawImage(depthMaskImageId, maskX, maskY, maskW, maskH, maskOpacity);

                    if (maskHasBounds)
                    {
                        const float bx = maskX + static_cast<float>(maskBounds.x) * scaleX;
                        const float by = maskY + static_cast<float>(maskBounds.y) * scaleY;
                        const float bw = static_cast<float>(maskBounds.width) * scaleX;
                        const float bh = static_cast<float>(maskBounds.height) * scaleY;

                        gameOverlayPtr->AddRect({ bx, by, bw, bh }, ARGB(230, 255, 240, 120), 1.8f);
                    }
                }
            }
        }
#endif

        // CAPTURE FRAME
        if (config.game_overlay_draw_frame)
        {
            int A = config.game_overlay_frame_a;
            int R = config.game_overlay_frame_r;
            int G = config.game_overlay_frame_g;
            int B = config.game_overlay_frame_b;
            auto clamp255 = [](int& v) { if (v < 0) v = 0; else if (v > 255) v = 255; };
            clamp255(A); clamp255(R); clamp255(G); clamp255(B);
            const uint32_t col =
                (uint32_t(A) << 24) |
                (uint32_t(R) << 16) |
                (uint32_t(G) << 8) |
                uint32_t(B);

            float thickness = config.game_overlay_frame_thickness;
            if (thickness <= 0.f) thickness = 1.f;

            gameOverlayPtr->AddRect(
                { static_cast<float>(baseX),
                  static_cast<float>(baseY),
                  static_cast<float>(regionW),
                  static_cast<float>(regionH) },
                col, thickness);
        }

        if (config.circle_fov_enabled && config.game_overlay_draw_circle_fov)
        {
            int A = config.game_overlay_frame_a;
            int R = config.game_overlay_frame_r;
            int G = config.game_overlay_frame_g;
            int B = config.game_overlay_frame_b;
            auto clamp255 = [](int& v) { if (v < 0) v = 0; else if (v > 255) v = 255; };
            clamp255(A); clamp255(R); clamp255(G); clamp255(B);
            const uint32_t col =
                (uint32_t(A) << 24) |
                (uint32_t(R) << 16) |
                (uint32_t(G) << 8) |
                uint32_t(B);

            float thickness = config.game_overlay_frame_thickness;
            if (thickness <= 0.f) thickness = 1.f;

            const cv::Size regionSize(regionW, regionH);
            const cv::Point2f center = getCircleFovCenter(regionSize);
            const float radius = getCircleFovRadiusPixels(regionSize, config.circle_fov_radius_percent);
            gameOverlayPtr->AddCircle(
                { static_cast<float>(baseX) + center.x, static_cast<float>(baseY) + center.y, radius },
                col,
                thickness);
        }

        // BOXES
        if (config.game_overlay_draw_boxes && (!boxesCopy.empty() || !trackDebugCopy.empty()))
        {
            int A = config.game_overlay_box_a;
            int R = config.game_overlay_box_r;
            int G = config.game_overlay_box_g;
            int B = config.game_overlay_box_b;
            auto clamp255 = [](int& v) { if (v < 0) v = 0; else if (v > 255) v = 255; };
            clamp255(A); clamp255(R); clamp255(G); clamp255(B);
            const uint32_t col =
                (uint32_t(A) << 24) |
                (uint32_t(R) << 16) |
                (uint32_t(G) << 8) |
                uint32_t(B);

            float thickness = config.game_overlay_box_thickness;
            if (thickness <= 0.f) thickness = 1.f;

            const bool drawCompensatedTracks =
                config.game_overlay_compensate_latency && !trackDebugCopy.empty();

            if (drawCompensatedTracks)
            {
                for (const auto& t : trackDebugCopy)
                {
                    auto rect = projectDetectionBox(t.box, t.velocityX, t.velocityY, t.lastUpdate);
                    if (!rect)
                        continue;
                    gameOverlayPtr->AddRect(*rect, col, thickness);
                }

                for (size_t i = 0; i < boxesCopy.size(); ++i)
                {
                    const int cls = (i < classesCopy.size()) ? classesCopy[i] : -1;
                    if (detectionRepresentedByTrack(boxesCopy[i], cls, trackDebugCopy))
                        continue;

                    auto rect = projectDetectionBox(boxesCopy[i]);
                    if (!rect)
                        continue;

                    gameOverlayPtr->AddRect(*rect, col, thickness);
                }
            }
            else
            {
                for (const auto& b : boxesCopy)
                {
                    auto rect = projectDetectionBox(b);
                    if (!rect)
                        continue;
                    gameOverlayPtr->AddRect(*rect, col, thickness);
                }
            }

            for (const auto& t : trackDebugCopy)
            {
                auto rect = projectDetectionBox(
                    t.box,
                    config.game_overlay_compensate_latency ? t.velocityX : 0.0,
                    config.game_overlay_compensate_latency ? t.velocityY : 0.0,
                    t.lastUpdate);
                if (!rect)
                    continue;

                std::wstring label = L"ID " + std::to_wstring(t.trackId);
                if (t.trackId == lockedTrackId || t.isLocked)
                    label += L" *";
                if (!t.observedThisFrame)
                    label += L" m" + std::to_wstring(t.missedFrames);

                const uint32_t textCol =
                    (t.trackId == lockedTrackId || t.isLocked)
                    ? ARGB(255, 255, 220, 70)
                    : ARGB(230, 180, 255, 180);

                gameOverlayPtr->AddText(
                    rect->x + 2.0f,
                    std::max(static_cast<float>(baseY), rect->y - 16.0f),
                    label,
                    15.0f,
                    textCol
                );
            }
        }

        // FUTURE POINTS
        if (config.game_overlay_draw_future && !futurePts.empty())
        {
            const int total = static_cast<int>(futurePts.size());
            const int baseA = std::max(5, std::min(255, config.game_overlay_box_a));

            for (int i = 0; i < total; ++i)
            {
                float alphaFactor =
                    std::exp(-config.game_overlay_future_alpha_falloff *
                        (static_cast<float>(i) / static_cast<float>(total)));

                int a = static_cast<int>(baseA * alphaFactor);
                if (a < 12) a = 12;

                const uint32_t col =
                    (uint32_t(a) << 24) |
                    (uint32_t(255 - (i * 255 / total)) << 16) |
                    (uint32_t(50) << 8) |
                    (uint32_t(i * 255 / total));

                float px = static_cast<float>(baseX) + static_cast<float>(futurePts[i].first) * scaleX;
                float py = static_cast<float>(baseY) + static_cast<float>(futurePts[i].second) * scaleY;

                if (px < baseX - 40 || py < baseY - 40 ||
                    px > baseX + regionW + 40 || py > baseY + regionH + 40)
                    continue;

                gameOverlayPtr->FillCircle({ px, py, config.game_overlay_future_point_radius }, col);
            }
        }

        // WIND DEBUG TAIL
        if (config.game_overlay_draw_wind_tail && windTailPts.size() > 1)
        {
            const size_t n = windTailPts.size();
            const auto& anchor = windTailPts.back();
            const float centerX = static_cast<float>(baseX) + regionW * 0.5f;
            const float centerY = static_cast<float>(baseY) + regionH * 0.5f;
            for (size_t i = 1; i < n; ++i)
            {
                const auto& p0 = windTailPts[i - 1];
                const auto& p1 = windTailPts[i];

                const float rel0x = static_cast<float>(p0.first - anchor.first);
                const float rel0y = static_cast<float>(p0.second - anchor.second);
                const float rel1x = static_cast<float>(p1.first - anchor.first);
                const float rel1y = static_cast<float>(p1.second - anchor.second);

                const float x0 = centerX + rel0x * scaleX;
                const float y0 = centerY + rel0y * scaleY;
                const float x1 = centerX + rel1x * scaleX;
                const float y1 = centerY + rel1y * scaleY;

                const uint8_t alpha = static_cast<uint8_t>(35 + (190 * i) / n);
                gameOverlayPtr->AddLine({ x0, y0, x1, y1 }, ARGB(alpha, 80, 210, 255), 1.3f);
            }

            const float hx = centerX;
            const float hy = centerY;
            gameOverlayPtr->FillCircle({ hx, hy, 3.5f }, ARGB(230, 120, 230, 255));
            gameOverlayPtr->AddText(
                static_cast<float>(baseX) + 8.0f,
                static_cast<float>(baseY + regionH) - 22.0f,
                L"Wind tail",
                14.0f,
                ARGB(210, 120, 230, 255)
            );
        }

        // ICONS
        if (config.game_overlay_icon_enabled && g_iconImageId != 0 && !boxesCopy.empty())
        {
            const int iconW = config.game_overlay_icon_width;
            const int iconH = config.game_overlay_icon_height;
            const float offXIcon = config.game_overlay_icon_offset_x;
            const float offYIcon = config.game_overlay_icon_offset_y;
            std::string anchor = config.game_overlay_icon_anchor;
            const int wantedClass = config.game_overlay_icon_class;
            const size_t count = boxesCopy.size();
            for (size_t i = 0; i < count; ++i)
            {
                const auto& b = boxesCopy[i];
                int cls = (i < classesCopy.size()) ? classesCopy[i] : -1;
                // Class filter (-1 = all)
                if (wantedClass >= 0 && cls != wantedClass)
                {
                    continue;
                }

                auto boxRect = projectDetectionBox(b);
                if (!boxRect)
                    continue;

                float drawX = boxRect->x;
                float drawY = boxRect->y;

                if (anchor == "center")
                {
                    drawX = boxRect->x + boxRect->w / 2.0f - iconW / 2.0f;
                    drawY = boxRect->y + boxRect->h / 2.0f - iconH / 2.0f;
                }
                else if (anchor == "top" || anchor == "head")
                {
                    drawX = boxRect->x + boxRect->w / 2.0f - iconW / 2.0f;
                    drawY = boxRect->y - iconH;
                }
                else if (anchor == "bottom")
                {
                    drawX = boxRect->x + boxRect->w / 2.0f - iconW / 2.0f;
                    drawY = boxRect->y + boxRect->h;
                }
                else
                {
                    drawX = boxRect->x + boxRect->w / 2.0f - iconW / 2.0f;
                    drawY = boxRect->y + boxRect->h / 2.0f - iconH / 2.0f;
                }

                drawX += offXIcon;
                drawY += offYIcon;

                gameOverlayPtr->DrawImage(g_iconImageId, drawX, drawY, (float)iconW, (float)iconH, 1.0f);
            }
        }

        if (config.game_overlay_show_target_correction)
        {
            draw_target_correction_demo_game_overlay(
                gameOverlayPtr,
                static_cast<float>(baseX) + regionW * 0.5f,
                static_cast<float>(baseY) + regionH * 0.5f);
        }

        else
        {
        }

        gameOverlayPtr->EndFrame();
    }

    if (gameOverlayPtr)
    {
        gameOverlayPtr->Stop();
        delete gameOverlayPtr;
        gameOverlayPtr = nullptr;
    }
}

