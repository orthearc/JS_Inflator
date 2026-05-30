# Arc Inflator — Custom Build Design

**Date:** 2026-05-30
**Base:** [JS_Inflator](https://github.com/Kiriki-liszt/JS_Inflator) by Kiriki-liszt (yg331), GPLv3, VST3 SDK 3.7.9 + VSTGUI 4.12.
**Distribution:** Personal use (me + friends). GPLv3 → modified source shared alongside any binary.

## Goal

Ship **our own VST3, "Arc Inflator"** — a distinct, rebranded derivative of JS_Inflator (regenerated UIDs, installs alongside the original) with four work-areas plus a Windows build:

1. **Skin** the *Twarch* GUI variant to the *Arcorrect* visual look (recolor) and add an animated starfield background; default the plugin to this GUI.
2. **Extend oversampling** beyond x8 to add x16 / x32 / x64, with **independent online (real-time) and offline (render) factors**.
3. **Output fader range** changed from −12..0 dB to **−12..+12 dB**, matching the input fader.
4. **Identity** — rebrand name/vendor to "Arc Inflator" / "The Arc" and regenerate the plugin UIDs (Area E), retaining yg331's GPLv3 attribution.

## Non-Goals (out of scope)

- No change to the "Original" GUI variant (only the twarch template is reskinned).
- No new vector arc-knobs; twarch's existing filmstrip knobs are reused, only recolored.
- No per-mode (online/offline) **phase** parameter — phase stays a single global toggle. (Offline + linear + x64 is achievable by manual selection.)
- No audio-reactive starfield (constant gentle drift only).
- No DSP redesign of the inflator curve, band-split, metering, or the Original GUI.
- No removal of yg331's copyright headers or the `LICENSE` — GPLv3 requires retaining attribution; we add ours alongside.

## Context — codebase map

VST3 plugin, two-part (Processor + Controller), single editor description.

| File | Role | Relevant anchors |
|------|------|------------------|
| `source/JSIF_shared.h` | shared constants, `overSample` enum, init values | enum :7-13; `init_Output` :33; `init_OS` :51-52 |
| `source/JSIF_cids.h` | param ID enum | `kParamInput`..`kParamBypass` :16-34, `kGuiSwitch=1000` |
| `source/JSIF_processor.h` | DSP class, oversampler members, Kaiser FIR, tap/latency constants | members :331-381; `setupProcessing` :237; `getLatencySamples` :240 |
| `source/JSIF_processor.cpp` | audio processing, gain, OS dispatch, latency | `setupProcessing` :192; `getLatencySamples` :325-339; gain :760-761; `latencyBypass` :688-735; `processAudio` OS dispatch :769-925 |
| `source/JSIF_controller.cpp` | parameter registration, state, GUI switch | Input/Output Range :289-307; OS list :335-341; Phase :349-354; GuiSwitch :389-394; state :420-565 |
| `resource/JSIF_editor.uidesc` | VSTGUI layout, colors, bitmaps, both GUIs | templates `Original` :201 / `Twarch` :734 (isolated); fonts `Arial_18/24/26/28` :66-80; OS menu :839-877 (`max-value:3`); Phase menu :879-917; faders (CSlider) Input :1111 / Output :1144; knobs :973-1021; background CView :747-758 |
| `resource/twarch/` | twarch bitmap assets (PNG filmstrips) | background, knobs (63-frame), handle, buttons, meters |
| `libs/r8brain-free-src` | r8brain resampler (linear-phase path) | `CDSPResampler24` |

---

## Area C — Output fader −12..+12 dB

Input already maps `In_db = 24·fInput − 12` (range −12..+12, center 0 dB at 0.5). Output maps `Out_db = 12·fOutput − 12` (range −12..0). Make output identical to input.

**Edits:**
1. `JSIF_controller.cpp:302` — `maxPlain = 0.0` → `12.0` (Output `RangeParameter`).
2. `JSIF_processor.cpp:761` — `12.0 * fOutput - 12.0` → `24.0 * fOutput - 12.0`.
3. `JSIF_shared.h:33` — `init_Output = 1.0` → `0.5` (default becomes 0 dB / center).
4. `JSIF_controller.cpp:502` setState fallback `savedOutput = 1.0` → `0.5`. (Processor `setState` has **no** output fallback — it `return kResultFalse` on a missing field, `:546` — so nothing changes there; `fOutput = savedOutput` at `:558` and the `getState` write at `:602` are unaffected. `defaultPlain` stays `0.0` at controller `:303`.)

**Migration risk:** existing saved sessions store `fOutput` normalized; a stored `1.0` was 0 dB and would become +12 dB after the remap. Accepted for a personal tool; bump `version.h`.

**Verify:** Output shows −12..+12 dB in host, 0 dB at center detent; +6 dB output ≈ 2× linear gain (measure null/meter).

---

## Area B — Oversampling x16 / x32 / x64 + online/offline factors

### B1. Extend the factor set (both engines)

`overSample` enum (`shared.h:7-13`) gains `overSample_16x`, `overSample_32x`, `overSample_64x`; `overSample_num` 3 → 6.

**Min-phase path** — custom Kaiser FIR 2× cascade. Existing: 2x=1 stage (`upTap_21=85`), 4x=2 (83,31), 8x=3 (87,33,21). Add:
- 16x = 4 stages, 32x = 5 stages, 64x = 6 stages.
- New `Flt` member vectors (`upSample_161..164`, `_321..325`, `_641..646`, and `dn` equivalents) + tap constants. Later stages use progressively shorter taps (each higher octave has a wider transition band relative to its Nyquist); coefficients designed by the **existing** `Kaiser::calcFilter` exactly as current stages — no new DSP method.
- New `Fir_x16_up/dn`, `Fir_x32_up/dn`, `Fir_x64_up/dn` (header :376-381 pattern), each cascading 2× stages.
- New min-phase latency constants `latency_Fir_x16/x32/x64`.

**Linear-phase path** — r8brain `CDSPResampler24`. Add `upSample_16x_Lin`/`32x`/`64x` + `dn` equivalents, instantiated at the higher ratio (same API already used for 2x/4x/8x; r8brain natively supports the ratio). New latency constants `latency_r8b_x16/x32/x64` (derived from `getInLenBeforeOutPos`, as the existing ones).

**Allocation/design — all in `setupProcessing` (processor.cpp:192-298), NOT `setActive`:** extend the per-channel `resize` (:202-221), `memset` (:225-237), `Kaiser::calcFilter` (:240-252), upsample `×2.0` gain comp (:254-259), `FLT_SET` macro (:278-290), and r8brain `new CDSPResampler24(1.0, N, 1*N, transBand)` (:292-297) to cover the new 16/32/64 stages/ratios (r8brain `transBand` continues the ~2.0→2.2 trend: ~2.3/2.4/2.5). All factors pre-allocated so the online↔offline switch is glitch-free. (`setActive` :343 only calls the base — untouched.)

**Dispatch — extend every `fParamOS`-keyed `if/else` ladder.** `processAudio` has *three*: the latency selector (:769-779), the `oversampling` factor var (:781-784), and the up/down dispatch (:833-846 / :880-894); plus `latencyBypass` (:697-705) and `getLatencySamples` (:328-338). All currently fall through `else` → 8×, so explicit 16/32/64 branches are required in each, for both phase modes.

**Buffer sizing (must-fix):** `double up_x[8]` / `up_y[8]` (:792-793) hold `oversampling` samples per input sample — at x64 they overflow the stack. Enlarge to `[64]` (the `memcpy(…, sizeof(Sample64)*oversampling)` at :845 and the `for k < oversampling` loop at :850 depend on this).

CPU note: x64 runs the inflator 64× per sample — heavy; intended for offline.

### B2. Online vs offline factor (independent)

New parameter `kParamOSOffline` — **append** to the end of the `cids.h` enum (after `kParamBypass`, i.e. new tag `16`), **not** inserted mid-enum: inserting would renumber `kParamSplit/Zoom/Phase/In/Bypass` and break existing state, automation, and the uidesc `<control-tags>` map (:135-150, where `OS=10`, `Phase=13`, etc.). Register a second 7-entry `StringListParameter` "OS (Offline)" (x1..x64) beside OS in `controller.cpp` (~:341), and add `"OSOffline": "16"` to the uidesc control-tags. Default **x16**; online OS default stays **x1**.

- `setupProcessing` (`processor.cpp:192`): store `isOffline = (newSetup.processMode == Steinberg::Vst::kOffline)`.
- Add `effectiveOS()` = `isOffline ? fParamOSOffline : fParamOS`; replace `fParamOS` reads in `processAudio` (all three ladders above), `latencyBypass`, `getLatencySamples`.
- New member `fParamOSOffline`: constructor init (:22-37); `process()` param-change switch — add a `case kParamOSOffline` beside the `kParamOS` case (~:385).
- **State stream (correctness-critical):** append `fParamOSOffline` to processor `getState` (:599-609) / `setState` (:543-553) **and** controller `getState`/`setState` (:562…/:499-522). The processor `setState` currently does `return kResultFalse` on **any** missing field (:543-553), so the appended read MUST default-on-absence instead — `if (streamer.readDouble(savedOSOffline) == false) savedOSOffline = <x16 normalized>;` (mirror the controller's tolerant pattern at :499-509) — otherwise **every pre-existing saved state fails to load**. Controller also needs a `stateOSOffline` member + `setParamNormalized(kParamOSOffline, …)`.
- Latency: host calls `setupProcessing(kOffline)` then re-queries `getLatencySamples` before an offline pass → correct factor reported per mode.

> Host-support caveat: offline switching relies on the host setting `processMode = kOffline`. Standard VST3 behavior; not every host honors it. Documented, not worked around.

### B3. GUI

- uidesc OS `COptionMenu` (:839-877): `max-value` 3 → 6.
- Add a second `COptionMenu` (control-tag `OSOffline`, `max-value:6`) in the twarch template near the OS/Phase menus (:839-917 region), plus a label.
- Add `OSOffline` to the `<control-tags>` section and the controller's tag wiring.

**Verify:** OS + OS(Offline) dropdowns show x1..x64; offline render uses the offline factor (observe reported-latency change between realtime and bounce, and/or processMode log); high-OS reduces aliasing (FFT of a high sine pushed into the nonlinearity shows lower alias images at higher factors); per-mode/per-phase latency reported correctly.

---

## Area A — Skin: recolor twarch + starfield

### Palette (from Arcorrect)
bg `#FF080810`, surface `#FF10101C` / `#FF1E1E34`, accent `#FF5090E0`, glow `#FF4070B8`, border `#FF2A2A50`, text `#FFA0A8C0`, bright text `#FFE0E8FF`. Font: **Segoe UI** (Arcorrect's `setDefaultSansSerifTypefaceName`, LookAndFeel.cpp:24). Note the `Twarch` template currently uses **Arial** (uidesc fonts `Arial_18/24/26/28`, :66-80) — the recolor redefines those to Segoe UI (a Windows system font).

### A1. Recolor (layout unchanged)
- **uidesc colors:** redefine the named tokens used by the `Twarch` template (:734) (`OS_back`, `mid_back`, `mid_gray`, `mid_font_color`, and the `RedCColor`/`BlackCColor` usages within it) to the palette above. Point the `Arial_*` font defs (:66-80) at Segoe UI.
- **Bitmaps (tint in `resource/twarch/`):** `twarch_background.png` → navy; `twarch_knob_red_.png` & `twarch_knob_yellow_.png` (63-frame filmstrips) → blue accent (preserve frame geometry); `twarch_handle.png`, `twarch_btn_IN/SPLIT/CLIP.png`, `twarch_meter_*.png` → palette. Keep the Original GUI assets untouched.

### A2. Starfield
- New `StarfieldView : VSTGUI::CView` — owns a `CVSTGUITimer` (~30 fps), ~180 stars (matching Arcorrect `Starfield.h:32`; whitish-blue, sizes 0.5–2.5 px), slow constant drift; `draw()` paints navy base + stars, `invalid()` per tick. No audio reactivity.
- Register the view via the editor delegate's `createCustomView` / an `IController` (the uidesc already uses sub-controllers `myVuMeterController` :440/:1300 and `GUIController` :680 as precedent) and place it as the **bottom child** of the `Twarch` `CViewContainer` (:734-745), sized to the window.
- Recolored `twarch_background.png` is re-authored with **transparency in the empty navy regions** so stars show through; opaque panel/control graphics remain.

**Verify:** twarch GUI renders the navy/blue Arcorrect palette with drifting stars behind the panel; all controls remain aligned and functional; Original GUI unaffected; switching GUI Switch works.

---

## Area D — Build & verify (Windows)

Per `How_to_build.md`: CMake configure (fetches VST3 SDK + VSTGUI), VS2022 generator, build x64 Release. Run the VST3 validator; confirm the plugin loads. If VS2022 / CMake are absent → halt and report. DAW listening test done by user.

**Success gate:** CMake configure + build x64 Release succeed with no errors; VST3 validator passes; plugin instantiates.

---

## Area E — Identity: "Arc Inflator" as our own VST3

A distinct plugin that installs alongside JS Inflator (regenerated UIDs), defaulting to the recolored Twarch GUI. **GPLv3 constraint:** retain yg331's `// Copyright(c) 2024 yg331.` source headers + the `LICENSE`; add our copyright and a modification notice — never replace theirs.

**Name / vendor strings:**
- `entry.cpp:12` — `stringPluginName "JS Inflator"` → `"Arc Inflator"`.
- `entry.cpp:24` — factory vendor `"yg331"` → `"The Arc"`; update the contact URL/email (`:26`, currently `mailto:info@mycompanyname.com`).
- `version.h` — `stringOriginalFilename "JS Inflator.vst3"` → `"Arc Inflator.vst3"`; `stringFileDescription` → `"Arc Inflator VST3 (64Bit)"`; `stringCompanyName "yg331\0"` → `"The Arc\0"`; `stringLegalCopyright` → keep the yg331 line + append `Modifications (c) 2026 The Arc.`
- `CMakeLists.txt:115` — `COMPANY_NAME "yg331"` → `"The Arc"`; set the built bundle/output name to `Arc Inflator` (the internal CMake target id `JS_Inflator` may stay, to keep the diff surgical — the host shows the output/bundle name). Update any `BUNDLE_IDENTIFIER` to a `com.thearc.*` form.

**Regenerate UIDs (separate plugin):**
- `cids.h:12-13` — replace `kJSIF_ProcessorUID` and `kJSIF_ControllerUID` with two freshly-generated FUIDs → installs alongside JS Inflator, no host conflict; existing JS Inflator presets/sessions won't load (accepted — new plugin).
- **Rename the snapshot resource** `resource/A6B5CA9F…_snapshot.png` and `…_snapshot_2.0x.png` to `<NEW_PROCESSOR_UID_HEX>_snapshot.png` / `…_2.0x.png` (the VST3 snapshot filename must equal the processor UID hex). Update the `smtg_target_add_plugin_snapshots` reference (CMakeLists:85) if it names the file.
- AU target is mac-only — out of scope for the Windows build.

**Default GUI = Twarch:**
- `controller.cpp:392` — `GuiSwitch->toNormalized(0)` → `toNormalized(1)`; setState fallback `savedGUI = 0.0` (`:509`) → the Twarch-normalized value (`1.0` for the 2-entry list). Editor opens on the recolored Twarch; Original still selectable.

**Verify:** host shows "Arc Inflator" by "The Arc"; installs without replacing/conflicting with an existing JS Inflator; opens on the recolored Twarch GUI; snapshot thumbnail displays; `LICENSE` + yg331 headers retained.

---

## Sequencing & verification order

1. **E (identity)** — first, so every subsequent build produces "Arc Inflator" with fresh UIDs and confirms it installs alongside JS Inflator.
2. **C (faders)** — smallest DSP/param change, isolated; build + check param range.
3. **B (oversampling)** — DSP + params; build + aliasing/latency checks per mode.
4. **A (skin)** — most iterative/visual; build + visual confirmation in real plugin.
5. **D** — full build + validator at each stage and final.

Each area is independently buildable/verifiable. Changes are surgical: every edit traces to one of the four work-areas. `version.h` bumped once at the end.

## Risks

- **Min-phase 16/32/64 cascade tap/cutoff tuning** is the only DSP-judgment part; mitigated by following the existing Kaiser pattern and verifying aliasing via FFT.
- **r8brain high-ratio latency** grows; reported correctly, but linear-phase x64 has large latency by nature.
- **Output remap** shifts old session recall (accepted).
- **`overSample_num` 3→6 remaps saved OS values:** states saved pre-change normalize OS with base 3; re-read with base 6 shifts the recalled factor (old x8 → x64), via `ToNormalized/FromNormalized` at processor :603/:559. Accepted for a personal tool, like the output remap; bump `version.h`.
- **Online OS latency-change relies on host re-query:** a mid-session *online* OS change updates the internal latency queue + `getLatencySamples`, but (pre-existing behavior) does not call `restartComponent(kLatencyChanged)`, so some hosts won't re-read until reload. Offline switching is fine (host re-queries after `setupProcessing(kOffline)`). Out of scope to change; noted.
- **Offline switching** depends on host honoring `kOffline`.
- **Starfield compositing** requires a transparent-region background asset; if alpha authoring is impractical, fallback = draw stars only in fixed gap rectangles over an opaque background.
