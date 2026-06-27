# First Launch Checklist

Use this checklist for a fresh unpacked build or a fresh local build.

1. Pick the right build:
   - Use **DML** if you want easiest setup or non-NVIDIA compatibility.
   - Use **CUDA + TensorRT** if you have the NVIDIA CUDA/TensorRT stack ready.
2. Put the detector model in the `models` folder beside `ai.exe`:
   - DML uses `.onnx`.
   - TensorRT normally uses `.engine`; a CUDA build can also build an `.engine` from a selected `.onnx`.
3. Start the app once so `config.ini` is generated.
4. Open the GUI or overlay and save settings from there when possible.
5. Confirm the selected `input_method` matches the device/control path you actually want.

Related docs:

- [Backend selection](backends.md)
- [Configuration guide](../config.md)
- [Build from source](../build.md)
