# Arc Inflator — Custom Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship "Arc Inflator" — a rebranded standalone VST3 derivative of JS_Inflator with the Twarch GUI recolored to the Arcorrect look + animated starfield, oversampling extended to x16/x32/x64 with independent online/offline factors, and the output fader at −12..+12 dB.

**Architecture:** VST3 SDK 3.7.12 + VSTGUI 4.12, two-part (Processor + Controller) plugin, single `.uidesc` editor. DSP changes are surgical edits to `JSIF_processor.*`; the min-phase oversampling is extended via a **generic loop-based half-band cascade** (a shared 6-stage filter array driven by `log2(factor)`) rather than the existing unrolled per-factor functions, because x64 cannot be hand-unrolled. The linear-phase path adds higher-ratio r8brain resamplers. GUI work is `.uidesc` recolor + a new `StarfieldView` custom view.

**Tech Stack:** C++17, VST3 SDK, VSTGUI, r8brain-free-src (resampler, git submodule), CMake + Visual Studio 2022, x64.

**Spec:** `docs/superpowers/specs/2026-05-30-arc-inflator-custom-design.md` (validated to convergence).

---

## Testing approach (TDD adapted)

This repo has **no unit-test harness** (unlike sibling projects). The verification loop per task is therefore: **make change → build x64 Release → run the gate** where the gate is one of: compiler success, the VST3 validator (`validator.exe` from the SDK), a measured behavioral check (param range, reported latency, aliasing FFT, cascade flatness), or a visual check in the validator/host. Each task ends with a commit. "Expected" outputs are stated for every gate. Where a value must be *measured* (FIR latencies), the exact measurement procedure is given — that is a concrete instruction, not a placeholder.

**Build/validate commands (reused throughout)** — run from the repo root `C:\Users\The Arc\Documents\Arc Inflator`:

```powershell
# Configure (once; re-run after CMakeLists changes)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DSMTG_WIN=1 `
  -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=0 -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=0 `
  -DSMTG_MDA_VST3_VST2_COMPATIBLE=0 -DSMTG_CREATE_BUNDLE_FOR_WINDOWS=0
# Build
cmake --build build --config Release --target JS_Inflator
# Validate (path may vary; validator is built by the SDK)
& "build/bin/Release/validator.exe" "build/VST3/Release/Arc Inflator.vst3"
```

The compiled plugin lands at `build/VST3/Release/Arc Inflator.vst3` (after Area E renames it; before that, `JS Inflator.vst3`). "GATE PASS" = build exits 0 **and** the named check holds.

---

## File Structure (what changes, and why)

| File | Responsibility | Phases touching it |
|------|----------------|--------------------|
| `source/JSIF_cids.h` | param-ID enum, plugin FUIDs | E (new FUIDs, `kParamOSOffline`) |
| `source/JSIF_shared.h` | `overSample` enum, init constants | C (`init_Output`), B (new factors, `init_OSOffline`) |
| `source/JSIF_processor.h` | DSP class decl, stage-filter members, tap/latency constants | B (cascade members, generic Fir decls, `isOffline`, `fParamOSOffline`) |
| `source/JSIF_processor.cpp` | audio processing, gain, OS dispatch, state, latency, cleanup | C (gain), B (cascade impl, dispatch, alloc, terminate, state, effectiveOS, strcmp send) |
| `source/JSIF_controller.cpp` | param registration, 3 state sites, GUI switch, latency msg | C (Output range), B (OSOffline param + state), E (default GUI, strcmp fix) |
| `source/JSIF_entry.cpp` | plugin name + factory vendor | E |
| `source/version.h` | Windows identity strings | E |
| `CMakeLists.txt` | build, version, snapshot, (mac bundle) | E (version, snapshot filename) |
| `resource/JSIF_editor.uidesc` | VSTGUI layout/colors/fonts, both GUIs | B (OSOffline menu), A (recolor, starfield view, default) |
| `resource/twarch/*.png` | Twarch bitmap assets | A (recolor) |
| `resource/<UID>_snapshot*.png` | plugin thumbnail (named by FUID) | E (rename) |
| `source/StarfieldView.h/.cpp` | **new** animated background CView | A |

**Implementation order:** Phase 0 (toolchain) → E (identity) → C (fader) → B1 (oversampling DSP) → B2 (online/offline) → B3 (OS-offline GUI) → A1 (recolor) → A2 (starfield) → D (final validate). Rationale: prove the build first; identity early so every build is "Arc Inflator"; smallest DSP change (C) before the largest (B); visual last (most iterative).

---

## Phase 0 — Toolchain & baseline build

### Task 0.1: Populate the r8brain submodule

**Files:** `libs/r8brain-free-src/` (currently empty)

- [ ] **Step 1: Initialize the submodule**

```powershell
git submodule init
git submodule update
```

- [ ] **Step 2: Verify it populated**

Run: `Get-ChildItem libs/r8brain-free-src | Measure-Object`
Expected: Count > 0 (contains `CDSPResampler.h`, `r8bbase.cpp`, etc.). If still empty, clone directly: `git clone https://github.com/avaneev/r8brain-free-src.git libs/r8brain-free-src`.

### Task 0.2: Ensure the VST3 SDK is present

**Files:** external — `C:/VST_SDK/vst3sdk` (per `CMakeLists.txt:7`)

- [ ] **Step 1: Check for the SDK**

Run: `Test-Path C:/VST_SDK/vst3sdk/CMakeLists.txt`
Expected: `True`. If `False`, obtain VST3 SDK 3.7.12 (includes VSTGUI):
```powershell
git clone --recursive --branch v3.7.12_build_20 https://github.com/steinbergmedia/vst3sdk.git C:/VST_SDK/vst3sdk
```
(If the SDK cannot be installed, **HALT** and report to the user — the build cannot proceed.)

### Task 0.3: Baseline build of the unmodified plugin

**Files:** none (verifies toolchain before any change)

- [ ] **Step 1: Configure**

Run the Configure command from "Build/validate commands" above.
Expected: configures with no error; `build/JS_Inflator.sln` exists.

- [ ] **Step 2: Build Release x64**

Run: `cmake --build build --config Release --target JS_Inflator`
Expected: build succeeds; `build/VST3/Release/JS Inflator.vst3` exists.

- [ ] **Step 3: Validate**

Run: `& "build/bin/Release/validator.exe" "build/VST3/Release/JS Inflator.vst3"`
Expected: ends with `Result: 0 tests failed` (or equivalent all-pass).
GATE: if the baseline does not build+validate, stop and fix the environment before any code change.

- [ ] **Step 4: Commit the build setup**

```powershell
git add libs/.gitkeep
git commit -m "build: initialize r8brain submodule and verify baseline build"
```
(If nothing to stage, skip — the submodule pointer is already committed.)

---

## Phase 1 — Identity (Area E): "Arc Inflator"

> GPLv3: keep every `// Copyright(c) 2024 yg331.` header and the `LICENSE`. We add our notice, never replace theirs.

### Task 1.1: Rename plugin + vendor strings

**Files:**
- Modify: `source/JSIF_entry.cpp:12,24,25,26`
- Modify: `source/version.h:12,14,16,18,19`

- [ ] **Step 1: Edit `JSIF_entry.cpp`**

Line 12: `#define stringPluginName "JS Inflator"` → `#define stringPluginName "Arc Inflator"`
Line 24: `BEGIN_FACTORY_DEF ("yg331",` → `BEGIN_FACTORY_DEF ("The Arc",`
Line 25: the URL → `"https://github.com/orthearc/"` (or your URL)
Line 26: `"mailto:info@mycompanyname.com")` → `"mailto:orthearc@gmail.com")`

