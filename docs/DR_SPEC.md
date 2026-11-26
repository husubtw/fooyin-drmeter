# DR Meter Plugin Technical Specification (`fyplugin_drmeter`)

## 1. Scope & Goal
Offline Dynamic Range (`TT DR14`) scanner plugin for Fooyin `0.15.1`, matching the measurement behavior, log format, and results dialog layout of Foobar2000's `foo_dr_meter v1.0.8`.

- **Source of Truth:** `foo_dr_meter v1.0.8` reference behavior (DR14 block windowing, AES17 RMS, 80-character `foo_dr.txt` layout, and results dialog columns).
- **Out of Scope (v0.1):** Real-time playback meter widget and a dedicated settings UI page (settings UI exposure via `AdvancedSettingsRegistry` is planned for v0.2).

---

## 2. Architecture

### 2.1. `drmeter_core` (Static Library — Pure C++23)
No `Fooyin::*`, Qt, or codec library dependencies.
- `DrAnalyzer`: Streaming per-channel DR14, AES17 RMS ($\sqrt{2}$ / `+3.0103 dB` factor), and sample peak calculator (`process(std::span<const double> interleaved, size_t frameCount)` + `finish()`).
- Result structs (`drtypes.h`): Per-channel metrics (`drDb`, `rmsDb`, `peakDb`, `channelLabel`, `isLfe`), track result (`trackDrInt`, `trackDrFloat`, `peakDb`, `rmsDb`, `totalFrames`, `durationSec`, format metadata), and album result (`officialDr`, track list).
- `DrLogFormatter`: Generates `foo_dr.txt` logs and tabular clipboard text (`Copy Results`).

### 2.2. `drmeter_plugin_logic` (Static Library — Non-GUI Plugin Logic)
Links `drmeter_core`, `Fooyin::Core`, and `Fooyin::Utils`.
- `DrScanner` / `DrWorker`: `QThread` + `QtConcurrent::map` + `std::stop_token`, decoding via `AudioLoader::loadDecoderForTrack(track, options)` and `Audio::convert(buffer, f64Format)`. Uses `AudioDecoder::NoLooping | AudioDecoder::NoInfiniteLooping` for bounded or offset segments (e.g., CUE subsongs) and `AudioDecoder::NoSeeking | AudioDecoder::NoInfiniteLooping` for standard full-file tracks.
- `DrAlbumGrouper`: Groups tracks using Fooyin's `ScriptParser` with default pattern `%album artist% | %date% | %album% | %discnumber%`.
- `DrLogExporter`: Collects valid albums, plans per-directory log writes with overwrite detection, and performs atomic UTF-8 file writes via `QSaveFile`.
- `DrResultsModel`: `QAbstractTableModel` exposing track/album DR and dynamic per-channel metrics.
- `DrTagWriter`: Extra-tag writer for `DYNAMIC RANGE` and `ALBUM DYNAMIC RANGE`.
- `DrSettings`: Reads and writes plugin settings through `SettingsManager::fileValue()` / `fileSet()`.

### 2.3. `fyplugin_drmeter` (Fooyin Plugin Module — `create_fooyin_plugin`)
Links `drmeter_plugin_logic`, `Fooyin::Core`, `Fooyin::Gui`, and `Fooyin::Utils`; implements `CorePlugin` + `GuiPlugin`.
- `DrMeterPlugin`: Context menu registration (`TrackSelectionController` / `ActionManager`) and scan/dialog lifecycle management.
- `DrProgressDialog`: Non-modal scan progress and cancellation dialog.
- `DrResultsDialog`: Results dialog presenting `DrResultsModel` in a `QTableView` with tag-write, log-export, and clipboard actions.

---

## 3. DSP & Math Specification (`TT DR14` / `foo_dr_meter v1.0.8`)

### 3.1. Blocking & RMS Definitions (`foo_dr_meter v1.0.8` Aligned)
- **Input:** Normalized interleaved `double` samples (nominal `[-1.0, +1.0]`; lossy/float sources may exceed `1.0` — never clipped to `1.0`).
- **Block size:** Non-overlapping ~3-second blocks matching `foo_dr_meter v1.0.8` across all sample rates:
  - `blockFrames = max(1, floor(sampleRate * (132480.0 / 44100.0)))` (`3.0040816326530613` s):
    - `44100 Hz` → `132480`, `88200 Hz` → `264960`, `176400 Hz` → `529920`, `352800 Hz` → `1059840`
    - `48000 Hz` → `144195`, `96000 Hz` → `288391`
