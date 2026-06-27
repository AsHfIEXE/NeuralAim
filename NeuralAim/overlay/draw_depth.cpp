#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <commdlg.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <algorithm>
#include <exception>
#include <thread>
#include <atomic>
#include <mutex>

#include "imgui/imgui.h"
#include "NeuralAim.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "capture.h"
#include "draw_settings.h"
#include "include/other_tools.h"
#include "overlay/ui_sections.h"

#ifdef USE_CUDA
#include "overlay/export_progress_panel.h"
#include "depth/depth_anything_trt.h"
#include "depth/depth_mask.h"
#include "tensorrt/nvinf.h"
#include "tensorrt/trt_monitor.h"
#endif

#ifdef USE_CUDA
static const char* kDepthColormapNames[] = {
    "Autumn",
    "Bone",
    "Jet",
    "Winter",
    "Rainbow",
    "Ocean",
    "Summer",
    "Spring",
    "Cool",
    "HSV",
    "Pink",
    "Hot",
    "Parula",
    "Magma",
    "Inferno",
    "Plasma",
    "Viridis",
    "Cividis",
    "Twilight",
    "Twilight Shifted",
    "Turbo",
    "Deepgreen"
};

#endif

void draw_depth()
{
#ifndef USE_CUDA
    if (OverlayUI::BeginSection("Depth", "depth_section_unavailable"))
    {
        ImGui::TextUnformatted("Depth requires a CUDA build.");
        OverlayUI::EndSection();
    }
    return;
#else
    static std::string depthStatus = "Depth runtime is managed automatically.";
    static std::atomic<bool> depthExportRunning{ false };
    static std::thread depthExportThread;
    static std::mutex depthExportMutex;
    static std::string depthExportResult;
    static std::string depthExportedModel;

    if (depthExportThread.joinable() && !depthExportRunning.load())
    {
        depthExportThread.join();
    }
    std::string completedEngineModel;
    {
        std::lock_guard<std::mutex> lock(depthExportMutex);
        if (!depthExportResult.empty())
        {
            depthStatus = depthExportResult;
            completedEngineModel = depthExportedModel;
            depthExportResult.clear();
            depthExportedModel.clear();
        }
    }
    if (!completedEngineModel.empty() && config.depth_model_path != completedEngineModel)
    {
        config.depth_model_path = completedEngineModel;
        OverlayConfig_MarkDirty();
    }

    std::vector<std::string> availableDepthModels = getAvailableDepthModels();
    std::string selectedModel;
    bool hasModels = !availableDepthModels.empty();

    if (OverlayUI::BeginSection("Depth Inference", "depth_section_inference"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("Enable Depth Inference");
            if (ImGui::Checkbox("##enable_depth_inference", &config.depth_inference_enabled))
            {
                OverlayConfig_MarkDirty();
                if (!config.depth_inference_enabled)
                    depthStatus = "Depth inference disabled.";
            }
            OverlayUI::EndSettingRow(row);
        }

        if (!hasModels)
        {
            OverlayUI::TextRow("No depth models available in 'depth_models'.", IM_COL32(255, 108, 108, 255));
        }
        else
        {
            int currentModelIndex = 0;
            auto it = std::find(availableDepthModels.begin(), availableDepthModels.end(), config.depth_model_path);
            if (it == availableDepthModels.end())
            {
                std::string configFile = std::filesystem::path(config.depth_model_path).filename().string();
                it = std::find(availableDepthModels.begin(), availableDepthModels.end(), configFile);
            }
            if (it != availableDepthModels.end())
            {
                currentModelIndex = static_cast<int>(std::distance(availableDepthModels.begin(), it));
            }

            std::vector<const char*> modelItems;
            modelItems.reserve(availableDepthModels.size());
            for (const auto& modelName : availableDepthModels)
            {
                modelItems.push_back(modelName.c_str());
            }

            const auto row = OverlayUI::BeginSettingRow("Depth model");
            if (ImGui::Combo("##depth_model", &currentModelIndex, modelItems.data(), static_cast<int>(modelItems.size())))
            {
                if (config.depth_model_path != availableDepthModels[currentModelIndex])
                {
                    config.depth_model_path = availableDepthModels[currentModelIndex];
                    OverlayConfig_MarkDirty();
                }
            }
            OverlayUI::EndSettingRow(row);

            selectedModel = availableDepthModels[currentModelIndex];
        }

        const bool selectedIsOnnx = hasModels && OtherTools::HasExtensionCaseInsensitive(selectedModel, ".onnx");
        const bool exportBusy = depthExportRunning.load();
        if (!hasModels || selectedIsOnnx || exportBusy)
        {
            ImGui::BeginDisabled();
        }
        {
            const auto row = OverlayUI::BeginSettingRow("Load depth model");
            if (ImGui::Button("Load", ImVec2(row.controlWidth, 0.0f)))
            {
                if (config.depth_model_path != selectedModel)
                {
                    config.depth_model_path = selectedModel;
                    OverlayConfig_MarkDirty();
                    depthStatus = "Depth model path applied. Runtime loader will update automatically.";
                }
                else
                {
                    depthStatus = "Depth model path already selected.";
                }
            }
            OverlayUI::EndSettingRow(row);
        }
        if (!hasModels || selectedIsOnnx || exportBusy)
        {
            ImGui::EndDisabled();
        }

        if (!hasModels || !selectedIsOnnx || exportBusy)
        {
            ImGui::BeginDisabled();
        }
        {
            const auto row = OverlayUI::BeginSettingRow("Export depth engine");
            if (ImGui::Button("Export", ImVec2(row.controlWidth, 0.0f)))
            {
                if (!depthExportRunning.load())
                {
                    if (config.depth_model_path != selectedModel)
                    {
                        config.depth_model_path = selectedModel;
                        OverlayConfig_MarkDirty();
                    }

                    std::string exportPath = selectedModel;
                    if (exportPath.empty())
                    {
                        depthStatus = "Set a depth ONNX path to export.";
                    }
                    else if (!OtherTools::HasExtensionCaseInsensitive(exportPath, ".onnx"))
                    {
                        depthStatus = "Export expects an .onnx depth model path.";
                    }
                    else
                    {
                        depthExportRunning.store(true);
                        depthStatus = "Depth engine export started...";
                        depthExportThread = std::thread([exportPath] {
                            depth_anything::DepthAnythingTrt exporter;
                            std::string result;
                            std::string exportedModel;
                            try
                            {
                                std::string enginePath;
                                if (exporter.exportEngine(exportPath, gLogger, &enginePath))
                                {
                                    exportedModel = std::filesystem::path(enginePath).filename().string();
                                    result = "Depth engine exported: " + exportedModel;
                                }
                                else
                                {
                                    if (gTrtExportCancelRequested.load())
                                    {
                                        result = "Depth export canceled.";
                                    }
                                    else
                                    {
                                        result = exporter.lastError();
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                result = std::string("Depth export failed: ") + e.what();
                                exportedModel.clear();
                            }
                            catch (...)
                            {
                                result = "Depth export failed with an unknown error.";
                                exportedModel.clear();
                            }
                            {
                                std::lock_guard<std::mutex> lock(depthExportMutex);
                                depthExportResult = result;
                                depthExportedModel = exportedModel;
                            }
                            depthExportRunning.store(false);
                        });
                    }
                }
            }
            OverlayUI::EndSettingRow(row);
        }
        if (!hasModels || !selectedIsOnnx || exportBusy)
        {
            ImGui::EndDisabled();
        }

        if (exportBusy)
        {
            OverlayExportUI::DrawTensorRtExportPanel(
                "depth_tensor_rt_export",
                "Depth engine export",
                "Compiling optimized depth inference engine",
                selectedModel.c_str(),
                "Cancel depth export");
        }

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Depth Runtime", "depth_section_runtime"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("Depth FPS");
            if (ImGui::SliderInt("##depth_fps", &config.depth_fps, 0, 120))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask FPS");
            if (ImGui::SliderInt("##depth_mask_fps", &config.depth_mask_fps, 1, 30))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }
        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Depth Mask", "depth_section_mask"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("Enable Depth Mask");
            if (ImGui::Checkbox("##enable_depth_mask", &config.depth_mask_enabled))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask Near %");
            if (ImGui::SliderInt("##depth_mask_near_percent", &config.depth_mask_near_percent, 1, 100))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask Expand (px)");
            if (ImGui::SliderInt("##depth_mask_expand", &config.depth_mask_expand, 0, 128))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask Hold Frames");
            if (ImGui::SliderInt("##depth_mask_hold_frames", &config.depth_mask_hold_frames, 0, 120))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask Alpha");
            if (ImGui::SliderInt("##depth_mask_alpha", &config.depth_mask_alpha, 0, 255))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }
        if (config.depth_mask_enabled && config.depth_mask_alpha == 0)
        {
            OverlayUI::TextRow("Depth mask is invisible: alpha is 0.", IM_COL32(255, 108, 108, 255));
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Mask Invert");
            if (ImGui::Checkbox("##depth_mask_invert", &config.depth_mask_invert))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("Depth Debug Overlay (Game)");
            if (ImGui::Checkbox("##depth_debug_overlay_game", &config.depth_debug_overlay_enabled))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        int colormapIndex = config.depth_colormap;
        {
            const auto row = OverlayUI::BeginSettingRow("Depth colormap");
            if (ImGui::Combo("##depth_colormap", &colormapIndex, kDepthColormapNames, IM_ARRAYSIZE(kDepthColormapNames)))
            {
                config.depth_colormap = colormapIndex;
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("Depth Status", "depth_section_status"))
    {
        ImGui::Text("Status: %s", depthStatus.c_str());

        if (config.depth_inference_enabled && config.depth_mask_enabled)
        {
            auto& depthMask = depth_anything::GetDepthMaskGenerator();
            const auto state = depthMask.debugState();
            const auto lastErr = depthMask.lastError();
            const auto frameSize = depthMask.lastFrameSize();

            ImGui::Separator();
            ImGui::Text("Mask runtime: %s", state.model_ready ? "ready" : "not ready");
            ImGui::Text("Mask model path: %s",
                state.last_model_path.empty() ? "(none)" : state.last_model_path.c_str());
            if (frameSize.first > 0 && frameSize.second > 0)
                ImGui::Text("Last mask frame: %dx%d", frameSize.first, frameSize.second);

            if (!lastErr.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Mask error: %s", lastErr.c_str());
        }
        else if (config.depth_inference_enabled)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Depth mask is disabled.");
        }
        else
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Depth inference is disabled.");
        }

        ImGui::TextUnformatted("Depth preview appears in game overlay when debug overlay is enabled.");
        OverlayUI::EndSection();
    }
#endif
}
