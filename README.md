<div align="center">

# NeuralAim

Native C++ vision-assist tooling for Windows, built around DirectML and CUDA + TensorRT runtime paths.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://github.com/AsHfIEXE/NeuralAim)
[![CUDA 13.1](https://img.shields.io/badge/CUDA-13.1-76B900?style=for-the-badge&logo=nvidia&logoColor=white)](https://developer.nvidia.com/cuda-downloads)
[![Discord](https://img.shields.io/badge/Discord-Join_Server-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/qF8h4CDsyY)
[![GitHub Stars](https://img.shields.io/github/stars/AsHfIEXE/NeuralAim?style=for-the-badge&color=ffb500)](https://github.com/AsHfIEXE/NeuralAim)

<p>
  <a href="https://github.com/AsHfIEXE/NeuralAim/releases" target="_blank">
    <img width="76%" src="https://github.com/AsHfIEXE/ashfiexe_aimbot/blob/main/media/one.gif" alt="NeuralAim preview">
  </a>
</p>

<p>
  <a href="https://github.com/AsHfIEXE/NeuralAim">Repository</a>
  &nbsp;|&nbsp;
  <a href="https://discord.gg/qF8h4CDsyY">Discord Builds</a>
  &nbsp;|&nbsp;
  <a href="docs/build.md">Build Guide</a>
  &nbsp;|&nbsp;
  <a href="docs/config.md">Config Reference</a>
</p>

</div>

---

## Overview

NeuralAim ships with two Windows runtime options:

| Runtime | Best fit | Setup profile |
| --- | --- | --- |
| **DirectML (DML)** | NVIDIA, AMD, Intel, integrated graphics, laptops, and older systems | Universal Windows 10/11 x64 build with no CUDA requirement |
| **CUDA + TensorRT** | Supported NVIDIA GPUs where maximum performance matters | Requires a current NVIDIA driver and CUDA 13.1 |

Precompiled `.exe` builds are distributed through the **pre-releases** channel on Discord:

```text
https://discord.gg/qF8h4CDsyY
```

After downloading a package, unpack it and run `ai.exe`.

## Runtime Support

### DirectML

Choose the DML build when compatibility matters more than NVIDIA-specific acceleration.

| Requirement | Details |
| --- | --- |
| OS | Windows 10/11 x64 |
| GPU | Modern NVIDIA, AMD Radeon, Intel Iris/Xe, or integrated graphics |
| Extra runtime | None |
| Recommended for | GTX 10xx/9xx/7xx, AMD GPUs, Intel GPUs, laptops, and office PCs |

### CUDA + TensorRT

Choose the CUDA build for the fastest supported NVIDIA path.

| Requirement | Details |
| --- | --- |
| OS | Windows 10/11 x64 |
| GPU | GTX 1660 or RTX 2000/3000/4000/5000 |
| Driver | Latest NVIDIA graphics driver |
| CUDA | CUDA 13.1 |
| Not supported | GTX 10xx/Pascal and older because of TensorRT limitations |
| Source-build extra | TensorRT 10.14.1.48 is only needed separately when building from source |

The precompiled CUDA release includes the required CUDA + TensorRT runtime files. Before troubleshooting missing CUDA DLLs or launch-close issues, update the NVIDIA driver and install [CUDA 13.1](https://developer.nvidia.com/cuda-13-1-0-download-archive).

## Quick Start

1. Join the [Discord server](https://discord.gg/qF8h4CDsyY).
2. Download the DML or CUDA package from the **pre-releases** channel.
3. Unpack the build.
4. For CUDA, install or update the NVIDIA driver and [CUDA 13.1](https://developer.nvidia.com/cuda-13-1-0-download-archive).
5. For DML, skip extra runtime setup.
6. Start `ai.exe`.
7. Wait for the first model export. The first launch can take up to 5 minutes.
8. Place your `.onnx` model in the `models` folder.
9. Press `Home`, select the model in the overlay, and tune settings.

## Controls

| Input | Action |
| --- | --- |
| Right Mouse Button | Aim at the detected target |
| F2 | Exit |
| F3 | Pause aiming |
| F4 | Reload config |
| Home | Open or close the overlay and settings |

## Build From Source

Source builds are for advanced users who want to modify NeuralAim or prepare their own runtime package.

| Resource | Link |
| --- | --- |
| Build workflow | [docs/build.md](docs/build.md) |
| CUDA Toolkit | [developer.nvidia.com/cuda-downloads](https://developer.nvidia.com/cuda-downloads) |
| TensorRT docs | [docs.nvidia.com/deeplearning/tensorrt](https://docs.nvidia.com/deeplearning/tensorrt/) |

## Documentation

| Topic | Link |
| --- | --- |
| Config reference | [docs/config.md](docs/config.md) |
| Setup, FAQ, troubleshooting | [docs/guides.md](docs/guides.md) |
| Backend notes | [docs/guides/backends.md](docs/guides/backends.md) |
| Input methods | [docs/guides/input-methods.md](docs/guides/input-methods.md) |
| Data collection | [docs/guides/data-collection.md](docs/guides/data-collection.md) |
| Source config files | [NeuralAim/config/config.cpp](NeuralAim/config/config.cpp), [NeuralAim/config/config.h](NeuralAim/config/config.h) |

## References

- [TensorRT Documentation](https://docs.nvidia.com/deeplearning/tensorrt/)
- [OpenCV Documentation](https://docs.opencv.org/4.x/d1/dfb/intro.html)
- [ImGui](https://github.com/ocornut/imgui)
- [CppWinRT](https://github.com/microsoft/cppwinrt)
- [GLFW](https://www.glfw.org/)
- [WindMouse](https://ben.land/post/2021/04/25/windmouse-human-mouse-movement/)
- [KMBOX](https://www.kmbox.top/)
- [MAKCU](https://makcu.com)
- [depth-anything-tensorrt](https://github.com/spacewalk01/depth-anything-tensorrt)

## License Notes

| Dependency | License |
| --- | --- |
| OpenCV | [Apache License 2.0](https://opencv.org/license.html) |
| ImGui | [MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE) |

## Support And Community

Development is supported through [Boosty](https://boosty.to/ashfiexe) and [Patreon](https://www.patreon.com/c/ashfiexe). Supporters get access to improved and better-trained AI models.

For builds, help, discussion, and contribution questions, join the Discord server:

```text
https://discord.gg/qF8h4CDsyY
```

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=AsHfIEXE/NeuralAim&type=date&legend=top-left)](https://www.star-history.com/#AsHfIEXE/NeuralAim&type=date&legend=top-left)
