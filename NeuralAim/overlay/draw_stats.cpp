#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "imgui/imgui.h"
#include "NeuralAim.h"
#include "overlay.h"
#include "capture.h"
#include "other_tools.h"
#include "overlay/ui_sections.h"

void draw_stats()
{
    static float preprocess_times[120] = {};
    static float inference_times[120] = {};
    static float copy_times[120] = {};
    static float postprocess_times[120] = {};
    static float nms_times[120] = {};
    static int index_inf = 0;

    static float capture_fps_vals[120] = {};
    static int index_fps = 0;

    static float avg_preprocess_cached = 0.0f;
    static float avg_inference_cached = 0.0f;
    static float avg_copy_cached = 0.0f;
    static float avg_post_cached = 0.0f;
    static float avg_nms_cached = 0.0f;
    static float avg_fps_cached = 0.0f;
    static double last_avg_update_time = 0.0;

    float current_preprocess = 0.0f;
    float current_inference = 0.0f;
    float current_copy = 0.0f;
    float current_post = 0.0f;
    float current_nms = 0.0f;

#ifdef USE_CUDA
    current_preprocess = static_cast<float>(trt_detector.lastPreprocessTime.count());
    current_inference = static_cast<float>(trt_detector.lastInferenceTime.count());
    current_copy = static_cast<float>(trt_detector.lastCopyTime.count());
    current_post = static_cast<float>(trt_detector.lastPostprocessTime.count());
    current_nms = static_cast<float>(trt_detector.lastNmsTime.count());
#else
    if (dml_detector)
    {
        current_preprocess = static_cast<float>(dml_detector->lastPreprocessTimeDML.count());
        current_inference = static_cast<float>(dml_detector->lastInferenceTimeDML.count());
        current_copy = static_cast<float>(dml_detector->lastCopyTimeDML.count());
        current_post = static_cast<float>(dml_detector->lastPostprocessTimeDML.count());
        current_nms = static_cast<float>(dml_detector->lastNmsTimeDML.count());
    }
#endif

    preprocess_times[index_inf] = current_preprocess;
    inference_times[index_inf] = current_inference;
    copy_times[index_inf] = current_copy;
    postprocess_times[index_inf] = current_post;
    nms_times[index_inf] = current_nms;
    index_inf = (index_inf + 1) % IM_ARRAYSIZE(inference_times);

    float current_fps = static_cast<float>(captureFps.load());
    capture_fps_vals[index_fps] = current_fps;
    index_fps = (index_fps + 1) % IM_ARRAYSIZE(capture_fps_vals);

    auto avg = [](const float* arr, int n) -> float {
        float sum = 0.0f; int cnt = 0;
        for (int i = 0; i < n; ++i)
            if (arr[i] > 0.0f) { sum += arr[i]; ++cnt; }
        return cnt ? (sum / cnt) : 0.0f;
        };

    const double now = ImGui::GetTime();
    if (last_avg_update_time == 0.0 || (now - last_avg_update_time) >= 1.0)
    {
        avg_preprocess_cached = avg(preprocess_times, IM_ARRAYSIZE(preprocess_times));
        avg_inference_cached = avg(inference_times, IM_ARRAYSIZE(inference_times));
        avg_copy_cached = avg(copy_times, IM_ARRAYSIZE(copy_times));
        avg_post_cached = avg(postprocess_times, IM_ARRAYSIZE(postprocess_times));
        avg_nms_cached = avg(nms_times, IM_ARRAYSIZE(nms_times));
        avg_fps_cached = avg(capture_fps_vals, IM_ARRAYSIZE(capture_fps_vals));

        last_avg_update_time = now;
    }

    const bool captureUsesMonitorRefresh =
        config.capture_method == "duplication_api" ||
        (config.capture_method == "winrt" && config.capture_target != "window");

    static int cachedRefreshMonitorIdx = -1;
    static double cachedRefreshQueryTime = -100.0;
    static double cachedMonitorRefreshHz = 0.0;
    if (captureUsesMonitorRefresh)
    {
        const int monitorIdx = std::max(0, config.monitor_idx);
        if (cachedRefreshMonitorIdx != monitorIdx || now - cachedRefreshQueryTime >= 2.0)
        {
            cachedMonitorRefreshHz = GetMonitorRefreshRateByIndex(monitorIdx);
            cachedRefreshMonitorIdx = monitorIdx;
            cachedRefreshQueryTime = now;
        }
    }

    if (OverlayUI::BeginSection("Time Breakdown", "stats_section_time_breakdown"))
    {
        ImGui::PlotLines("Preprocess", preprocess_times, IM_ARRAYSIZE(preprocess_times), index_inf, nullptr, 0.0f, 20.0f, ImVec2(0, 40));
        ImGui::SameLine(); ImGui::Text("%.2f | Avg: %.2f", current_preprocess, avg_preprocess_cached);

        ImGui::PlotLines("Inference", inference_times, IM_ARRAYSIZE(inference_times), index_inf, nullptr, 0.0f, 20.0f, ImVec2(0, 40));
        ImGui::SameLine();

        ImGui::Text("%.2f | Avg:", current_inference);
        ImGui::SameLine();

        const bool inf_slow = (avg_inference_cached > 20.0f);
        if (inf_slow)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

        ImGui::Text("%.2f", avg_inference_cached);

        if (inf_slow)
            ImGui::PopStyleColor();

        ImGui::PlotLines("Copy", copy_times, IM_ARRAYSIZE(copy_times), index_inf, nullptr, 0.0f, 10.0f, ImVec2(0, 40));
        ImGui::SameLine(); ImGui::Text("%.2f | Avg: %.2f", current_copy, avg_copy_cached);

        ImGui::PlotLines("Postprocess", postprocess_times, IM_ARRAYSIZE(postprocess_times), index_inf, nullptr, 0.0f, 10.0f, ImVec2(0, 40));
        ImGui::SameLine(); ImGui::Text("%.2f | Avg: %.2f", current_post, avg_post_cached);

        ImGui::PlotLines("NMS", nms_times, IM_ARRAYSIZE(nms_times), index_inf, nullptr, 0.0f, 5.0f, ImVec2(0, 40));
        ImGui::SameLine(); ImGui::Text("%.2f | Avg: %.2f", current_nms, avg_nms_cached);

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Capture FPS", "stats_section_capture_fps"))
    {
        const float fpsPlotMax = (captureUsesMonitorRefresh && cachedMonitorRefreshHz > 1.0)
            ? static_cast<float>(cachedMonitorRefreshHz)
            : 360.0f;
        ImGui::PlotLines("##fps_plot", capture_fps_vals, IM_ARRAYSIZE(capture_fps_vals), index_fps, nullptr, 0.0f, fpsPlotMax, ImVec2(0, 60));
        ImGui::SameLine();
        ImGui::Text("Now: %.1f | Avg: %.1f", current_fps, avg_fps_cached);
        if (captureUsesMonitorRefresh && cachedMonitorRefreshHz > 1.0)
        {
            const float refreshHz = static_cast<float>(cachedMonitorRefreshHz);
            const float fpsLoad = std::clamp(avg_fps_cached / refreshHz, 0.0f, 1.0f);
            ImGui::Spacing();
            ImGui::Text("Monitor load");
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f / %.1f Hz (%.0f%%)", avg_fps_cached, refreshHz, fpsLoad * 100.0f);
            ImGui::ProgressBar(fpsLoad, ImVec2(-1.0f, 18.0f), "");
        }
        OverlayUI::EndSection();
    }

    int latestWidth = 0;
    int latestHeight = 0;
    size_t queueDepth = 0;
    {
        std::lock_guard<std::mutex> lk(frameMutex);
        if (!latestFrame.empty())
        {
            latestWidth = latestFrame.cols;
            latestHeight = latestFrame.rows;
        }
        queueDepth = frameQueue.size();
    }

    const int captureFpsLimit = std::max(0, config.capture_fps);
    const float currentFrameTimeMs = (current_fps > 0.01f) ? (1000.0f / current_fps) : 0.0f;
    const float avgFrameTimeMs = (avg_fps_cached > 0.01f) ? (1000.0f / avg_fps_cached) : 0.0f;
    const int sourceWidth = screenWidth.load(std::memory_order_relaxed);
    const int sourceHeight = screenHeight.load(std::memory_order_relaxed);

    std::string captureSource = "Unknown";
    std::string sourceSizeLabel = "Desktop size";
    if (config.capture_method == "duplication_api")
    {
        captureSource = "Monitor " + std::to_string(std::max(0, config.monitor_idx) + 1);
    }
    else if (config.capture_method == "winrt")
    {
        if (config.capture_target == "window")
        {
            captureSource = config.capture_window_title.empty()
                ? "Window target is empty"
                : "Window: " + config.capture_window_title;
            sourceSizeLabel = "Window size";
        }
        else
        {
            captureSource = "Monitor " + std::to_string(std::max(0, config.monitor_idx) + 1);
        }
    }
    else if (config.capture_method == "virtual_camera")
    {
        captureSource =
            "Camera: " + config.virtual_camera_name + " (" +
            std::to_string(config.virtual_camera_width) + "x" +
            std::to_string(config.virtual_camera_heigth) + ")";
        sourceSizeLabel = "Camera size";
    }
    else if (config.capture_method == "udp_capture")
    {
        captureSource = "UDP " + config.udp_ip + ":" + std::to_string(config.udp_port);
        sourceSizeLabel = "Stream size";
    }

    if (OverlayUI::BeginSection("Capture Details", "stats_section_capture_details"))
    {
        ImGui::Text("Method: %s", config.capture_method.c_str());
        ImGui::Text("Backend: %s", config.backend.c_str());
        ImGui::TextWrapped("Source: %s", captureSource.c_str());

        if (sourceWidth > 0 && sourceHeight > 0)
            ImGui::Text("%s: %dx%d", sourceSizeLabel.c_str(), sourceWidth, sourceHeight);
        else
            ImGui::TextDisabled("%s: n/a", sourceSizeLabel.c_str());

        if (captureUsesMonitorRefresh)
        {
            if (cachedMonitorRefreshHz > 0.0)
                ImGui::Text("Monitor refresh: %.2f Hz", cachedMonitorRefreshHz);
            else
                ImGui::TextDisabled("Monitor refresh: n/a");
        }

        if (latestWidth > 0 && latestHeight > 0)
            ImGui::Text("Latest frame: %dx%d", latestWidth, latestHeight);
        else
            ImGui::TextDisabled("Latest frame: n/a");

        ImGui::Text("Detection resolution: %d", config.detection_resolution);
        if (captureFpsLimit > 0)
            ImGui::Text("Capture FPS limit: %d", captureFpsLimit);
        else
            ImGui::Text("Capture FPS limit: unlimited");

        if (currentFrameTimeMs > 0.0f || avgFrameTimeMs > 0.0f)
            ImGui::Text("Frame time: now %.2f ms | avg %.2f ms", currentFrameTimeMs, avgFrameTimeMs);
        else
            ImGui::TextDisabled("Frame time: n/a");

        ImGui::Text("Frame queue depth: %d", static_cast<int>(queueDepth));
        ImGui::Text("Circle FOV: %s", config.circle_fov_enabled ? "on" : "off");

        static bool winrtStatsInitialized = false;
        static uint64_t lastWinrtPolls = 0;
        static uint64_t lastWinrtDrained = 0;
        static uint64_t lastWinrtReturned = 0;
        static uint64_t lastWinrtEmpty = 0;
        static uint64_t lastWinrtReadbackMicros = 0;
        static uint64_t lastWinrtMapMicros = 0;
        static uint64_t lastWinrtPixelCopyMicros = 0;
        static double lastWinrtStatsTime = 0.0;
        static float winrtPollRate = 0.0f;
        static float winrtDrainedRate = 0.0f;
        static float winrtReturnedRate = 0.0f;
        static float winrtEmptyRate = 0.0f;
        static float winrtReadbackAvgMs = 0.0f;
        static float winrtMapAvgMs = 0.0f;
        static float winrtPixelCopyAvgMs = 0.0f;

        if (config.capture_method == "winrt")
        {
            const uint64_t winrtPolls = captureWinrtPollAttemptsTotal.load(std::memory_order_relaxed);
            const uint64_t winrtDrained = captureWinrtFramesDrainedTotal.load(std::memory_order_relaxed);
            const uint64_t winrtReturned = captureWinrtFramesReturnedTotal.load(std::memory_order_relaxed);
            const uint64_t winrtEmpty = captureWinrtEmptyPollsTotal.load(std::memory_order_relaxed);
            const uint64_t winrtReadbackMicros = captureWinrtReadbackMicrosTotal.load(std::memory_order_relaxed);
            const uint64_t winrtMapMicros = captureWinrtMapMicrosTotal.load(std::memory_order_relaxed);
            const uint64_t winrtPixelCopyMicros = captureWinrtPixelCopyMicrosTotal.load(std::memory_order_relaxed);

            if (!winrtStatsInitialized)
            {
                lastWinrtPolls = winrtPolls;
                lastWinrtDrained = winrtDrained;
                lastWinrtReturned = winrtReturned;
                lastWinrtEmpty = winrtEmpty;
                lastWinrtReadbackMicros = winrtReadbackMicros;
                lastWinrtMapMicros = winrtMapMicros;
                lastWinrtPixelCopyMicros = winrtPixelCopyMicros;
                lastWinrtStatsTime = now;
                winrtStatsInitialized = true;
            }
            else if (now - lastWinrtStatsTime >= 1.0)
            {
                const float dt = static_cast<float>(std::max(0.001, now - lastWinrtStatsTime));
                const uint64_t returnedDelta = winrtReturned - lastWinrtReturned;
                winrtPollRate = static_cast<float>(winrtPolls - lastWinrtPolls) / dt;
                winrtDrainedRate = static_cast<float>(winrtDrained - lastWinrtDrained) / dt;
                winrtReturnedRate = static_cast<float>(returnedDelta) / dt;
                winrtEmptyRate = static_cast<float>(winrtEmpty - lastWinrtEmpty) / dt;
                if (returnedDelta > 0)
                {
                    winrtReadbackAvgMs = static_cast<float>(winrtReadbackMicros - lastWinrtReadbackMicros) /
                        (1000.0f * static_cast<float>(returnedDelta));
                    winrtMapAvgMs = static_cast<float>(winrtMapMicros - lastWinrtMapMicros) /
                        (1000.0f * static_cast<float>(returnedDelta));
                    winrtPixelCopyAvgMs = static_cast<float>(winrtPixelCopyMicros - lastWinrtPixelCopyMicros) /
                        (1000.0f * static_cast<float>(returnedDelta));
                }

                lastWinrtPolls = winrtPolls;
                lastWinrtDrained = winrtDrained;
                lastWinrtReturned = winrtReturned;
                lastWinrtEmpty = winrtEmpty;
                lastWinrtReadbackMicros = winrtReadbackMicros;
                lastWinrtMapMicros = winrtMapMicros;
                lastWinrtPixelCopyMicros = winrtPixelCopyMicros;
                lastWinrtStatsTime = now;
            }

            ImGui::Separator();
            ImGui::Text("WinRT frames: %.1f/s | pulled: %.1f/s", winrtReturnedRate, winrtDrainedRate);
            ImGui::Text("WinRT empty polls: %.1f/s | polls: %.1f/s", winrtEmptyRate, winrtPollRate);
            ImGui::Text("WinRT readback avg: %.3f ms | Map: %.3f ms", winrtReadbackAvgMs, winrtMapAvgMs);
            ImGui::Text("WinRT memcpy avg: %.3f ms", winrtPixelCopyAvgMs);
        }
        else
        {
            winrtStatsInitialized = false;
        }

#ifdef USE_CUDA
        if (config.backend == "TRT")
        {
            const bool depthMaskEnabled = config.depth_inference_enabled && config.depth_mask_enabled;
            const bool canUseCudaCapture = (config.capture_method == "duplication_api");
            const bool directCaptureActive =
                canUseCudaCapture &&
                config.capture_use_cuda &&
                !depthMaskEnabled;

            std::string directCaptureStatus;
            if (!canUseCudaCapture)
                directCaptureStatus = "N/A (requires duplication_api)";
            else if (!config.capture_use_cuda)
                directCaptureStatus = "Disabled by user";
            else if (depthMaskEnabled)
                directCaptureStatus = "CPU fallback (depth mask is enabled)";
            else
                directCaptureStatus = "Active";

            ImGui::Separator();
            ImGui::Text("CUDA Direct Capture: %s", config.capture_use_cuda ? "enabled" : "disabled");
            ImGui::Text("Depth mask: %s", depthMaskEnabled ? "on" : "off");
            ImGui::Text("Capture pipeline: %s", directCaptureActive ? "GPU direct path" : "CPU readback");

            if (directCaptureActive)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 1.0f, 0.45f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.28f, 1.0f));
            ImGui::TextWrapped("Direct capture status: %s", directCaptureStatus.c_str());
            ImGui::PopStyleColor();

            static uint64_t lastGpuAttempts = 0;
            static uint64_t lastGpuCaptured = 0;
            static uint64_t lastGpuTimeouts = 0;
            static uint64_t lastGpuAccumulated = 0;
            static uint64_t lastGpuMissed = 0;
            static uint64_t lastGpuPresent = 0;
            static uint64_t lastGpuMouseOnly = 0;
            static uint64_t lastGpuMetadataOnly = 0;
            static uint64_t lastGpuCoalesced = 0;
            static uint64_t lastCpuFallbackAttempts = 0;
            static uint64_t lastCpuFallbackFrames = 0;
            static double lastGpuStatsTime = 0.0;
            static float gpuAttemptRate = 0.0f;
            static float gpuCapturedRate = 0.0f;
            static float gpuTimeoutRate = 0.0f;
            static float gpuAccumulatedRate = 0.0f;
            static float gpuMissedRate = 0.0f;
            static float gpuPresentRate = 0.0f;
            static float gpuMouseOnlyRate = 0.0f;
            static float gpuMetadataOnlyRate = 0.0f;
            static float gpuCoalescedRate = 0.0f;
            static float cpuFallbackAttemptRate = 0.0f;
            static float cpuFallbackFrameRate = 0.0f;

            const uint64_t gpuAttempts = captureGpuAttemptsTotal.load(std::memory_order_relaxed);
            const uint64_t gpuCaptured = captureGpuCapturedTotal.load(std::memory_order_relaxed);
            const uint64_t gpuTimeouts = captureGpuTimeoutTotal.load(std::memory_order_relaxed);
            const uint64_t gpuAccumulated = captureGpuAccumulatedFramesTotal.load(std::memory_order_relaxed);
            const uint64_t gpuMissed = captureGpuMissedFramesTotal.load(std::memory_order_relaxed);
            const uint64_t gpuPresent = captureGpuPresentFramesTotal.load(std::memory_order_relaxed);
            const uint64_t gpuMouseOnly = captureGpuMouseOnlyEventsTotal.load(std::memory_order_relaxed);
            const uint64_t gpuMetadataOnly = captureGpuMetadataOnlyEventsTotal.load(std::memory_order_relaxed);
            const uint64_t gpuCoalesced = captureGpuCoalescedEventsTotal.load(std::memory_order_relaxed);
            const uint64_t cpuFallbackAttempts = captureCpuFallbackAttemptsTotal.load(std::memory_order_relaxed);
            const uint64_t cpuFallbackFrames = captureCpuFallbackFramesTotal.load(std::memory_order_relaxed);

            if (lastGpuStatsTime <= 0.0)
            {
                lastGpuAttempts = gpuAttempts;
                lastGpuCaptured = gpuCaptured;
                lastGpuTimeouts = gpuTimeouts;
                lastGpuAccumulated = gpuAccumulated;
                lastGpuMissed = gpuMissed;
                lastGpuPresent = gpuPresent;
                lastGpuMouseOnly = gpuMouseOnly;
                lastGpuMetadataOnly = gpuMetadataOnly;
                lastGpuCoalesced = gpuCoalesced;
                lastCpuFallbackAttempts = cpuFallbackAttempts;
                lastCpuFallbackFrames = cpuFallbackFrames;
                lastGpuStatsTime = now;
            }
            else if (now - lastGpuStatsTime >= 1.0)
            {
                const float dt = static_cast<float>(std::max(0.001, now - lastGpuStatsTime));
                gpuAttemptRate = static_cast<float>(gpuAttempts - lastGpuAttempts) / dt;
                gpuCapturedRate = static_cast<float>(gpuCaptured - lastGpuCaptured) / dt;
                gpuTimeoutRate = static_cast<float>(gpuTimeouts - lastGpuTimeouts) / dt;
                gpuAccumulatedRate = static_cast<float>(gpuAccumulated - lastGpuAccumulated) / dt;
                gpuMissedRate = static_cast<float>(gpuMissed - lastGpuMissed) / dt;
                gpuPresentRate = static_cast<float>(gpuPresent - lastGpuPresent) / dt;
                gpuMouseOnlyRate = static_cast<float>(gpuMouseOnly - lastGpuMouseOnly) / dt;
                gpuMetadataOnlyRate = static_cast<float>(gpuMetadataOnly - lastGpuMetadataOnly) / dt;
                gpuCoalescedRate = static_cast<float>(gpuCoalesced - lastGpuCoalesced) / dt;
                cpuFallbackAttemptRate = static_cast<float>(cpuFallbackAttempts - lastCpuFallbackAttempts) / dt;
                cpuFallbackFrameRate = static_cast<float>(cpuFallbackFrames - lastCpuFallbackFrames) / dt;

                lastGpuAttempts = gpuAttempts;
                lastGpuCaptured = gpuCaptured;
                lastGpuTimeouts = gpuTimeouts;
                lastGpuAccumulated = gpuAccumulated;
                lastGpuMissed = gpuMissed;
                lastGpuPresent = gpuPresent;
                lastGpuMouseOnly = gpuMouseOnly;
                lastGpuMetadataOnly = gpuMetadataOnly;
                lastGpuCoalesced = gpuCoalesced;
                lastCpuFallbackAttempts = cpuFallbackAttempts;
                lastCpuFallbackFrames = cpuFallbackFrames;
                lastGpuStatsTime = now;
            }

            ImGui::Text("DDA submitted frames: %.1f/s | attempts: %.1f/s", gpuCapturedRate, gpuAttemptRate);
            ImGui::Text("DDA present frames: %.1f/s | mouse-only: %.1f/s", gpuPresentRate, gpuMouseOnlyRate);
            ImGui::Text("DDA metadata-only: %.1f/s | coalesced: %.1f/s", gpuMetadataOnlyRate, gpuCoalescedRate);
            ImGui::Text("DDA GPU timeouts: %.1f/s | accumulated: %.1f/s", gpuTimeoutRate, gpuAccumulatedRate);
            ImGui::Text("DDA GPU missed/coalesced: %.1f/s", gpuMissedRate);
            ImGui::Text("DDA CPU fallback: %.1f/s | attempts: %.1f/s", cpuFallbackFrameRate, cpuFallbackAttemptRate);
        }
#endif

        OverlayUI::EndSection();
    }
}