- **Trailing partial block:**
  - When the track has at least one full block (`fullBlocks >= 1`) and remaining samples (`rem > 0`), the trailing partial block is included as the final block $B - 1$, zero-padded to `blockFrames` for block RMS (actual peak for block peak).
  - When the track is shorter than one block (`fullBlocks == 0`, `rem > 0`), the single partial block is normalized by its actual frame count `N`.
- **Whole-Track Peak & RMS (for table and `foo_dr.txt`):**
  - Computed across all samples of the track (including any trailing partial block).
  - Per-channel sample peak: $P_c = \max_i \vert{}x_{c,i}\vert{}$, $\text{Peak}_c = 20 \log_{10}(P_c)\text{ dBFS}$.
  - Per-channel AES17 RMS (with $\sqrt{2}$ / `+3.0103 dB` factor, matching `foo_dr_meter v1.0.8` block-mean RMS):
    - When `fullBlocks >= 1`: $R_c = \sqrt{\frac{2}{B \cdot L} \sum_i x_{c,i}^2}$, $\text{RMS}_c = 20 \log_{10}(R_c)\text{ dBFS}$ (where $B$ is total block count including any trailing partial block and $L = \text{blockFrames}$).
    - When `fullBlocks == 0`: $R_c = \sqrt{\frac{2}{N} \sum_i x_{c,i}^2}$.
  - Track peak: $\max_c \text{Peak}_c$.
  - Track RMS (power average across channels): $10 \log_{10}\!\left(\frac{1}{C} \sum_{c=1}^{C} R_c^2\right)\text{ dBFS}$.
- **Per-Channel DR (3-second blocks):**
  - For each block $b \in [0, B)$ and channel $c$:
    - Block peak: $p_{c,b} = \max_{i \in b} \vert{}x_{c,i}\vert{}$.
    - Block AES17 RMS (with $\sqrt{2}$ / `+3.0103 dB` factor):
      $$r_{c,b} = \sqrt{\frac{2}{N_b} \sum_{i \in b} x_{c,i}^2}$$
      where $N_b = L$ for full blocks and zero-padded trailing blocks (`fullBlocks >= 1`), or $N_b = N$ for tracks shorter than one block.
  - Sort block RMS values $r_{c,b}$ descending per channel using exact floating-point sorting (no 10,000-bin `[0, 1]` histogram clipping); take top 20%:
    - $k = \max(1, \lfloor 0.20 \cdot B \rfloor)$.
    - $\text{RMS}_{\text{top20}, c} = \sqrt{\frac{1}{k} \sum_{j=1}^{k} r_{c,(j)}^2}$.
  - Second highest block peak: sort block peaks descending and pick $p_{c,(2)}$ (fallback to $p_{c,(1)}$ if $B < 2$).
  - Channel DR:
    $$\text{DR}_c = 20 \log_{10}\!\left(\frac{p_{c,(2)}}{\text{RMS}_{\text{top20}, c}}\right)$$
    (If $\text{DR}_c < 0$, recompute using $p_{c,(1)}$; clamp final result to $\ge 0.0$.)
- **Track DR & Album DR:**
  - For multichannel tracks, exclude `AudioFormat::ChannelPosition::LFE` and `AudioFormat::ChannelPosition::LFE2` from DR averaging when non-LFE channels exist.
  - Track DR (float): arithmetic mean of active channels' $\text{DR}_c$.
  - Track DR (int): `static_cast<int>(std::lround(trackDrFloat))`.
  - Official Album DR: `static_cast<int>(std::lround(mean(trackDrFloat)))` — arithmetic mean of the unrounded `trackDrFloat` values across tracks in the album group, then rounded to the nearest integer (matching `foo_dr_meter v1.0.8`).