- [ ] **Step 2: Edit `version.h`**

Line 12: `#define stringOriginalFilename "JS Inflator.vst3"` → `"Arc Inflator.vst3"`
Line 14: `#define stringFileDescription "JS Inflator VST3 (64Bit)"` → `"Arc Inflator VST3 (64Bit)"`
Line 16: `#define stringFileDescription "JS Inflator VST3"` → `"Arc Inflator VST3"` (32-bit branch)
Line 18: `#define stringCompanyName "yg331\0"` → `"The Arc\0"`
Line 19: `#define stringLegalCopyright "Copyright(c) 2024 yg331."` → `"Copyright(c) 2024 yg331. Modifications (c) 2026 The Arc."`

- [ ] **Step 3: Build**

Run the Build command. Expected: succeeds (still output as `JS Inflator.vst3` until CMake rename in Task 1.4).

- [ ] **Step 4: Commit**

```powershell
git add source/JSIF_entry.cpp source/version.h
git commit -m "feat(identity): rename plugin/vendor strings to Arc Inflator / The Arc"
```

### Task 1.2: Regenerate the plugin FUIDs

**Files:** Modify `source/JSIF_cids.h:12-13`

- [ ] **Step 1: Generate two fresh FUIDs**

Generate two distinct GUIDs (PowerShell: `[guid]::NewGuid().ToString("N").ToUpper()` twice). Each 32-hex-char GUID becomes four `0x`-prefixed 8-hex groups. Example transform of `A1B2C3D4E5F6...`: `0xA1B2C3D4, 0xE5F6...`.

- [ ] **Step 2: Replace the UIDs in `JSIF_cids.h`**

```cpp
// Lines 12-13 — replace with YOUR generated values:
static const Steinberg::FUID kJSIF_ProcessorUID  (0x________, 0x________, 0x________, 0x________);
static const Steinberg::FUID kJSIF_ControllerUID (0x________, 0x________, 0x________, 0x________);
```
**Record the Processor UID's 32-char hex** (concatenate the four groups, no `0x`/spaces) — needed for the snapshot rename. Example: groups `0xA1B2C3D4,0xE5F60718,...` → `A1B2C3D4E5F60718...`.

- [ ] **Step 3: Build**

Run the Build command. Expected: succeeds.

- [ ] **Step 4: Commit**

```powershell
git add source/JSIF_cids.h
git commit -m "feat(identity): regenerate FUIDs so Arc Inflator is a distinct plugin"
```

### Task 1.3: Rename the snapshot resource to the new Processor UID

**Files:** Rename `resource/A6B5CA9F4C4F5B9388F837774504BD37_snapshot.png` and `..._snapshot_2.0x.png`; modify `CMakeLists.txt:85-89`

- [ ] **Step 1: Rename both snapshot files**

```powershell
$hex = "<NEW_PROCESSOR_UID_HEX>"   # the 32-char hex recorded in Task 1.2
Rename-Item "resource/A6B5CA9F4C4F5B9388F837774504BD37_snapshot.png"     "resource/${hex}_snapshot.png"
Rename-Item "resource/A6B5CA9F4C4F5B9388F837774504BD37_snapshot_2.0x.png" "resource/${hex}_snapshot_2.0x.png"
```

- [ ] **Step 2: Update the snapshot references in `CMakeLists.txt`**

In the `smtg_target_add_plugin_snapshots` block (around :85-89), replace both `A6B5CA9F4C4F5B9388F837774504BD37_snapshot*.png` filenames with `${hex}_snapshot*.png` (literal new hex).

- [ ] **Step 3: Reconfigure + build**

Run Configure, then Build. Expected: succeeds; the snapshot is bundled.

- [ ] **Step 4: Commit**

```powershell
git add -A resource CMakeLists.txt
git commit -m "feat(identity): rename snapshot to new processor UID"
```

### Task 1.4: Output filename = "Arc Inflator.vst3" + version bump

**Files:** Modify `CMakeLists.txt:22` (version); confirm Windows output name comes from `version.h`

- [ ] **Step 1: Bump the project version**

`CMakeLists.txt:22`: `VERSION 2.0.3.2` → `VERSION 1.0.0.0` (Arc Inflator v1; drives `projectversion.h`/`FULL_VERSION_STR`).

- [ ] **Step 2: (mac parity, optional) bundle id/company**

`CMakeLists.txt:114-115` (inside `if(SMTG_MAC)`): `BUNDLE_IDENTIFIER io.github.yg331.JS.Inflator` → `com.thearc.arcinflator`; `COMPANY_NAME "yg331"` → `"The Arc"`. (No effect on the Windows build — Windows identity is in `version.h`.)

- [ ] **Step 3: Reconfigure + build**

Run Configure + Build. Expected: output is now `build/VST3/Release/Arc Inflator.vst3` (driven by `version.h` `stringOriginalFilename`).

- [ ] **Step 4: Validate identity**

