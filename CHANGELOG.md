# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `CMakePresets.json` with `release`, `dev`, and `asan` configure/build/test presets.
- `DRMETER_BUILD_PLUGIN` CMake option to allow standalone `drmeter_core` builds without Fooyin installed.
- `.clang-tidy` configuration, GitHub Actions CI workflow, `CONTRIBUTING.md`, and issue templates.

### Changed
- Default `BUILD_TESTING` to `OFF` so end users can build the plugin without GoogleTest installed.

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