### 3.2. Edge Cases
- **Tracks shorter than 3 seconds ($B = 1$, 0 full blocks):** Evaluate the single partial block normalized by actual frame count $N$ ($p_{(1)}$ and single-block AES17 RMS); never emit `NaN`.
- **Silent track or silent channel:** Guard against $\log_{10}(0)$; yield `DR = 0.0` and Peak/RMS `-inf`, never `NaN`.
- **Non-finite samples:** `NaN`, `±Inf`, and finite samples whose square overflows `double` (`!std::isfinite(sample * sample)`) are skipped during accumulation in `DrAnalyzer::process()` while preserving frame counts and block boundaries, preventing `NaN` propagation and strict-weak-ordering UB in `std::ranges::sort`.
- **Duration rounding:** Format `M:SS` using `std::llround(totalFrames / static_cast<double>(sampleRate))`.
- **Negative zero:** Values with `-0.005 < v <= 0.0` format as `0.00 dBFS`, never `-0.00 dBFS`.
- **Reference Signal Expectations (AES17 RMS convention matching `foo_dr_meter v1.0.8`):**
  - Full-scale 0 dBFS sine wave (1 kHz, 12 s, stereo): `Peak = 0.00 dBFS`, `RMS = 0.00 dBFS`, `DR = 0`.
  - Full-scale 0 dBFS square wave: `Peak = 0.00 dBFS`, `RMS = +3.01 dBFS`, `DR = 0`.
  - Known crest-factor pulse signal: matches expected DR within ±0.01 dB.

