# Common Recipes

## Lowest Setup Burden

```ini
backend = DML
ai_model = your_model.onnx
input_method = WIN32
circle_fov_enabled = true
```

## CUDA/TensorRT Performance Test

```ini
backend = TRT
ai_model = your_model.engine
capture_method = duplication_api
capture_use_cuda = true
show_window = false
collect_data_while_playing = false
```

Then check `[CaptureDiag]` output.

## Provider Benchmark

Use this when you want repeatable provider timings without starting capture, overlay, input devices, or file logging:

```powershell
.\ai.exe --benchmark-providers
.\ai.exe --benchmark-providers cpu,dml-gpu --bench-runs 200 --bench-warmup 20
.\ai.exe --benchmark-providers dml-gpu,dml-cpu --bench-model models\your_model.onnx
.\ai.exe --benchmark-providers cuda --bench-cuda-model models\your_model.engine
```

The benchmark prints one final CSV-style summary in seconds. DML builds support `cpu`, `dml-gpu`, and `dml-cpu`; CUDA builds support `cuda` through TensorRT.

DML benchmark runs append rows to `benchmark_results\provider_benchmark.csv`; CUDA benchmark runs append rows to `benchmark_results\provider_benchmark_cuda.csv`. Use `--bench-no-save` for a disposable run.

## Razer Control Test

```ini
input_method = RAZER
```

Make sure `rzctl.dll` is beside `ai.exe` or in:

```text
NeuralAim
```

## Teensy Control Test

```ini
input_method = TEENSY41_HID
teensy_hid_serial = AUTO
teensy_hid_vid_filter = AUTO
teensy_hid_pid_filter = AUTO
```

Start broad with `AUTO`, then narrow filters after the device is confirmed.

Related docs:

- [Capture diagnostics](capture-diagnostics.md)
- [Input methods](input-methods.md)
- [Build workflow](build-workflow.md)
