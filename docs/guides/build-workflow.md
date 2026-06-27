# Build Workflow for Everyday Changes

Use the full builder when setting up a backend or changing dependencies:

```powershell
.\BUILDER.bat
```

Use the no-options builder after the backend already builds and you only changed app code:

```powershell
.\build_no-options.bat
```

The no-options builder only asks DML or CUDA and then builds the existing CMake target. It does not download, restore, update, or rebuild OpenCV.

Project builds should always go through the provided batch wrappers:

```powershell
.\build_dml.bat
.\build_cuda.bat
```

Related docs:

- [Build from source](../build.md)
- [Common recipes](recipes.md)