### 3.3. Reference Parity Criteria (`foo_dr_meter v1.0.8`)
- **Standard lossless PCM (e.g., 44.1 kHz / 16-bit stereo FLAC):** Exact integer Track DR and Official Album DR; track and per-channel Peak/RMS within ±0.01 dB; per-channel DR within ±0.01–0.025 dB (exact floating-point top-20% sorting avoids `foo_dr_meter`'s 10,000-bin `[0, 1]` RMS histogram clipping on `> 0 dBFS` float signals).
- **Single-file image + CUE sheet (multi-subsong):** Sample-exact CUE subsong slicing via `_CUE_INDEX01_SECTOR` / `_CUE_END_SECTOR` (75 CD sectors/sec) with fallback to `seek(offset)` + `duration`; full-scale peaks formatted as `0.00 dBFS` (never `-0.00 dBFS`); all CUE subsongs skipped during tag writes.
- **DSF / DSD64 tracks:** Decoded through Fooyin's `AudioLoader` (`SampleFormat::F64` conversion); Track and Official DR consistent within ±1 DR of `foo_dr_meter` (absolute dBFS levels may differ due to DSD→PCM filter/gain differences); ID3v2 tag writing supported via TagLib 2+.

---

## 4. Album Grouping & Tag Writing

### 4.1. Album Grouping
- Default grouping pattern: `%album artist% | %date% | %album% | %discnumber%` evaluated via Fooyin's `ScriptParser`.
- Fallback key when all fields are empty: parent directory path.
- Each album group computes its own `Official DR value`.

### 4.2. Tag Writing (`Update File Tags`)
- Extra tag keys (uppercase, plain integer string without `"DR"` prefix):
  - `DYNAMIC RANGE` = `"13"`
  - `ALBUM DYNAMIC RANGE` = `"13"`
- Single-track scans write both `DYNAMIC RANGE` and `ALBUM DYNAMIC RANGE` by default (controlled by `DrMeter/WriteAlbumDrForSingleTrack`).
- **Write Filter:**
  `!track.hasCue() && !track.isInArchive() && !track.isRemote() && audioLoader->canWriteMetadata(track)`
- Update only these two fields via `Track::replaceExtraTag(...)`; preserve all other existing tags.
- Commit via `MusicLibrary::writeTrackMetadata(tracksToWrite)` and monitor `WriteRequest::finished` (`QFuture<WriteResult>`).
- **Failure & cancellation behavior:**
  - If `writeTrackMetadata()` returns an invalid `WriteRequest` (`!finished.isValid()`) or a non-cancelled future finishes with no result, all targeted tracks are reported as `failed` (`updated = 0`), never as success.
  - Calling `DrTagWriter::cancel()` disconnects the `QFutureWatcher` from `DrTagWriter`, and closing or rejecting `DrResultsDialog` disconnects `DrTagWriter` from the dialog before cancelling any in-flight write, so queued or un-cancellable completions cannot display a `QMessageBox` on a closing dialog.
- Display a completion summary (e.g., `"Updated 7 files"` or `"Updated 0 files, skipped 8 (CUE/read-only)"`).

---

## 5. Log Format & `Write Log` Behavior

### 5.1. `foo_dr.txt` Format
80-character separator layout compatible with `foo_dr_meter v1.0.8` and `dr.loudness-war.info` (C-locale, `.` decimal separator):

```text
fooyin <fooyin_version> / DR Meter v<plugin_version>
log date: YYYY-MM-DD HH:MM:SS

--------------------------------------------------------------------------------
Analyzed: <Album Artist> / <Album>
--------------------------------------------------------------------------------

DR         Peak           RMS       Duration Track
--------------------------------------------------------------------------------
DR11      -2.23 dBFS   -16.35 dBFS     10:43 01-Track One
DR13       0.00 dBFS   -15.91 dBFS      4:22 02-Track Two
--------------------------------------------------------------------------------

Number of tracks:  2
Official DR value: DR12

Samplerate:        44100 Hz
Channels:          2
Bits per sample:   16
Bitrate:           750 kbps
Codec:             FLAC
================================================================================
```
- **Track label format:** Zero-padded 2-digit track number + hyphen + title (`01-Track One`); fall back to `<Title>` alone if track number is missing/zero.
- **Bit-depth reporting (`buildTrackMetadata()`):** Lossy tracks (`track.encoding()` equal to `"Lossy"`, case-insensitive) and unprobed floating-point streams (`SampleFormat::F32`, `SampleFormat::F64`, or `SampleFormat::Unknown` with `track.bitDepth() <= 0`) set `bitsPerSample = 0`, so `foo_dr.txt` omits the `Bits per sample:` line. Non-lossy tracks use `track.bitDepth()` when `> 0`; fallback to `decodedFormat.bitdepth()` applies only to integer PCM formats (`U8`, `S16`, `S24In32`, `S32`).
- If an album contains mixed sample rates, bit depths, or codecs, list all unique values comma-separated; report average bitrate in kbps.
- For multi-album logs written to the same file/clipboard, concatenate album blocks separated by a blank line after `================================================================================`.

### 5.2. `Write Log` Behavior
- **Single album:** Open `QFileDialog::getSaveFileName` defaulting to `<album_dir>/foo_dr.txt`.
- **Multiple albums:**
  - Group album blocks by target directory.
  - If multiple albums/discs share the same directory, concatenate their blocks into one `foo_dr.txt` so they do not overwrite each other.
  - Show a single confirmation dialog listing all target `foo_dr.txt` paths (explicitly flagging any existing files that will be overwritten). Upon confirmation, write all files as UTF-8 and report the result.

---

## 6. User Interface Specification

### 6.1. Entry Point
- Register `Dynamic Range Meter` action in the track selection context menu (`TrackSelectionController` / `ActionManager`, following `rgscanner`).

### 6.2. Progress Dialog
- Shows progress count (`3 / 7`), current track name, progress bar, and `Cancel` button.
- Clicking `Cancel` triggers `std::stop_source::request_stop()` and closes without opening the results dialog. If a queued `DrWorker::scanFinished` arrives after cancellation, `DrScanner` suppresses `scanFinished` and emits `scanCancelled`.

### 6.3. `DR Meter Results` Dialog (`foo_dr_meter v1.0.8` Layout)
- **Table Columns (`QTableView` + `DrResultsModel`):**
  - `Name`, `Track DR`, `Album DR`
  - Dynamic per-channel columns derived from `AudioFormat::channelLayout()` (e.g., `FL`, `FR` for stereo):
    - `DR (FL)`, `DR (FR)`, ... (`12.92 dB`)
    - `RMS (FL)`, `RMS (FR)`, ... (`-20.16 dBFS`)
    - `Peak (FL)`, `Peak (FR)`, ... (`-1.92 dBFS` / `0.00 dBFS`)
- **Bottom Bar:**
  - Left: `Measured in: M:SS.mmm, speed: XXXX.XXx` (wall-clock elapsed time and total audio duration / elapsed time ratio).
  - Right Buttons: `Update File Tags`, `Write Log`, `Copy Log`, `Copy Results`, `Close`.

---

## 7. Settings (`DrSettings`)
Settings are persisted in Fooyin's `SettingsManager` under the following keys and defaults:
- `DrMeter/AlbumGroupingPattern`: `"%album artist% | %date% | %album% | %discnumber%"`
- `DrMeter/LogFileName`: `"foo_dr.txt"`
- `DrMeter/VerboseDialog`: `true`
- `DrMeter/WriteAlbumDrForSingleTrack`: `true`

Exposing these keys in Fooyin's settings UI via `GuiPluginContext::advancedSettingsRegistry` (`Tools / DR Meter`) is planned for v0.2.
