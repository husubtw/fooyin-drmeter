# fooyin-drmeter

Offline Dynamic Range (`TT DR14`) analyzer plugin (`fyplugin_drmeter.so`) for [Fooyin](https://github.com/fooyin/fooyin) `0.15.1`, matching the measurement accuracy, `foo_dr.txt` log layout, and results dialog of Foobar2000's `foo_dr_meter v1.0.8`.

## Features
- **Accurate TT DR14 DSP core (`drmeter_core`):** Pure C++23 streaming engine with per-channel DR, AES17 RMS, unclipped sample peak (including `> 0 dBFS` float peaks), multichannel LFE exclusion, and locale-independent C-locale numeric formatting.
- **Parallel Fooyin decode pipeline:** Decodes via Fooyin's `AudioLoader` (`SampleFormat::F64`) using `QThread` + `QtConcurrent::map` with cooperative cancellation (`std::stop_token`), sample-exact CUE subsong slicing, and native DSF/DSD support.
- **`DR Meter Results` dialog:** Dynamic per-channel columns (`FL`, `FR`, surround layouts), elapsed time & speed readout, safe `Update File Tags` (`DYNAMIC RANGE` & `ALBUM DYNAMIC RANGE`), single/multi-album `Write Log` (`foo_dr.txt`), `Copy Log`, and `Copy Results`.

## Requirements
- Fooyin `0.15.1` SDK (`/usr/include/fooyin/`, `/usr/lib/cmake/fooyin/`)
- Qt 6 (`Core`, `Widgets`, `Concurrent`, `Sql`, `Network`)
- CMake `>= 3.21`, Ninja, C++23 compiler (GCC or Clang)
- Optional (for running tests): GoogleTest (`GTest::gtest_main`)

## Build & Test
```bash
# Standard build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Sanitizer build (ASan + LSan + UBSan)
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## Run & Install

### Run without installing
```bash
FOOYIN_PLUGIN_PATH="$PWD/build" fooyin
```

### User-local install
```bash
cmake --install build --prefix ~/.local
```
This installs `fyplugin_drmeter.so` to `~/.local/lib/fooyin/plugins/fyplugin_drmeter.so`.

## Usage
1. Select one or more tracks or albums in Fooyin.
2. Right-click and choose **Dynamic Range Meter** (also bindable under Fooyin Shortcuts as `DrMeter.MeasureDynamicRange`).
3. Inspect per-track and per-channel DR/RMS/Peak values, then optionally update file tags, export `foo_dr.txt`, or copy logs/results to the clipboard.

## Tag Writing & CUE Safety
- **Update File Tags** writes uppercase extra tags `DYNAMIC RANGE` and `ALBUM DYNAMIC RANGE` as plain integers (e.g., `13`) while preserving all other existing tags.
- **CUE, archive, remote, and read-only tracks are skipped safely:** CUE subsongs (both external `.cue` and embedded cuesheets) are analyzed with sample-exact boundaries, but tag writes are intentionally skipped so a parent single-file image (e.g., `.flac`) is never overwritten by subsong values. The completion dialog reports updated, failed, and skipped counts.

## License
This project is licensed under the **GNU General Public License v3.0 or later (`GPL-3.0-or-later`)** — see [LICENSE](LICENSE) for the full license text.
