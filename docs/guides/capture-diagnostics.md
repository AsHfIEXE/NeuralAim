# Capture Diagnostics

When diagnostic logging prints a line like this:

```text
[CaptureDiag] backend=TRT method=duplication_api capture_fps=60 use_cuda=true show_window=true prefer_gpu=false need_cpu_copy=true ...
```

Use these fields:

| Field | Meaning |
|---|---|
| `backend` | Active inference backend, such as `TRT` or `DML`. |
| `method` | Capture method, usually `duplication_api`. |
| `use_cuda` | Whether CUDA capture is allowed by config/build. |
| `show_window` | Whether preview/debug window is open. |
| `prefer_gpu` | Whether the capture path is trying to stay GPU-side. |
| `need_cpu_copy` | Whether some active feature needs CPU-readable pixels. |
| `gpu_attempts` | Number of GPU capture attempts. If this is `0`, the GPU path is not being attempted. |
| `gpu_ok` | Successful GPU capture frames. |
| `gpu_timeout`, `gpu_not_ready`, `gpu_lost` | GPU capture failure categories. |
| `cuda_map_failed`, `cuda_array_failed`, `cuda_copy_failed` | CUDA interop/copy failure categories. |
| `cpu_path_frames` | Frames captured through the CPU path. |
| `trt_cpu_submitted` | Frames sent to TensorRT from CPU-prepared input. |
| `trt_gpu_submitted` | Frames submitted to TensorRT from GPU path. |

For example, this pattern means TensorRT is running, but capture/preprocess is going through CPU frames:

```text
prefer_gpu=false need_cpu_copy=true gpu_attempts=0 cpu_path_frames=6000 trt_cpu_submitted=6000
```

That is not automatically wrong. It means some current setting or feature is selecting a CPU-readable path.

Related docs:

- [Backend selection and checks](backends.md)
- [Overlay and GUI behavior](overlay.md)
- [Data collection](data-collection.md)