Run: `& "build/bin/Release/validator.exe" "build/VST3/Release/Arc Inflator.vst3"`
Expected: all tests pass; validator prints name "Arc Inflator", vendor "The Arc". GATE: confirm both strings.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt
git commit -m "feat(identity): version 1.0.0.0, output as Arc Inflator.vst3"
```

### Task 1.5: Default the GUI to the recolored Twarch

**Files:** Modify `source/JSIF_controller.cpp:392,509`

- [ ] **Step 1: Default the GuiSwitch param to Twarch (index 1)**

`controller.cpp:392`: `GuiSwitch->setNormalized(GuiSwitch->toNormalized(0));` → `GuiSwitch->setNormalized(GuiSwitch->toNormalized(1));`

- [ ] **Step 2: Default the restored value to Twarch**

`controller.cpp:509`: `if (streamer.readDouble(savedGUI) == false) savedGUI = 0.0;` → `... savedGUI = 1.0;`

> Do NOT touch the GUI-switch comparison logic at `update()` :683-684, `editorAttached` :698-699, or `createView` (~:586): they branch on exact `stateGUI == 0.0 / == 1.0`, and Twarch normalizes to exactly `1.0`, so the new default works as-is.

- [ ] **Step 3: Build + check default GUI**

Build, then open in the validator's editor (or a host). Expected: the editor opens on the Twarch template (not Original).

- [ ] **Step 4: Commit**

```powershell
git add source/JSIF_controller.cpp
git commit -m "feat(identity): default plugin GUI to Twarch"
```

---

## Phase 2 — Output fader −12..+12 dB (Area C)

### Task 2.1: Widen the output gain range

**Files:** Modify `source/JSIF_controller.cpp:302`, `source/JSIF_processor.cpp:761`, `source/JSIF_shared.h:33`, `source/JSIF_controller.cpp:502`

- [ ] **Step 1: Controller — output param max = +12 dB**

`controller.cpp:302`: `maxPlain = 0.0;` → `maxPlain = 12.0;` (in the `kParamOutput` block; `minPlain` stays −12.0, `defaultPlain` stays 0.0).

- [ ] **Step 2: Processor — output gain maps to ±12 dB**

`processor.cpp:761`: `Vst::Sample64 Out_db = expf(logf(10.f) * (12.0 * fOutput - 12.0) / 20.f);`
→ `Vst::Sample64 Out_db = expf(logf(10.f) * (24.0 * fOutput - 12.0) / 20.f);`

- [ ] **Step 3: Default output = 0 dB (center)**

`shared.h:33`: `init_Output = 1.0,` → `init_Output = 0.5,`

- [ ] **Step 4: Controller restore fallback = center**

`controller.cpp:502`: `if (streamer.readDouble(savedOutput) == false) savedOutput = 1.0;` → `... savedOutput = 0.5;`

- [ ] **Step 5: Build**

Run Build. Expected: succeeds.

- [ ] **Step 6: GATE — verify the range and gain**

In the validator/host: the Output param now displays **−12.00 dB … +12.00 dB**, centered at 0.00 dB. Behavioral check: set Output to +6 dB, feed a known sine; output level rises ~+6 dB (×1.995). Set to +12 dB → ×3.98; −12 dB → ×0.251.

- [ ] **Step 7: Commit**

```powershell
git add source/JSIF_controller.cpp source/JSIF_processor.cpp source/JSIF_shared.h
git commit -m "feat(gain): output fader range -12..+12 dB, default 0 dB"
```

---

## Phase 3 — Oversampling x16 / x32 / x64 (Area B1)

> **Architecture decision (refines spec B1):** the existing min-phase functions unroll every output sample (`Fir_x8_up` = 8 explicit blocks) — impractical at x64. Implement a **generic loop-based cascade**: one shared array of 6 half-band up-stage filters and 6 down-stage filters (designed at `96000·2^k`, cutoff `Fs/4`), and generic `Fir_up_n` / `Fir_dn_n` that run `nStages = log2(factor)` stages. Leave the existing `Fir_x2/x4/x8_*` untouched (surgical) and route only the new 16/32/64 factors through the generic path. The linear-phase path adds higher-ratio r8brain resamplers.

### Task 3.1: Extend the `overSample` enum

**Files:** Modify `source/JSIF_shared.h:7-13`

- [ ] **Step 1: Add the three factors and fix the count**

```cpp
typedef enum {
    overSample_1x,
    overSample_2x,
    overSample_4x,
    overSample_8x,
    overSample_16x,
    overSample_32x,
    overSample_64x,
    overSample_num = 6
} overSample;
```
(`overSample_num` = highest index = 6, used as the `ToNormalized/FromNormalized` step count.)

- [ ] **Step 2: Build (expect it to still compile — dispatch defaults handle unknown factors as no-op until wired)**

Run Build. Expected: succeeds (new enum values unused so far).

- [ ] **Step 3: Commit**

```powershell
git add source/JSIF_shared.h
git commit -m "feat(os): add overSample_16x/32x/64x enum values"
```

### Task 3.2: Add the generic min-phase stage filters + tap constants

**Files:** Modify `source/JSIF_processor.h` (members near :338-364; method decls near :376-381)

- [ ] **Step 1: Declare the shared stage arrays and tap tables**

In the protected members (after the existing `dnSample_83` vectors, ~:350), add:

```cpp
// Generic half-band cascade for x16/x32/x64 (6 stages: indices 0..5)
static SMTG_CONSTEXPR int32 kMaxStages = 6;
std::vector<Flt> upStage[kMaxStages];   // [stage][channel]
std::vector<Flt> dnStage[kMaxStages];
static SMTG_CONSTEXPR int32 upStageTap[kMaxStages] = { 87, 33, 21, 17, 15, 13 };
static SMTG_CONSTEXPR int32 dnStageTap[kMaxStages] = { 113, 33, 21, 17, 15, 13 };
// Min-phase FIR latencies for the new factors (MEASURED in Task 3.7).
int32 latency_Fir_x16 = 0, latency_Fir_x32 = 0, latency_Fir_x64 = 0;
// Linear-phase (r8brain) resamplers for the new factors.
std::vector<r8b::CDSPResampler24*> upSample_16x_Lin, upSample_32x_Lin, upSample_64x_Lin;
std::vector<r8b::CDSPResampler24*> dnSample_16x_Lin, dnSample_32x_Lin, dnSample_64x_Lin;
int32 latency_r8b_x16 = 0, latency_r8b_x32 = 0, latency_r8b_x64 = 0;
```

- [ ] **Step 2: Declare the generic cascade methods**

After the existing `Fir_x8_dn` declaration (~:381), add:

```cpp
void Fir_up_n(Sample64* in, Sample64* out, int32 channel, int32 nStages);
void Fir_dn_n(Sample64* in, Sample64* out, int32 channel, int32 nStages);
```

- [ ] **Step 3: Build**

Run Build. Expected: succeeds (members/decls unused so far — `-D_CRT_SECURE_NO_WARNINGS` is already set; unused-private-member is not an error in MSVC).

- [ ] **Step 4: Commit**

```powershell
git add source/JSIF_processor.h
git commit -m "feat(os): declare generic 6-stage cascade members and r8brain x16/32/64"
```

### Task 3.3: Implement the generic cascade functions

**Files:** Modify `source/JSIF_processor.cpp` (after `Fir_x8_dn`, ~:1118)

- [ ] **Step 1: Implement `Fir_up_n` and `Fir_dn_n`**

```cpp
// Generic min-phase half-band interpolation: 1 in -> (1<<nStages) out.
void JSIF_Processor::Fir_up_n(Vst::Sample64* in, Vst::Sample64* out, int32 channel, int32 nStages)
{
    Vst::Sample64 a[64], b[64];
    a[0] = *in;
    int32 count = 1;
    Vst::Sample64* src = a; Vst::Sample64* dst = b;
    for (int32 k = 0; k < nStages; k++)
    {
        Flt& f = upStage[k][channel];
        const size_t shift = sizeof(double) * (f.TAP_SIZE - 1) / 2;
        for (int32 i = 0; i < count; i++)
        {
            memmove(f.buff + 1, f.buff, shift);
            f.buff[0] = src[i];
            HB_upsample(&f, &dst[2 * i]);
        }
        count *= 2;
        Vst::Sample64* t = src; src = dst; dst = t;
    }
    memcpy(out, src, sizeof(Vst::Sample64) * count);
}

// Generic min-phase half-band decimation: (1<<nStages) in -> 1 out.
void JSIF_Processor::Fir_dn_n(Vst::Sample64* in, Vst::Sample64* out, int32 channel, int32 nStages)
{
    Vst::Sample64 a[64], b[64];
    int32 count = 1 << nStages;
    memcpy(a, in, sizeof(Vst::Sample64) * count);
    Vst::Sample64* src = a; Vst::Sample64* dst = b;
    for (int32 k = nStages - 1; k >= 0; k--)   // highest-rate stage first
    {
        Flt& f = dnStage[k][channel];
        const size_t shift = sizeof(double) * (f.TAP_SIZE - 2);
        const int32 outCount = count / 2;
        for (int32 i = 0; i < outCount; i++)
        {
            memmove(f.buff + 3, f.buff + 1, shift);
            f.buff[2] = src[2 * i];
            f.buff[1] = src[2 * i + 1];
            HB_dnsample(&f, &dst[i]);
        }
        count = outCount;
        Vst::Sample64* t = src; src = dst; dst = t;
    }
    *out = src[0];
}
```
(These reuse the existing `HB_upsample`/`HB_dnsample` and the same buffer-shift idioms as `Fir_x8_up`/`Fir_x8_dn`.)

- [ ] **Step 2: Build**

Run Build. Expected: succeeds.

- [ ] **Step 3: Commit**

```powershell
git add source/JSIF_processor.cpp
git commit -m "feat(os): implement generic loop-based half-band cascade"
```

### Task 3.4: Allocate + design the cascade and r8brain resamplers

**Files:** Modify `source/JSIF_processor.cpp` `setupProcessing` (~:216-298) and `terminate` (~:90-110)

- [ ] **Step 1: In `setupProcessing`, after the existing `dnSample_8x_Lin` r8brain block (:297), allocate the new engines**

```cpp
    // ---- Generic min-phase cascade stages (x16/32/64) ----
    for (int s = 0; s < kMaxStages; s++) { upStage[s].resize(numChannels); dnStage[s].resize(numChannels); }
    // ---- Linear-phase r8brain for new factors ----
    upSample_16x_Lin.resize(numChannels); upSample_32x_Lin.resize(numChannels); upSample_64x_Lin.resize(numChannels);
    dnSample_16x_Lin.resize(numChannels); dnSample_32x_Lin.resize(numChannels); dnSample_64x_Lin.resize(numChannels);
