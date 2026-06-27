# Build From Source

This guide keeps the useful dependency and model-export notes from the older build
guide, but updates the actual build steps for the current wrapper-based build
system.

Use the provided batch wrappers for project builds. The wrappers set up the
PowerShell scripts, Visual Studio environment, Ninja, backend-specific
dependencies, CMake generator, and build configuration.

## 1. Choose a Backend

| Backend | Use when | Model format |
|---|---|---|
| DML | You want the easiest Windows GPU path or non-NVIDIA support. | `.onnx` |
| CUDA + TensorRT | You have a supported NVIDIA GPU and want the fastest backend. | `.engine`, or `.onnx` for first-time engine generation |

The two builds are separated at compile time. CUDA builds do not compile or link
DirectML/ONNX Runtime files, and DML builds do not compile CUDA/TensorRT files.

## 2. Requirements

Required for both builds:

- Windows 10 or Windows 11 x64.
- Visual Studio or Visual Studio Build Tools with the Desktop development with C++ workload.
- Windows SDK with C++/WinRT headers.
- CMake.
- PowerShell.
- Internet access for first-time dependency setup, unless everything is already prepared locally.

The scripts can download or restore several project dependencies. They cannot
install Visual Studio, GPU drivers, or NVIDIA account-gated SDKs for you.

### DML Requirements

- A GPU and driver that support DirectML.
- NuGet packages restored by the scripts:
  - `Microsoft.ML.OnnxRuntime.DirectML`
  - `Microsoft.AI.DirectML`
- OpenCV DML layout under:

```text
NeuralAim\modules\opencv\build\dml
```

The DML wrapper can prepare the OpenCV layout automatically.

### CUDA Requirements

- Supported NVIDIA GPU. GTX 10xx/Pascal and older are not supported by the current TensorRT path.
- CUDA Toolkit 13.1 or newer at runtime. The current dependency resolver prefers CUDA 13.2 + TensorRT 10.16 when available, with CUDA 13.1 + TensorRT 10.14 as a fallback profile.
- TensorRT 10 Windows binary SDK, extracted under:

```text
NeuralAim\modules\TensorRT-*
```

- OpenCV 4.13.0 built with CUDA support under:

```text
NeuralAim\modules\opencv\build\cuda\install
```

cuDNN is optional for this project. The CUDA OpenCV helper disables OpenCV DNN
CUDA/cuDNN by default because inference uses TensorRT, not OpenCV DNN.

## 3. First-Time Build

From the repository root, run the interactive launcher:

```powershell
.\BUILDER.bat
```

Choose `DML` or `CUDA` when prompted. For a first build, allow downloads/updates
unless you have already prepared all dependencies.

Direct backend wrappers are also available:

```powershell
.\build_dml.bat
.\build_cuda.bat
```

Non-interactive examples:

```powershell
.\build_dml.bat -NonInteractive -OpenCvAlreadyBuilt $false -DownloadOrUpdateNeeded $true
.\build_cuda.bat -NonInteractive -OpenCvAlreadyBuilt $false -DownloadOrUpdateNeeded $true -OpenBrowserForDownloads
```

Useful CUDA options:

```powershell
.\build_cuda.bat -CudaArchBin 8.6
.\build_cuda.bat -CudaArchBin all
.\build_cuda.bat -SkipOpenCvBuild -OpenCvAlreadyBuilt $true
```

`-CudaArchBin all` expands to:

```text
7.5;8.0;8.6;8.7;8.8;8.9;9.0;10.0;10.3;11.0;12.0;12.1
```

For a broad release build, keep the multi-architecture CUDA set but limit OpenCV
to the modules used by the app and reduce OpenCV build parallelism:

```powershell
.\build_cuda.bat -CudaArchBin all -OpenCvAlreadyBuilt $false -DownloadOrUpdateNeeded $true -OpenCvBuildList cudev,core,imgproc,imgcodecs,videoio,highgui,dnn,cudaarithm,cudaimgproc,cudawarping -OpenCvMaxCpuCount 2 -OpenCvCleanBuild
```

