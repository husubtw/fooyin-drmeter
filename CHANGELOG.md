# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-11-27

### Added
- Fooyin `Settings → Advanced → Tools → DR Meter` settings UI via `AdvancedSettingsRegistry` (`Album grouping pattern`, `Log file name`, `Verbose results dialog`, `Write ALBUM DYNAMIC RANGE for single-track albums`).
- `DrMeter/VerboseDialog` toggle to switch `DrResultsDialog` between compact 3-column and full per-channel views.
- Tri-state numeric column sorting (`Ascending → Descending → Original scan order`) in `DrResultsDialog` with `Error` rows and empty channel cells pinned to the bottom.
- `Ctrl+C` and table context-menu `Copy` for selected rows (TSV in visible order), and order-aware `Copy Results`.
- Single-source generated `core/drversion.h` header from CMake `PROJECT_VERSION` and `FOOYIN_MIN_VERSION`.
- GitHub Release workflow (`.github/workflows/release.yml`) publishing explicitly ABI-tagged Ubuntu 24.04 (`Fooyin 0.15.1` / `Qt 6.4`) `.fyplugin` bundles and `SHA256SUMS.txt`.
- LeakSanitizer check for standalone `drmeter_core_tests` in Arch CI.

### Fixed
- `DrSettings` / `DrLogExporter`: reject path separators, `:`, `.`, `..`, and control characters in `DrMeter/LogFileName` and refuse directory targets in `writeSingleLogFile()`.
- `DrAlbumGrouper` / `DrLogExporter`: prevent remote, virtual, malformed archive, or empty paths from falling back to `QDir::currentPath()` during multi-album log export; filter out `Track::isVirtual()` tracks before scanning.
- `DrResultsModel`: sanitize channel labels and failed track names consistently with `DrLogFormatter::sanitizeText()`.

## [0.1.2] - 2026-11-26

### Added
- Expanded integration and unit tests covering CUE sector slicing (`_CUE_INDEX01_SECTOR` / `_CUE_END_SECTOR`), bounded `offset + duration` seeking, 0-frame EOF failure exclusion, partial-failure album DR averaging, `channelLayoutFromAudioFormat()` (`LFE`/`LFE2`), `DrSettings` defaults/fallbacks/`writeAlbumDrForSingleTrack`, and UTF-8-safe log/TSV sanitization.
- Strict ASan/UBSan (`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`) and `clang-tidy --warnings-as-errors='*'` gates in the Arch Linux CI job.
- Example `foo_dr.txt` log (`docs/foo_dr_example.txt`), collapsible log preview, badges, and quick-reference build/install table in `README.md`.

### Changed
- Removed unused `DrMeter/VerboseDialog` setting (`SettingKeys::VerboseDialog` and `DrSettings` accessors) and synced `docs/DR_SPEC.md` accordingly.
- Updated `.github/ISSUE_TEMPLATE/bug_report.yml` (`fooyin-drmeter` name and `Fedora 45` example).

### Fixed
- `DrScanner`: mark 0-frame EOF decodes (`analyzer.totalFrames() == 0`) as failed (`"No audio frames decoded"`) so empty/broken streams are excluded from Official Album DR, logs, and tag writes.
- `DrMeterPlugin`: clear `Qt::WA_DeleteOnClose` before closing and synchronously delete surviving `DrResultsDialog` and `DrProgressDialog` instances in `shutdown()`.
- `DrLogFormatter`: distinguish `+inf`, `-inf`, and `nan` in `formatNumericDb()`, sanitize ASCII control characters (`\r`/`\n`/`\t` → space, strip `< 0x20` / `0x7F` via `unsigned char` so UTF-8 bytes are preserved), and trim leading/trailing ASCII spaces on metadata strings so control-only values fall back cleanly.

## [0.1.1] - 2026-11-26

### Added
- `CMakePresets.json` with `release`, `dev`, and `asan` configure/build/test presets.
- `DRMETER_BUILD_PLUGIN` CMake option to allow standalone `drmeter_core` builds without Fooyin installed.
- `DRMETER_PLUGIN_INSTALL_DIR` CMake cache variable for cross-distro user-local installs (e.g., overriding Fedora's `lib64/fooyin/plugins` default to `lib/fooyin/plugins` under `~/.local`).
- Opt-in `DRMETER_PACKAGE_BUNDLE` CMake option (`package_plugin` target) to build `.fyplugin` bundles for Fooyin's GUI installer, AppImage, and Flatpak.
- `.clang-tidy` configuration, 3-distro GitHub Actions CI workflow (Arch Linux, Ubuntu 24.04 via official Fooyin 0.15.1 Linux SDK, Fedora 45), `CONTRIBUTING.md`, and issue templates.

### Changed
- Default `BUILD_TESTING` to `OFF` so end users can build the plugin without GoogleTest installed.
- Require Fooyin `>= 0.15.1` in `find_package(Fooyin 0.15.1 REQUIRED)` and explicitly link `Fooyin::Utils`.
- Updated `README.md` with cross-distro Linux build/install instructions and aligned `docs/DR_SPEC.md` with the codebase.

### Fixed
- `DrTagWriter`: report failure instead of false success when `MusicLibrary::writeTrackMetadata()` returns an invalid `QFuture`, and guard `QFutureWatcher::result()` on empty results.
- `DrResultsDialog` / `DrTagWriter`: disconnect `m_tagWriter` on dialog close/reject and disconnect `m_watcher` on cancel so queued or un-cancellable completions cannot open a `QMessageBox` on a closing dialog.
- `DrAnalyzer`: skip non-finite (`NaN`, `±Inf`, or square-overflow) floating-point samples in `process()` to prevent accumulator poisoning and strict-weak-ordering UB in `std::ranges::sort`.
- `DrScanner`: omit `Bits per sample` for lossy tracks and un-probed floating-point decodes instead of falling back to `decodedFormat.bitdepth()` (`32`).
- `DrScanner` / `DrMeterPlugin`: suppress `scanFinished` and emit `scanCancelled` if a queued scan completion arrives after cancellation, preventing `DrResultsDialog` from opening after the user clicks Cancel.
- `DrScanner`: declare `stopGuard` strictly before `std::stop_callback` after `decoder->start()` so `~stop_callback()` finishes before `decoder->stop()` runs, preventing a concurrent `CarrierDecoder::interruptRead()` data race / UAF.

## [0.1.0] - 2026-11-26

### Added
- Initial release of `fyplugin_drmeter` for Fooyin 0.15.1.
- Pure C++23 `drmeter_core` library implementing TT DR14 measurement (`foo_dr_meter v1.0.8` parity) and 80-column `foo_dr.txt` / tabular clipboard formatting.
- Multi-threaded offline `DrScanner` supporting standard lossless/lossy PCM, sample-exact CUE subsong slicing, and DSF/DSD64 decoding via Fooyin's `AudioLoader`.
- `DrResultsDialog`, `DrProgressDialog`, multi-album directory log export (`DrLogExporter`), and safe `DYNAMIC RANGE` / `ALBUM DYNAMIC RANGE` tag writing (`DrTagWriter`).

[Unreleased]: https://github.com/husubtw/fooyin-drmeter/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/husubtw/fooyin-drmeter/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/husubtw/fooyin-drmeter/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/husubtw/fooyin-drmeter/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/husubtw/fooyin-drmeter/releases/tag/v0.1.0