```
Place this **before** the existing per-channel `for` loop at :223, alongside the other `.resize` calls.

- [ ] **Step 2: Inside the per-channel loop (after the r8brain `new` block at :297, before the loop's closing `}` at :298), design the stages and construct the resamplers**

```cpp
        for (int s = 0; s < kMaxStages; s++)
        {
            const double designFs = 96000.0 * (1 << s);      // 96k,192k,384k,768k,1.536M,3.072M
            const double designFb = designFs * 0.25;          // half-band cutoff = Fs/4
            memset(upStage[s][channel].coef, 0, sizeof(upStage[s][channel].coef));
            memset(dnStage[s][channel].coef, 0, sizeof(dnStage[s][channel].coef));
            Kaiser::calcFilter(designFs, 0.0, designFb, upStageTap[s], 100.0, upStage[s][channel].coef);
            Kaiser::calcFilter(designFs, 0.0, designFb, dnStageTap[s], 100.0, dnStage[s][channel].coef);
            for (int i = 0; i < upStageTap[s]; i++) upStage[s][channel].coef[i] *= 2.0;  // interpolation gain
            FLT_SET(upStage[s], upStageTap[s]);
            FLT_SET(dnStage[s], dnStageTap[s]);
        }
        upSample_16x_Lin[channel] = new r8b::CDSPResampler24(1.0, 16.0, 1 * 16, 2.3);
        upSample_32x_Lin[channel] = new r8b::CDSPResampler24(1.0, 32.0, 1 * 32, 2.4);
        upSample_64x_Lin[channel] = new r8b::CDSPResampler24(1.0, 64.0, 1 * 64, 2.5);
        dnSample_16x_Lin[channel] = new r8b::CDSPResampler24(16.0, 1.0, 1 * 16, 2.3);
        dnSample_32x_Lin[channel] = new r8b::CDSPResampler24(32.0, 1.0, 1 * 32, 2.4);
        dnSample_64x_Lin[channel] = new r8b::CDSPResampler24(64.0, 1.0, 1 * 64, 2.5);
```
> The `FLT_SET` macro (defined :262-277) references `filter[channel]` — invoking it as `FLT_SET(upStage[s], ...)` works because `upStage[s]` is the per-channel vector, indexed `[channel]` inside the macro.

- [ ] **Step 2b: Enlarge the FLT_SET indexing if needed** — confirm the macro body uses `filter[channel]`; `upStage[s]` substitutes as `filter`, yielding `upStage[s][channel]`. No macro change required.

- [ ] **Step 3: In `terminate()` (after the existing `dnSample_8x_Lin` cleanup ~:97 and Flt `clear_delete`s ~:110), free the new engines**

```cpp
    for (auto& v : { &upSample_16x_Lin,&upSample_32x_Lin,&upSample_64x_Lin,
                     &dnSample_16x_Lin,&dnSample_32x_Lin,&dnSample_64x_Lin })
        for (auto* p : *v) delete p;
    clear_delete(upSample_16x_Lin); clear_delete(upSample_32x_Lin); clear_delete(upSample_64x_Lin);
    clear_delete(dnSample_16x_Lin); clear_delete(dnSample_32x_Lin); clear_delete(dnSample_64x_Lin);
    for (int s = 0; s < kMaxStages; s++) { clear_delete(upStage[s]); clear_delete(dnStage[s]); }
```

- [ ] **Step 4: Build + validate (no behavior change yet — factors not dispatched)**

Build, run validator. Expected: passes; no leak/crash on instantiate+destroy (validator exercises this).

- [ ] **Step 5: Commit**

```powershell
git add source/JSIF_processor.cpp
git commit -m "feat(os): allocate/design cascade stages and r8brain x16/32/64; free in terminate"
```

### Task 3.5: Enlarge the per-sample oversample buffers

**Files:** Modify `source/JSIF_processor.cpp:792-793`

- [ ] **Step 1: Grow `up_x`/`up_y` to 64**

`processor.cpp:792-793`:
```cpp
        double up_x[64] = {0.0, };
        double up_y[64] = {0.0, };
```
(They hold `oversampling` samples per input; x64 needs 64. `memcpy(... *oversampling)` at :845 and `for k<oversampling` at :850 then stay in-bounds.)

- [ ] **Step 2: Build**

Run Build. Expected: succeeds.

- [ ] **Step 3: Commit**

```powershell
git add source/JSIF_processor.cpp
git commit -m "fix(os): enlarge up_x/up_y buffers to 64 for x16/32/64"
```

### Task 3.6: Wire the dispatch ladders for 16/32/64

**Files:** Modify `source/JSIF_processor.cpp` — `processAudio` latency selector (:769-779), `oversampling` var (:781-784), up dispatch (:833-846), down dispatch (:880-894); `latencyBypass` (:697-706); `getLatencySamples` (:328-338)

- [ ] **Step 1: `oversampling` factor var (:781-784) — add the new factors**

```cpp
        int32 oversampling = 1;
        if      (fParamOS == overSample_2x)  oversampling = 2;
        else if (fParamOS == overSample_4x)  oversampling = 4;
        else if (fParamOS == overSample_8x)  oversampling = 8;
        else if (fParamOS == overSample_16x) oversampling = 16;
        else if (fParamOS == overSample_32x) oversampling = 32;
        else if (fParamOS == overSample_64x) oversampling = 64;
```

- [ ] **Step 2: `processAudio` latency selector (:769-779) — add new factors in both phase branches**

```cpp
        if (!fParamPhase) {
            if      (fParamOS == overSample_2x)  latency = latency_Fir_x2;
            else if (fParamOS == overSample_4x)  latency = latency_Fir_x4;
            else if (fParamOS == overSample_8x)  latency = latency_Fir_x8;
            else if (fParamOS == overSample_16x) latency = latency_Fir_x16;
            else if (fParamOS == overSample_32x) latency = latency_Fir_x32;
            else if (fParamOS == overSample_64x) latency = latency_Fir_x64;
        }
        else {
            if      (fParamOS == overSample_2x)  latency = latency_r8b_x2;
            else if (fParamOS == overSample_4x)  latency = latency_r8b_x4;
            else if (fParamOS == overSample_8x)  latency = latency_r8b_x8;
            else if (fParamOS == overSample_16x) latency = latency_r8b_x16;
            else if (fParamOS == overSample_32x) latency = latency_r8b_x32;
            else if (fParamOS == overSample_64x) latency = latency_r8b_x64;
        }
