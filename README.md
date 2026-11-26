# fooyin-drmeter

[![CI](https://github.com/husubtw/fooyin-drmeter/actions/workflows/ci.yml/badge.svg)](https://github.com/husubtw/fooyin-drmeter/actions/workflows/ci.yml)

Offline Dynamic Range (`TT DR14`) scanner plugin for [Fooyin](https://github.com/fooyin/fooyin) `0.15.1`. Measurement behavior, 80-column `foo_dr.txt` log layout, and results dialog columns follow foobar2000's `foo_dr_meter v1.0.8`.

## Features

- Per-track and Official Album TT DR14 measurement grouped by album tags (falling back to directory path when untagged).
- Per-channel DR, AES17 RMS, and unclipped sample peak (including `> 0 dBFS` float peaks) for mono, stereo, and multichannel tracks, excluding LFE channels from the DR average.
- Sample-exact CUE subsong analysis (external `.cue` and embedded cuesheets) and DSF/DSD decoding.
- Single- and multi-album `foo_dr.txt` export with directory grouping and overwrite confirmation, plus clipboard export for logs and TSV tables.
- Extra-tag writing for `DYNAMIC RANGE` and `ALBUM DYNAMIC RANGE`.

Technical details and math definitions are documented in [docs/DR_SPEC.md](docs/DR_SPEC.md).

## Compatibility & Requirements

- **Tested platforms:** Linux with Fooyin `0.15.1` (Arch Linux, Fedora 45, and Ubuntu 24.04 via the official Fooyin Linux SDK).
- **Plugin ABI:** Fooyin plugins are native C++ binaries bound to the Fooyin plugin IID and Qt 6 ABI; build against the installed Fooyin/Qt 6 version and rebuild after Fooyin upgrades.
- **Untested platforms:** Windows and FreeBSD are untested in v0.1 (PRs welcome).
- **Toolchain:** C++23 compiler with `<format>` (GCC 13+ or equivalent Clang; Debian 12 Bookworm's default GCC 12 is too old), Qt 6.4+, CMake >= 3.21, Ninja, and Fooyin `0.15.1` headers/SDK. GoogleTest is optional and only required when `-DBUILD_TESTING=ON`.

## Build & Install

### Arch Linux (and derivatives such as CachyOS, Manjaro)

```bash
sudo pacman -S --needed fooyin cmake ninja base-devel
cmake --preset release
cmake --build --preset release
cmake --install build-release --prefix ~/.local
```

This installs `fyplugin_drmeter.so` to `~/.local/lib/fooyin/plugins/fyplugin_drmeter.so`.

### Fedora (45+ with native `fooyin-devel` 0.15.1)

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel fooyin-devel
cmake --preset release -DDRMETER_PLUGIN_INSTALL_DIR=lib/fooyin/plugins
cmake --build --preset release
cmake --install build-release --prefix ~/.local
```

Fedora's `fooyin-devel` defaults `FOOYIN_PLUGIN_INSTALL_DIR` to `lib64/fooyin/plugins`, which works for system-wide packaging (`--prefix /usr`). For user-local installs under `~/.local`, `-DDRMETER_PLUGIN_INSTALL_DIR=lib/fooyin/plugins` is required because Fooyin only scans `~/.local/lib/fooyin/plugins`.

### Ubuntu 24.04 (using Fooyin's official Linux SDK)

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config ca-certificates curl xz-utils \
  qt6-base-dev libqt6svg6 libqt6sql6-sqlite \
  libavcodec60 libavfilter9 libavformat60 libavutil58 libswresample4 \
  libicu74 libtag1v5 libarchive13t64 zlib1g libqcoro6core0t64 libqcoro6network0t64

curl --fail --location --output fooyin-sdk.tar.xz \
  https://github.com/fooyin/fooyin/releases/download/v0.15.1/fooyin-sdk-0.15.1-linux-x64-qt6.4.tar.xz
echo "5b90b16628cdcd8f9a58ad087b03f58da1174948e475ab077d84fd22eeb1b090  fooyin-sdk.tar.xz" | sha256sum -c -
mkdir -p fooyin-sdk
tar -xJf fooyin-sdk.tar.xz -C fooyin-sdk --strip-components=1

cmake --preset release -DCMAKE_PREFIX_PATH="$PWD/fooyin-sdk"
cmake --build --preset release
cmake --install build-release --prefix ~/.local
```

These package names and the official SDK tarball target Ubuntu 24.04 (`noble`) SONAMEs; Debian 13 and other derivatives need matching distro packages or a local Fooyin build.

### Building a `.fyplugin` Bundle (GUI Install / AppImage / Flatpak)

```bash
cmake -S . -B build-bundle -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DDRMETER_PACKAGE_BUNDLE=ON
cmake --build build-bundle --target package_plugin
```

(Pass `-DCMAKE_PREFIX_PATH="$PWD/fooyin-sdk"` when building against the SDK tarball.)

This produces `build-bundle/fooyin-drmeter-linux-x64.fyplugin`, which can be installed via Fooyin's plugin installer. Fooyin extracts user plugins to `~/.local/lib/fooyin/plugins` for native packages and AppImage, and `~/.var/app/org.fooyin.fooyin/.local/lib/fooyin/plugins` for Flatpak. The bundle must still match the target Fooyin/Qt 6 ABI.

### Run Without Installing

```bash
FOOYIN_PLUGIN_PATH="$PWD/build-release" fooyin
```

## Usage

1. Select one or more tracks or albums in Fooyin.
2. Right-click and choose **Dynamic Range Meter** (also bindable in Fooyin Shortcuts as `DrMeter.MeasureDynamicRange`).
3. Review track and per-channel DR/RMS/Peak values, then optionally run **Update File Tags**, **Write Log**, **Copy Log**, or **Copy Results**.

## Behavior Notes

- **Tag writing:** **Update File Tags** writes only `DYNAMIC RANGE` and `ALBUM DYNAMIC RANGE` as plain integers (e.g., `13`) and preserves all other tags.
- **CUE and read-only safety:** CUE subsongs (external and embedded), archive entries, remote streams, and read-only formats are skipped during tag writes so single-file images are never overwritten.
- **Lossy formats:** Lossy tracks (MP3, Opus, Ogg, AAC) omit the `Bits per sample:` line in `foo_dr.txt`.
- **DSF / DSD:** DSD tracks are decoded to PCM through Fooyin; DR values stay within ±1 DR of `foo_dr_meter`, while absolute dBFS peak/RMS levels may differ depending on DSD-to-PCM filter gain.

## Development & Documentation

```bash
# Development build + unit and integration tests
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer build & test
cmake --preset asan && cmake --build --preset asan && ctest --preset asan --output-on-failure

# Standalone core library build & tests (no Fooyin required)
cmake -B build-core -G Ninja -DDRMETER_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build-core && ctest --test-dir build-core --output-on-failure
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for formatting/lint checks and [docs/DR_SPEC.md](docs/DR_SPEC.md) for the full technical specification.

## License

Licensed under `GPL-3.0-or-later`. See [LICENSE](LICENSE).
