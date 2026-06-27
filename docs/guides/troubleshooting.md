# Troubleshooting Order

Use this order when something feels wrong:

1. Confirm build family: DML or CUDA.
2. Confirm model type: `.onnx` for DML, `.engine` for TensorRT, or `.onnx` when you want CUDA to generate a new engine.
3. Confirm capture is showing the expected content.
4. Confirm selected `input_method` has its required runtime/device.
5. Turn on useful logs or diagnostics.
6. Rebuild through the matching wrapper and run a real DML or CUDA smoke test after source changes.

More specific guides:

- [Backend selection and checks](backends.md)
- [UDP capture](udp-capture.md)
- [Capture diagnostics](capture-diagnostics.md)
- [Input methods](input-methods.md)
- [Overlay and GUI behavior](overlay.md)
- [Build workflow](build-workflow.md)

## Detection Works, But Aim Does Not Move

Common symptoms:

- Detection works, but aim does not move in game.
- ESP or preview shows the target, but aim and auto-shoot do not work.
- Auto-shoot sometimes works, but auto-aim does not.
- The GUI opens with `Home`, but the game still ignores movement.
- Users try `WIN32`, `GHUB`, `MAKCU`, Arduino, or KMBOX without knowing which part failed.

If boxes or preview detections are visible, capture and model loading are usually already working. Check the input chain next.

`WIN32` is the standard Windows input path. Some games ignore or block Win32 synthetic mouse events, so `WIN32` can work on the desktop while doing nothing inside the game. When that happens, changing model confidence, FOV, or smoothing will not fix movement.

Use this order:

1. Check the console line for the selected method, for example `[Mouse] Using WIN32 input.`
2. If `WIN32` is selected, test movement outside the game.
3. If movement works outside the game but not in game, use another `input_method`.
4. For games that block Win32 input, connect and configure a supported external input device or runtime, such as Arduino/RP2350/Teensy, KMBOX, MAKCU, G HUB, or Razer.
5. After switching methods, confirm the device-specific logs show a connected/open state. The app does not silently fall back to `WIN32` when a hardware/runtime method fails.

See [Input methods](input-methods.md) for setup details and method-specific checks.