```

- [ ] **Step 3: Up dispatch (:833-846) — replace the `else` fall-through with explicit branches**

In the `if (!fParamPhase)` min-phase block, change the final `else ... Fir_x8_up(...)` to:
```cpp
                if      (fParamOS == overSample_2x)  Fir_x2_up(&inputSample, up_x, channel);
                else if (fParamOS == overSample_4x)  Fir_x4_up(&inputSample, up_x, channel);
                else if (fParamOS == overSample_8x)  Fir_x8_up(&inputSample, up_x, channel);
                else if (fParamOS == overSample_16x) Fir_up_n(&inputSample, up_x, channel, 4);
                else if (fParamOS == overSample_32x) Fir_up_n(&inputSample, up_x, channel, 5);
                else                                 Fir_up_n(&inputSample, up_x, channel, 6);
```
In the `else` linear-phase block, change the final `else ... upSample_8x_Lin` to:
```cpp
                    double* upSample_buff;
                    if      (fParamOS == overSample_2x)  upSample_2x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    else if (fParamOS == overSample_4x)  upSample_4x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    else if (fParamOS == overSample_8x)  upSample_8x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    else if (fParamOS == overSample_16x) upSample_16x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    else if (fParamOS == overSample_32x) upSample_32x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    else                                 upSample_64x_Lin[channel]->process(&inputSample, 1, upSample_buff);
                    memcpy(up_x, upSample_buff, sizeof(Vst::Sample64) * oversampling);
```

- [ ] **Step 4: Down dispatch (:880-894) — mirror the up changes**

Min-phase final `else ... Fir_x8_dn` →
```cpp
                if      (fParamOS == overSample_2x)  Fir_x2_dn(up_y, &inputSample, channel);
                else if (fParamOS == overSample_4x)  Fir_x4_dn(up_y, &inputSample, channel);
                else if (fParamOS == overSample_8x)  Fir_x8_dn(up_y, &inputSample, channel);
                else if (fParamOS == overSample_16x) Fir_dn_n(up_y, &inputSample, channel, 4);
                else if (fParamOS == overSample_32x) Fir_dn_n(up_y, &inputSample, channel, 5);
                else                                 Fir_dn_n(up_y, &inputSample, channel, 6);
```
Linear-phase final `else ... dnSample_8x_Lin` →
```cpp
                    double* dnSample_buff;
                    if      (fParamOS == overSample_2x)  dnSample_2x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    else if (fParamOS == overSample_4x)  dnSample_4x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    else if (fParamOS == overSample_8x)  dnSample_8x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    else if (fParamOS == overSample_16x) dnSample_16x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    else if (fParamOS == overSample_32x) dnSample_32x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    else                                 dnSample_64x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
                    inputSample = *dnSample_buff;
```

- [ ] **Step 5: `latencyBypass` ladder (:697-706) — add new factors (both phase branches)**

Mirror Step 2's pattern in the `latencyBypass` function (it has the same `if(!fParamPhase){…}else{…}` latency selection; add the `overSample_16x/32x/64x` lines pointing to `latency_Fir_x16/32/64` and `latency_r8b_x16/32/64`).

- [ ] **Step 6: `getLatencySamples` (:328-338) — replace `else`→x8 with explicit branches**

```cpp
        if (fParamPhase) {
            if      (fParamOS == overSample_1x)  return 0;
            else if (fParamOS == overSample_2x)  return latency_r8b_x2;
            else if (fParamOS == overSample_4x)  return latency_r8b_x4;
            else if (fParamOS == overSample_8x)  return latency_r8b_x8;
            else if (fParamOS == overSample_16x) return latency_r8b_x16;
            else if (fParamOS == overSample_32x) return latency_r8b_x32;
            else                                 return latency_r8b_x64;
        }
        else {
            if      (fParamOS == overSample_1x)  return 0;
            else if (fParamOS == overSample_2x)  return latency_Fir_x2;
            else if (fParamOS == overSample_4x)  return latency_Fir_x4;
            else if (fParamOS == overSample_8x)  return latency_Fir_x8;
            else if (fParamOS == overSample_16x) return latency_Fir_x16;
            else if (fParamOS == overSample_32x) return latency_Fir_x32;
            else                                 return latency_Fir_x64;
        }
```

- [ ] **Step 7: Build + smoke test**

Build. In the validator/host, select OS = x16, x32, x64 (min-phase and linear-phase) and play audio. Expected: audible output, no crash, no NaNs. (Latency constants are still 0 — fixed in 3.7.)

- [ ] **Step 8: Commit**

```powershell
git add source/JSIF_processor.cpp
git commit -m "feat(os): dispatch x16/32/64 in processAudio, latencyBypass, getLatencySamples"
```

### Task 3.7: Measure and set the new latency constants

**Files:** Modify the latency members in `source/JSIF_processor.h` (set in `setupProcessing`), or compute them in `setupProcessing`

- [ ] **Step 1: Compute the r8brain latencies in `setupProcessing`**

After constructing the new resamplers (Task 3.4 Step 2), add (mirrors the commented formula at processor.h:366-368):
```cpp
        if (channel == 0) {
            latency_r8b_x16 = -1 + 2 * (int32)upSample_16x_Lin[0]->getInLenBeforeOutPos(1);
            latency_r8b_x32 = -1 + 2 * (int32)upSample_32x_Lin[0]->getInLenBeforeOutPos(1);
            latency_r8b_x64 = -1 + 2 * (int32)upSample_64x_Lin[0]->getInLenBeforeOutPos(1);
        }
```

- [ ] **Step 2: Compute the min-phase FIR latencies**

The cascade group delay (in base-rate samples) = the existing `latency_Fir_x8` pattern extended. Set in `setupProcessing` (channel 0):
```cpp
        if (channel == 0) {
            // Sum of per-stage half-band group delays mapped to base rate.
            // Verify empirically (Step 3); start from the x8 baseline + added stages.
            latency_Fir_x16 = latency_Fir_x8;   // refine after measurement
            latency_Fir_x32 = latency_Fir_x8;
            latency_Fir_x64 = latency_Fir_x8;
        }
```

- [ ] **Step 3: MEASURE the true min-phase latency (procedure)**

Build. In a host, on a single channel at 48 kHz: bypass the inflator effect (set Effect=0 so dry passes through with latency comp), feed a unit impulse, OS=x16 min-phase, and capture the output. The plugin's internal `latency_q` compensates by `latency`; if `latency_Fir_x16` is wrong, the wet/dry null at Effect=0.5 will show a comb/preecho. Adjust `latency_Fir_x16/32/64` until the dry/wet phase aligns (null depth maximized) at each factor. Record the converged integers and replace the placeholders in Step 2 with literals.

- [ ] **Step 4: Build + verify latency reporting**

Build. In the host, switch OS factors and confirm the reported plugin latency (PDC) changes to the measured values per factor/phase.

- [ ] **Step 5: Commit**

```powershell
git add source/JSIF_processor.h source/JSIF_processor.cpp
git commit -m "feat(os): compute/measure latency for x16/32/64 (min + linear phase)"
```

### Task 3.8: Aliasing + cascade-flatness verification

**Files:** none (verification only)

- [ ] **Step 1: Aliasing FFT**

Feed a 0 dBFS sine at ~0.3·Nyquist into the inflator (Effect=100, Curve nonzero) and FFT the output. Compare alias-image levels at OS = x8 vs x16 vs x32 vs x64 (min-phase). Expected: higher OS → lower alias images (monotonic). Confirms the cascade decimation works.

- [ ] **Step 2: Cascade passband flatness**

Sweep / impulse the **full up+down chain** (Effect path, inflator linearized or bypassed) at x16/x32/x64 and plot magnitude. Expected: flat to ≥0.95·Nyquist, <0.1 dB droop at 20 kHz. If droop fails, increase the affected `upStageTap`/`dnStageTap` entries (Task 3.2 Step 1) and re-verify. Record the final tap values.

- [ ] **Step 3: Commit any tap-count refinements**

```powershell
git add source/JSIF_processor.h
git commit -m "test(os): finalize stage tap counts after aliasing/flatness checks"
```
(If no change was needed, skip.)

---

## Phase 4 — Online vs offline OS factor (Area B2)

### Task 4.1: Add the offline-OS parameter

**Files:** Modify `source/JSIF_cids.h` (append enum), `source/JSIF_shared.h` (init), `source/JSIF_controller.cpp` (~:341 registration)

- [ ] **Step 1: Append the param ID (do NOT insert)**

`JSIF_cids.h` — add after `kParamBypass` (so it becomes tag 16; inserting mid-enum would renumber existing tags and break state/automation/control-tags):
```cpp
    kParamBypass,
    kParamOSOffline,   // tag 16
    kGuiSwitch = 1000