This still produces a CUDA OpenCV build for the `all` architecture preset, but
avoids compiling unused contrib CUDA modules such as `cudafilters`.
Use `-OpenCvCleanBuild` when changing the OpenCV module list so the old OpenCV
CMake cache cannot keep stale module state.

## 4. Fast Local Rebuild

After a full wrapper build has configured the selected backend once, use:

```powershell
.\build_no-options.bat -Backend DML
.\build_no-options.bat -Backend CUDA
```

This only runs the existing CMake build tree:

```text
cmake --build build\<backend> --config Release --target ai --parallel
```

It does not restore packages, download dependencies, rebuild OpenCV, or refresh
CMake cache paths. If dependency paths changed or the build tree is stale, run
`BUILDER.bat`, `build_dml.bat`, or `build_cuda.bat` again.

## 5. What the Wrappers Prepare

The full build wrappers do the setup work that used to be handled manually:

- Import the Visual Studio compiler environment through `VsDevCmd.bat`.
- Find or cache Ninja.
- Restore NuGet packages for DML builds.
- Download or prepare `SimpleIni.h` and the embedded `serial` module when missing.
- Prepare DML OpenCV or build CUDA OpenCV.
- Resolve only the selected backend dependencies.
- Write `build\dependency-resolution.json` for debugging.
- Configure CMake with `Ninja Multi-Config`.
- Build `ai.exe`.

The top-level `CMakeLists.txt` intentionally accepts only Ninja generators for
the automated build path. Do not use the old Visual Studio CMake generator command
as the normal build path.

## 6. Dependency Layout

The wrappers can prepare most of this layout, but it is useful to know what the
project expects:

| Dependency | Expected location |
|---|---|
| SimpleIni | `NeuralAim\modules\SimpleIni.h` |
| serial | `NeuralAim\modules\serial\` |
| TensorRT | `NeuralAim\modules\TensorRT-*\` |
| DML OpenCV | `NeuralAim\modules\opencv\build\dml\` |
| CUDA OpenCV | `NeuralAim\modules\opencv\build\cuda\install\` |
| DML NuGet packages | `packages\Microsoft.ML.OnnxRuntime.DirectML.*` and `packages\Microsoft.AI.DirectML.*` |

The Visual Studio project file is not the source of truth for dependency paths.
Use the wrapper-generated CMake configuration.

## 7. Output and Runtime Files

Default outputs:

```text
build\dml\Release\ai.exe
build\cuda\Release\ai.exe
```

On startup, `ai.exe` changes the working directory to its own folder and creates
runtime folders such as:

```text
models
depth_models
```

Put detector models in the `models` folder beside `ai.exe`. Put depth models in
the `depth_models` folder beside `ai.exe`. The `screenshots` folder is created
only when a screenshot is saved.

When `AIMBOT_COPY_RUNTIME_DLLS` is enabled, CMake copies available runtime DLLs
next to `ai.exe`, including:

- OpenCV runtime DLL.
- `ghub_mouse.dll`, when present.
- `rzctl.dll`, when present.
- ONNX Runtime and DirectML DLLs for DML builds.
- TensorRT and optional cuDNN DLLs for CUDA builds.

## 8. Advanced CMake Overrides

Prefer wrapper arguments over manual CMake commands. Extra CMake cache variables
can be passed through the batch wrappers when needed:

```powershell
.\build_dml.bat -DAIMBOT_OPENCV_DML_ROOT=C:/path/to/opencv/dml
.\build_cuda.bat -DAIMBOT_TENSORRT_ROOT=C:/path/to/TensorRT-10.x -DAIMBOT_CUDNN_ROOT=C:/path/to/cudnn
```

Useful cache variables:

| Variable | Meaning |
|---|---|
| `AIMBOT_USE_CUDA` | `ON` for CUDA + TensorRT, `OFF` for DML. Set by wrappers. |
| `AIMBOT_COPY_RUNTIME_DLLS` | Copies runtime DLLs beside `ai.exe`. |
| `AIMBOT_OPENCV_DML_ROOT` | DML OpenCV build root. |
| `AIMBOT_OPENCV_CUDA_ROOT` | CUDA OpenCV install root. |
| `AIMBOT_TENSORRT_ROOT` | TensorRT SDK root. |
| `AIMBOT_CUDNN_ROOT` | Optional cuDNN root. |
| `AIMBOT_RZCTL_DLL` | Source path for `rzctl.dll`. |
| `AIMBOT_CPPWINRT_INCLUDE_DIR` | C++/WinRT include directory, if auto-detection fails. |

## 9. Exporting AI Models

Convert PyTorch `.pt` YOLO models to ONNX with Ultralytics:

```bash
pip install ultralytics -U

