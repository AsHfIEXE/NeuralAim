# Backend Selection and Checks

## Choosing DML vs CUDA

### Use DML When

- You want the least complicated Windows GPU path.
- You are testing compatibility.
- You do not want to install CUDA and TensorRT.
- You are using an ONNX model.

### Use CUDA + TensorRT When

- You have an NVIDIA GPU.
- CUDA Toolkit and TensorRT are installed.
- You want the highest-performance backend.
- You are using a TensorRT `.engine` model, or you have an `.onnx` ready for first-time engine generation.

## DML Runs But Does Not Detect Anything

Check these first:

```ini
backend = DML
ai_model = your_model.onnx
confidence_threshold = 0.10
class_player = 0
class_head = 1
```

Common causes:

- The selected model is a TensorRT `.engine` instead of `.onnx`.
- `confidence_threshold` is too high for the model.
- The model class IDs do not match `class_player` and `class_head`.
- `dml_device_id` points to the wrong GPU.
- The capture source is not actually showing the target content.

## CUDA Runs But GPU Usage Spikes

Start by checking which features force CPU-readable frames:

- Debug/preview window: `show_window = true`.
- Data collection.
- Screenshots.
- Any feature that needs pixels on the CPU for display or saving.

For the current CUDA path, the recommended FOV limiter is:

```ini
circle_fov_enabled = true
```

If the spike disappears when the GUI or overlay is open, compare the capture diagnostics in both states. The GUI/preview can change whether the app requests CPU copies, which can make the runtime path different from the closed-GUI path.

Related docs:

- [Capture diagnostics](capture-diagnostics.md)
- [Circle FOV](circle-fov.md)
- [Common recipes](recipes.md)