```

- [ ] **Step 2: Add the init default**

`JSIF_shared.h` — after `init_OS`:
```cpp
static const overSample
init_OS = overSample_1x,
init_OSOffline = overSample_16x;
```

- [ ] **Step 3: Register the param (controller, after the OS list ~:341)**

```cpp
    Vst::StringListParameter* OSOff = new Vst::StringListParameter(STR("OS Offline"), kParamOSOffline);
    OSOff->appendString(STR("x1"));  OSOff->appendString(STR("x2"));  OSOff->appendString(STR("x4"));
    OSOff->appendString(STR("x8"));  OSOff->appendString(STR("x16")); OSOff->appendString(STR("x32"));
    OSOff->appendString(STR("x64"));
    OSOff->setNormalized(OSOff->toNormalized(static_cast<int>(init_OSOffline)));
    parameters.addParameter(OSOff);
```
Also append `"x16"/"x32"/"x64"` to the **existing** OS list (`controller.cpp:336-339`):
```cpp
    OS->appendString(STR("x16")); OS->appendString(STR("x32")); OS->appendString(STR("x64"));
```

- [ ] **Step 4: Build**

Run Build. Expected: succeeds; validator shows OS and "OS Offline" each with 7 values.

- [ ] **Step 5: Commit**

```powershell
git add source/JSIF_cids.h source/JSIF_shared.h source/JSIF_controller.cpp
git commit -m "feat(os): add kParamOSOffline param (x1..x64) + extend OS list"
```

### Task 4.2: Processor — offline detection + effectiveOS

**Files:** Modify `source/JSIF_processor.h` (members), `source/JSIF_processor.cpp` (ctor :22-37, setupProcessing :192, process switch ~:385, and every `fParamOS` consumer)

- [ ] **Step 1: Add members (`processor.h`)**

```cpp
    overSample fParamOSOffline;
    bool isOffline = false;
    inline overSample effectiveOS() const { return isOffline ? fParamOSOffline : fParamOS; }
```

- [ ] **Step 2: Init in the constructor (`processor.cpp:22-37`)**

Add to the initializer list: `, fParamOSOffline(init_OSOffline)`

- [ ] **Step 3: Detect offline in `setupProcessing` (:192, near the top)**

```cpp
        isOffline = (newSetup.processMode == Steinberg::Vst::kOffline);
```

- [ ] **Step 4: Handle the param change in `process()` (~:385, beside `case kParamOS`)**

```cpp
                    case kParamOSOffline:
                        fParamOSOffline = static_cast<overSample>(
                            Steinberg::FromNormalized<ParamValue>(value, overSample_num));
                        break;
```

- [ ] **Step 5: Use `effectiveOS()` everywhere the audio path reads the factor**

In `processAudio` and `latencyBypass`, replace the `fParamOS` reads in the latency selector, `oversampling` var, and up/down dispatch (Task 3.6 Steps 1-5) with `effectiveOS()`. In `getLatencySamples`, replace `fParamOS` with `effectiveOS()`. (Mechanical substitution; the `process()` switch and state code keep `fParamOS`/`fParamOSOffline` as the raw param stores.)

- [ ] **Step 6: Build + verify offline switch**

Build. In a host, set OS=x1 (online) and OS Offline=x32, then bounce/render offline. Expected: during the offline render the reported latency / behavior matches x32 (the host calls `setupProcessing(kOffline)` → `isOffline=true` → `effectiveOS()` returns x32). Realtime playback uses x1.

- [ ] **Step 7: Commit**

```powershell
git add source/JSIF_processor.h source/JSIF_processor.cpp
git commit -m "feat(os): offline detection via processMode + effectiveOS() routing"
```

### Task 4.3: Persist the offline-OS param across all state sites

**Files:** Modify `source/JSIF_processor.cpp` (`getState` :599-609, `setState` :543-553), `source/JSIF_controller.cpp` (`setComponentState` :432-454, `setState` :499-522, `getState` :562-572)

- [ ] **Step 1: Processor `getState` — append the field at the END (after Bypass)**

`processor.cpp` after `streamer.writeInt32(bBypass ? 1 : 0);` (:609):
```cpp
        streamer.writeDouble(Steinberg::ToNormalized<ParamValue>(static_cast<ParamValue>(fParamOSOffline), overSample_num));
```

- [ ] **Step 2: Processor `setState` — tolerant read (do NOT return false on absence)**

After the last `if (streamer.readInt32(savedBypass) == false) return kResultFalse;` (:553):
```cpp
        Vst::ParamValue savedOSOff = Steinberg::ToNormalized<ParamValue>(static_cast<ParamValue>(init_OSOffline), overSample_num);
        streamer.readDouble(savedOSOff);   // tolerant: old states lack this field; keep default on short read
        fParamOSOffline = static_cast<overSample>(Steinberg::FromNormalized<ParamValue>(savedOSOff, overSample_num));
```

- [ ] **Step 3: Controller `setComponentState` — tolerant read of the SAME processor stream**

After `if (streamer.readInt32(savedBypass) == false) return kResultFalse;` (:442) add a tolerant read, and a `setParamNormalized` near :454:
```cpp
        Vst::ParamValue savedOSOff = OSOffline_default_normalized();   // = ToNormalized(init_OSOffline, overSample_num)
        streamer.readDouble(savedOSOff);
        setParamNormalized(kParamOSOffline, savedOSOff);