# TensorRT/CUDA source ONNX. The app can build a matching .engine from this.
yolo export model=your_model.pt format=onnx dynamic=true simplify=true

# DML source ONNX.
yolo export model=your_model.pt format=onnx simplify=true
```

For DML, place the exported `.onnx` in `models` beside `ai.exe` and select it in
the overlay.

For CUDA, place the exported `.onnx` in `models` beside `ai.exe`. When the CUDA
backend loads an `.onnx` model and the matching `.engine` file is missing, it
builds and saves the `.engine` next to the `.onnx`, then updates `config.ini` to
use the generated engine.

Depth models are separate. Put depth `.onnx` files in `depth_models` and use the
Depth section in the overlay to export a TensorRT depth engine when needed.

## 10. Validation

After source or build-system changes, rebuild the backend you changed through the
matching wrapper and run `ai.exe` from the output folder. Prefer a real DML or
CUDA smoke test over static source-shape checks, because the architecture changes
often.

For repeatable provider timings, run the provider benchmark from the output
folder:

```powershell
.\ai.exe --benchmark-providers
.\ai.exe --benchmark-providers cpu,dml-gpu --bench-runs 200 --bench-warmup 20
.\ai.exe --benchmark-providers cuda --bench-cuda-model models\your_model.engine
```

The benchmark prints a final CSV-style summary in seconds and does not write a
log file. DML builds benchmark ONNX Runtime providers (`cpu`, `dml-gpu`,
`dml-cpu`) and append to `benchmark_results\provider_benchmark.csv`. CUDA builds
benchmark only TensorRT/CUDA and append to
`benchmark_results\provider_benchmark_cuda.csv`. Use `--bench-no-save` for
console-only runs or `--bench-results <path>` for a different CSV path.

## 11. Troubleshooting

| Problem | What to check |
|---|---|
| `Build tree not found` from `build_no-options` | Run the full wrapper for that backend first. |
| CMake rejects the generator | Use the wrappers. The automated path expects Ninja or Ninja Multi-Config. |
| DML OpenCV missing | Run `.\build_dml.bat` with downloads enabled, or set `AIMBOT_OPENCV_DML_ROOT`. |
| CUDA dependency missing | Install CUDA Toolkit and extract the TensorRT Windows binary SDK under `NeuralAim\modules`, then rerun `.\build_cuda.bat`. |
| TensorRT archive extracted but not detected | Make sure you downloaded the Windows binary SDK archive, not TensorRT source code. The layout must contain `include\NvInfer.h`, `lib\nvinfer_10.lib`, and `bin\nvinfer_10.dll`. |
| OpenCV CUDA build fails | Check CUDA, MSVC, OpenCV 4.13.0, contrib sources, and CUDA architecture. Retry through `.\build_cuda.bat` so the helper uses the same settings. |
| Missing `rzctl.dll` | Keep `NeuralAim\rzctl.dll` in the repo or copy it beside `ai.exe`. |
| DML runs but detects nothing | Confirm the selected model is `.onnx`, lower `confidence_threshold`, and verify class IDs. |
| CUDA engine fails to load | Delete the stale `.engine`, select the `.onnx`, and let the CUDA backend rebuild the engine for the current TensorRT/CUDA stack. |
