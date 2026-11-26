# Contributing

## Building & Testing

Use CMake presets (`CMakePresets.json`) with Ninja:

```bash
# Release plugin build (no GTest required)
cmake --preset release && cmake --build --preset release

# Development build with unit + integration tests
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer build & test
cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure

# Standalone drmeter_core build & test (no Fooyin required)
cmake -B build-core -G Ninja -DDRMETER_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build-core && ctest --test-dir build-core --output-on-failure
```

## Code Quality Checks

```bash
# Formatting (must produce zero diffs)
find src tests -type f \( -name '*.h' -o -name '*.cpp' \) -exec clang-format --dry-run --Werror {} +

# Static analysis across src/ (requires `cmake --preset dev` for compile_commands.json)
clang-tidy -p build src/core/*.cpp src/plugin/*.cpp

# Verify drmeter_core has zero Fooyin/Qt/FFmpeg dependencies
nm -u build/libdrmeter_core.a | grep -iE 'fooyin|qt|qstring|avcodec|avformat' || echo "CLEAN"
```

## Pull Request Expectations

- Keep `drmeter_core` pure C++23 with no `Fooyin::*`, Qt, or codec library dependencies.
- Include targeted unit or integration regression tests for every bug fix or behavior change.
- Ensure CTest (`dev` and `asan`), `clang-format`, and `clang-tidy` pass with zero warnings.
