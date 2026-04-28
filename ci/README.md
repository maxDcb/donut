# Donut MSVC CI

`build-test-msvc.ps1` builds the Donut `debug` target with MSVC, creates a small native test executable, asks `donut.exe` to generate `loader.bin` plus the debug-only `instance` file, and runs `loader.exe instance`.

Targets:

- `x64`: builds with `VsDevCmd -arch=amd64`, generates `DONUT_ARCH_X64`, and runs the debug loader smoke test.
- `x84`: builds with `VsDevCmd -arch=amd64`, generates `DONUT_ARCH_X84`, and runs the debug loader smoke test.
- `arm64`: builds with `VsDevCmd -arch=arm64` using `Makefile_arm64.msvc`. Runtime execution is skipped until Donut has a real ARM64 loader architecture.

The ARM64 makefile looks for `lib\aplibarm64.lib`. If it is missing, it links `ci\aplib_stub.c` so non-aPLib flows can keep compiling while ARM64 support is being ported. `DONUT_COMPRESS_APLIB` intentionally returns `APLIB_ERROR` through the stub.

aPLib's public package documents C depacker sources, but Donut's generator also needs the packer entry points declared in `include\aplib.h`. For real ARM64 compression support, provide a native ARM64 library exporting those symbols as `lib\aplibarm64.lib`.