```
(Define `OSOffline_default_normalized()` inline or compute the constant directly: `Steinberg::ToNormalized<...>((ParamValue)init_OSOffline, overSample_num)`.)

- [ ] **Step 4: Controller UI `getState`/`setState` — mirror the field (this stream already defaults tolerantly)**

`getState` (after `streamer.writeDouble(stateGUI)` region ~:572): `streamer.writeDouble(stateOSOffline);`
`setState` (in the tolerant block ~:509): `if (streamer.readDouble(savedOSOffline) == false) savedOSOffline = <default normalized>;` then `setParamNormalized(kParamOSOffline, savedOSOffline);` and `stateOSOffline = savedOSOffline;`.
Add a `Vst::ParamValue stateOSOffline = <default>;` member in `JSIF_controller.h` beside `stateOS`.

- [ ] **Step 5: GATE — backward compatibility**

Build. Load a preset/session saved by the **baseline** plugin (Phase 0 build) — confirm it loads without error (the tolerant reads default the missing field). Then save+reload a new session and confirm OS Offline round-trips.

- [ ] **Step 6: Commit**

```powershell
git add source/JSIF_processor.cpp source/JSIF_controller.cpp source/JSIF_controller.h
git commit -m "feat(os): persist OS Offline across all state sites (backward-compatible)"
```

### Task 4.4: Fix the inverted latency-notification trigger

**Files:** Modify `source/JSIF_controller.cpp:633`, and the processor's OS param-change send

- [ ] **Step 1: Correct the strcmp condition**

`controller.cpp:633`: `if (strcmp("OS\n", text)) {` → `if (strcmp("OS\n", text) == 0) {`
(`strcmp` returns 0 on match; the original fired `restartComponent` for every message *except* the OS one.)

- [ ] **Step 2: Ensure the processor sends "OS\n" on OS/OSOffline change**

Confirm the existing `sendTextMessage("OS")` (or equivalent) in `process()` fires on `kParamOS`; add the same send to the new `case kParamOSOffline` so an offline-factor change also notifies. (If the existing message text is `"OS"` not `"OS\n"`, match whatever string the controller compares — keep both sides identical.)

- [ ] **Step 3: Build + verify online OS latency re-query**

Build. In a host, change the online OS factor during playback. Expected: the host re-reads plugin latency (PDC updates) without a reload.

- [ ] **Step 4: Commit**

```powershell
git add source/JSIF_controller.cpp source/JSIF_processor.cpp
git commit -m "fix(os): correct inverted strcmp so OS-change latency notifies host"
```

---

## Phase 5 — OS (Offline) GUI control (Area B3)

### Task 5.1: Add the control-tag + OS-menu max-value

**Files:** Modify `resource/JSIF_editor.uidesc` control-tags (:135-153) and the OS `COptionMenu` (:851)

- [ ] **Step 1: Add the OSOffline control-tag**

In the `"control-tags"` block (:135-153), add: `"OSOffline": "16",`

- [ ] **Step 2: Bump the OS menu max-value**

The Twarch OS `COptionMenu` (`control-tag: OS`, ~:851): `"max-value": "3",` → `"max-value": "6",` (now 7 items x1..x64).

- [ ] **Step 3: Build + check the OS dropdown shows x1..x64**

Build, open editor. Expected: the OS dropdown lists x1,x2,x4,x8,x16,x32,x64.

- [ ] **Step 4: Commit**

```powershell
git add resource/JSIF_editor.uidesc
git commit -m "feat(gui): OSOffline control-tag + OS menu max-value 6"
```

### Task 5.2: Add the OS (Offline) dropdown to the Twarch template

**Files:** Modify `resource/JSIF_editor.uidesc` (Twarch template, near the OS/Phase menus ~:839-917)

- [ ] **Step 1: Duplicate the OS `COptionMenu` as the offline one**

Copy the OS `COptionMenu` block (:839-877), change `"control-tag": "OS"` → `"control-tag": "OSOffline"`, set `"max-value": "6"`, and offset its `"origin"` (e.g. place it below the OS menu — adjust the y of `"origin": "487, 180"` to e.g. `"487, 212"`). The Zoom menus (`max-value:6`) confirm a 7-item `COptionMenu` renders correctly.

- [ ] **Step 2: (optional) add a small label**

Add a `CTextLabel` "Offline" beside it, matching the existing label style/colors.

- [ ] **Step 3: Build + check both dropdowns**

Build, open editor. Expected: OS and OS (Offline) both visible, each selecting x1..x64; changing OS Offline then bouncing uses that factor (re-verify Task 4.2 Step 6).

- [ ] **Step 4: Commit**

```powershell
git add resource/JSIF_editor.uidesc
git commit -m "feat(gui): add OS (Offline) dropdown to Twarch template"
```

---

## Phase 6 — Recolor the Twarch GUI to Arcorrect (Area A1)

> Palette: bg `#080810`, surface `#10101C`/`#1E1E34`, accent `#5090E0`, glow `#4070B8`, border `#2A2A50`, text `#A0A8C0`, bright `#E0E8FF`. uidesc colors use `#rrggbbaa`.

### Task 6.1: Recolor the uidesc color tokens, gradients, and fonts

**Files:** Modify `resource/JSIF_editor.uidesc` colors (:84-92), gradients (:93-133), fonts (:66-80)

- [ ] **Step 1: Redefine the custom color tokens**

In `"colors"` (:84-92), map to the palette (alpha `ff`):
```json
"OS_back": "#1e1e34ff",
"mid_back": "#10101cff",
"mid_font_color": "#a0a8c0ff",
"mid_gray": "#2a2a50ff",
"coverup": "#080810ff",
"db_dark": "#4070b8ff",
"New": "#5090e0ff"
```

- [ ] **Step 2: Recolor the gradients**

In `"gradients"` (:93-133), retune `"Blue"`/`"Yellow"`/`"Default TextButton Gradient*"` stops toward the accent/navy palette (e.g. `Blue` stop1 `#5090e0ff`, stop2 `#080810ff`).

- [ ] **Step 3: Point fonts at Segoe UI**

In `"fonts"` (:66-80), change each `"font-name": "Arial"` → `"font-name": "Segoe UI"` (keep the sizes).

> `~ BlackCColor` / `~ RedCColor` used by Twarch controls are SDK built-ins, not tokens — if any such control needs recoloring, change that control's color attribute to one of the named tokens above (do not try to redefine the `~` built-ins).

- [ ] **Step 4: Build + visual check**

Build, open editor. Expected: Twarch panel text/menus shift to navy/blue/lavender, Segoe UI typography. Original GUI unaffected.

- [ ] **Step 5: Commit**

```powershell
git add resource/JSIF_editor.uidesc
git commit -m "feat(skin): recolor Twarch uidesc tokens/gradients to Arcorrect, Segoe UI"
```

### Task 6.2: Tint the Twarch bitmap assets

**Files:** Modify (in place) `resource/twarch/*.png`

- [ ] **Step 1: Recolor the bitmaps to the palette**

Re-tint, preserving dimensions/frame geometry: `twarch_background.png` → navy `#080810` base; `twarch_knob_red_.png` & `twarch_knob_yellow_.png` (63-frame filmstrips — keep frame height 148) → blue accent `#5090E0`; `twarch_handle.png`, `twarch_btn_IN/SPLIT/CLIP.png`, `twarch_meter_*.png` → palette. Use an image tool / script; keep each file's pixel dimensions identical. Leave the Original-GUI assets (`orig_*`, root knobs) untouched.

- [ ] **Step 2: Build + visual check**

Build, open editor. Expected: knobs/buttons/background/faders show the Arcorrect palette; controls still aligned and functional; filmstrip knobs animate correctly (frame count unchanged).

- [ ] **Step 3: Commit**

```powershell
git add resource/twarch
git commit -m "feat(skin): tint Twarch bitmaps to Arcorrect palette"
```

---

## Phase 7 — Animated starfield background (Area A2)

### Task 7.1: Create the StarfieldView custom view

**Files:** Create `source/StarfieldView.h`, `source/StarfieldView.cpp`; modify `CMakeLists.txt` (add sources)

- [ ] **Step 1: Write `StarfieldView.h`**

```cpp
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include <vector>

namespace yg331 {
class StarfieldView : public VSTGUI::CView {
public:
    explicit StarfieldView(const VSTGUI::CRect& size);
    ~StarfieldView() override;
    void draw(VSTGUI::CDrawContext* ctx) override;
    void setViewSize(const VSTGUI::CRect& r, bool invalid = true) override;
private:
    struct Star { double x, y, b, speed; double size; };
    void rebuild();
    std::vector<Star> stars;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
    unsigned int rng = 2463534242u;      // xorshift seed (no <random> / time)
    double frac(unsigned int v) { return (v & 0xFFFFFF) / double(0x1000000); }
    unsigned int nextRand();
};
} // namespace yg331
```

- [ ] **Step 2: Write `StarfieldView.cpp`**

```cpp
#include "StarfieldView.h"
#include "vstgui/lib/cdrawcontext.h"
using namespace VSTGUI;
namespace yg331 {

StarfieldView::StarfieldView(const CRect& size) : CView(size) {
    rebuild();
    timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) {
        for (auto& s : stars) { s.y += s.speed; if (s.y > 1.0) { s.y -= 1.0; s.x = frac(nextRand()); } }
        invalid();
    }, 33);  // ~30 fps
    timer->start();
}
StarfieldView::~StarfieldView() { if (timer) timer->stop(); }

unsigned int StarfieldView::nextRand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void StarfieldView::rebuild() {
    stars.clear();
    for (int i = 0; i < 180; i++) {            // 180 stars, matching Arcorrect
        Star s;
        s.x = frac(nextRand()); s.y = frac(nextRand());
        s.b = 0.5 + 0.5 * frac(nextRand());
        s.size = 0.5 + 2.0 * frac(nextRand());
        s.speed = 0.0005 + 0.0015 * frac(nextRand());
        stars.push_back(s);
    }
}

void StarfieldView::setViewSize(const CRect& r, bool invalid) { CView::setViewSize(r, invalid); }

void StarfieldView::draw(CDrawContext* ctx) {
    const CRect b = getViewSize();
    ctx->setFillColor(CColor(8, 8, 16, 255));            // #080810 navy base
    ctx->drawRect(b, kDrawFilled);
    for (const auto& s : stars) {
        const CCoord px = b.left + s.x * b.getWidth();
        const CCoord py = b.top  + s.y * b.getHeight();
        const uint8_t a = (uint8_t)(180 + 60 * s.b);     // whitish-blue
        ctx->setFillColor(CColor(200, 215, 255, a));
        CRect dot(px, py, px + s.size, py + s.size);
        ctx->drawEllipse(dot, kDrawFilled);
    }
    setDirty(false);
}
} // namespace yg331
```

- [ ] **Step 3: Add the sources to CMake**

In `CMakeLists.txt` `smtg_add_vst3plugin(JS_Inflator ...)` (:31-40), add `source/StarfieldView.h` and `source/StarfieldView.cpp`.

- [ ] **Step 4: Build**

Reconfigure + Build. Expected: compiles (view not yet placed — no visual change).

- [ ] **Step 5: Commit**

```powershell
git add source/StarfieldView.h source/StarfieldView.cpp CMakeLists.txt
git commit -m "feat(skin): add StarfieldView custom CView (180 stars, 30fps drift)"
```

### Task 7.2: Register the view via createCustomView and place it in Twarch

**Files:** Modify `source/JSIF_controller.h` (declare override), `source/JSIF_controller.cpp` (implement), `resource/JSIF_editor.uidesc` (Twarch bottom child)

- [ ] **Step 1: Declare the override (`controller.h`, in the `VST3EditorDelegate` section)**

```cpp
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;
```

- [ ] **Step 2: Implement it (`controller.cpp`)**

```cpp
#include "StarfieldView.h"
VSTGUI::CView* PLUGIN_API JSIF_Controller::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*)
{
    if (name && std::strcmp(name, "StarfieldView") == 0)
        return new yg331::StarfieldView(VSTGUI::CRect(0, 0, 1300, 1300));
    return nullptr;
}
```

- [ ] **Step 3: Place the view as the bottom child of the Twarch container**

In `JSIF_editor.uidesc`, in the `Twarch` template's `children` (right after the container opens, **before** the background `CView` at :747 so it renders beneath), add:
```json
"CView": {
  "attributes": {
    "class": "CView",
    "custom-view-name": "StarfieldView",
    "origin": "0, 0",
    "size": "1300, 1300",
    "mouse-enabled": "false",
    "transparent": "false"
  }
},
```

- [ ] **Step 4: Make the recolored background show the stars through it**

For the stars to be visible, the recolored `twarch_background.png` (Task 6.2) must have **transparency** in the areas where stars should show (the empty navy regions). Re-export `twarch_background.png` with alpha=0 in those regions (panel/control graphics stay opaque). **Fallback** if alpha authoring is impractical: keep the background opaque and instead place the StarfieldView only in the empty gap rectangles (give it a smaller `origin`/`size` covering a visible gap), so stars still animate in view.

- [ ] **Step 5: Build + visual check**

Reconfigure + Build, open editor. Expected: the Twarch GUI shows drifting whitish-blue stars on a navy field behind the panel; controls still aligned/functional; Original GUI unaffected; GUI-switch toggles correctly.

- [ ] **Step 6: Commit**

```powershell
git add source/JSIF_controller.h source/JSIF_controller.cpp resource/JSIF_editor.uidesc resource/twarch/twarch_background.png
git commit -m "feat(skin): register + place animated starfield behind Twarch panel"
```

---

## Phase 8 — Final build & validation (Area D)

### Task 8.1: Full clean build + validator

**Files:** none

- [ ] **Step 1: Clean reconfigure + build**

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
# then run Configure + Build commands
```
Expected: configures and builds x64 Release with no errors; `build/VST3/Release/Arc Inflator.vst3`.

- [ ] **Step 2: Validator**

Run: `& "build/bin/Release/validator.exe" "build/VST3/Release/Arc Inflator.vst3"`
Expected: all tests pass.

- [ ] **Step 3: Coexistence + identity check**

Confirm the built `Arc Inflator.vst3` installs/loads in a host **alongside** an existing JS Inflator (distinct FUIDs), shows name "Arc Inflator" / vendor "The Arc", opens on the recolored Twarch GUI with the starfield, snapshot thumbnail renders.

- [ ] **Step 4: Feature smoke pass**

In a host: Output fader spans −12..+12 dB; OS and OS (Offline) each offer x1..x64; min-phase and linear-phase both work at every factor; offline bounce uses the offline factor; `LICENSE` + yg331 headers still present in the tree.

- [ ] **Step 5: Final commit + tag**

```powershell
git add -A
git commit -m "build: final validated Arc Inflator x64 Release"
git tag arc-inflator-v1.0.0
```

---

## Self-Review (completed by plan author)

- **Spec coverage:** A (recolor 6.1/6.2, starfield 7.1/7.2), B1 (oversampling 3.1-3.8), B2 (online/offline 4.1-4.4), C (fader 2.1), D (build 0.x, 8.1), E (identity 1.1-1.5) — every spec area maps to tasks. ✓
- **Placeholder scan:** the only "to determine" values are the FIR latencies (Task 3.7, with an explicit measurement procedure) and final tap counts (Task 3.8, explicit verification) — concrete procedures, not placeholders. ✓
- **Type/name consistency:** `effectiveOS()`, `fParamOSOffline`, `upStage[]/dnStage[]`, `upStageTap/dnStageTap`, `Fir_up_n/Fir_dn_n`, `latency_Fir_x16/32/64`, `latency_r8b_x16/32/64`, `kParamOSOffline`, `init_OSOffline`, `StarfieldView` — used consistently across tasks. ✓
- **Backward compat:** state appends are end-of-stream + tolerant reads at all three processor-stream sites (4.3). ✓
