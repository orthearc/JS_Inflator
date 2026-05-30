# Arc Inflator — Custom Build Design

**Date:** 2026-05-30
**Base:** [JS_Inflator](https://github.com/Kiriki-liszt/JS_Inflator) by Kiriki-liszt (yg331), GPLv3, VST3 SDK 3.7.9 + VSTGUI 4.12.
**Distribution:** Personal use (me + friends). GPLv3 → modified source shared alongside any binary.

## Goal

Customize the JS_Inflator plugin with three user-requested features plus a Windows build:

1. **Skin** the *twarch* GUI variant to the *Arcorrect* visual look (recolor) and add an animated starfield background.
2. **Extend oversampling** beyond x8 to add x16 / x32 / x64, with **independent online (real-time) and offline (render) factors**.
3. **Output fader range** changed from −12..0 dB to **−12..+12 dB**, matching the input fader.

## Non-Goals (out of scope)

- No change to the "Original" GUI variant (only the twarch template is reskinned).
- No new vector arc-knobs; twarch's existing filmstrip knobs are reused, only recolored.
- No per-mode (online/offline) **phase** parameter — phase stays a single global toggle. (Offline + linear + x64 is achievable by manual selection.)
- No audio-reactive starfield (constant gentle drift only).
- No DSP redesign of the inflator curve, band-split, metering, or the Original GUI.

## Context — codebase map

VST3 plugin, two-part (Processor + Controller), single editor description.

| File | Role | Relevant anchors |
|------|------|------------------|
| `source/JSIF_shared.h` | shared constants, `overSample` enum, init values | enum :7-13; `init_Output` :33; `init_OS` :51-52 |
| `source/JSIF_cids.h` | param ID enum | `kParamInput`..`kParamBypass` :16-34, `kGuiSwitch=1000` |
| `source/JSIF_processor.h` | DSP class, oversampler members, Kaiser FIR, tap/latency constants | members :331-381; `setupProcessing` :237; `getLatencySamples` :240 |
| `source/JSIF_processor.cpp` | audio processing, gain, OS dispatch, latency | `setupProcessing` :192; `getLatencySamples` :325-339; gain :760-761; `latencyBypass` :688-735; `processAudio` OS dispatch :769-925 |
| `source/JSIF_controller.cpp` | parameter registration, state, GUI switch | Input/Output Range :289-307; OS list :335-341; Phase :349-354; GuiSwitch :389-394; state :420-565 |
| `resource/JSIF_editor.uidesc` | VSTGUI layout, colors, bitmaps, both GUIs | twarch template :736; OS menu :839-877 (`max-value:3`); Phase menu :879-917; faders :1122/:1155; knobs :973-1021; background CView :747-758 |
| `resource/twarch/` | twarch bitmap assets (PNG filmstrips) | background, knobs (63-frame), handle, buttons, meters |
| `libs/r8brain-free-src` | r8brain resampler (linear-phase path) | `CDSPResampler24` |

---

## Area C — Output fader −12..+12 dB

Input already maps `In_db = 24·fInput − 12` (range −12..+12, center 0 dB at 0.5). Output maps `Out_db = 12·fOutput − 12` (range −12..0). Make output identical to input.

**Edits:**
1. `JSIF_controller.cpp:302` — `maxPlain = 0.0` → `12.0` (Output `RangeParameter`).
2. `JSIF_processor.cpp:761` — `12.0 * fOutput - 12.0` → `24.0 * fOutput - 12.0`.
3. `JSIF_shared.h:33` — `init_Output = 1.0` → `0.5` (default becomes 0 dB / center).
4. `JSIF_controller.cpp:502` setState fallback `savedOutput = 1.0` → `0.5`; check processor-side getState/setState defaults stay consistent.

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

**Allocation:** all factor engines pre-allocated in `setActive` (extends current pre-allocation of the 2x/4x/8x vectors) so switching is glitch-free.

**Dispatch:** extend the `if/else` ladders in `processAudio` (:833-890), `latencyBypass` (:697-705), and `getLatencySamples` (:328-338) to cover the three new factors for both phase modes.

CPU note: x64 runs the inflator 64× per sample — heavy; intended for offline.

### B2. Online vs offline factor (independent)

New parameter `kParamOSOffline` (add to `cids.h` enum), a second 7-entry `StringListParameter` "OS (Offline)" (x1..x64), registered beside OS in `controller.cpp` (~:341). Default **x16**; online OS default stays **x1**.

- `setupProcessing` (`processor.cpp:192`): store `isOffline = (newSetup.processMode == Steinberg::Vst::kOffline)`.
- Define `effectiveOS()` = `isOffline ? fParamOSOffline : fParamOS`. Replace `fParamOS` reads in `processAudio`, `latencyBypass`, `getLatencySamples` with `effectiveOS()`.
- New member `fParamOSOffline` (mirror `fParamOS` handling in process param-change :385, state load/save :554-565/:599-608, constructor init).
- Latency: host calls `setupProcessing(kOffline)` then `getLatencySamples` before an offline pass → correct factor reported per mode.

> Host-support caveat: offline switching relies on the host setting `processMode = kOffline`. Standard VST3 behavior; not every host honors it. Documented, not worked around.

### B3. GUI

- uidesc OS `COptionMenu` (:839-877): `max-value` 3 → 6.
- Add a second `COptionMenu` (control-tag `OSOffline`, `max-value:6`) in the twarch template near the OS/Phase menus (:839-917 region), plus a label.
- Add `OSOffline` to the `<control-tags>` section and the controller's tag wiring.

**Verify:** OS + OS(Offline) dropdowns show x1..x64; offline render uses the offline factor (observe reported-latency change between realtime and bounce, and/or processMode log); high-OS reduces aliasing (FFT of a high sine pushed into the nonlinearity shows lower alias images at higher factors); per-mode/per-phase latency reported correctly.

---

## Area A — Skin: recolor twarch + starfield

### Palette (from Arcorrect)
bg `#FF080810`, surface `#FF10101C` / `#FF1E1E34`, accent `#FF5090E0`, glow `#FF4070B8`, border `#FF2A2A50`, text `#FFA0A8C0`, bright text `#FFE0E8FF`. Font: Segoe UI.

### A1. Recolor (layout unchanged)
- **uidesc colors:** redefine the named tokens used by the twarch template (`OS_back`, `mid_back`, `mid_gray`, `mid_font_color`, and the `RedCColor`/`BlackCColor` usages within twarch) to the palette above. Set menu/text fonts to Segoe UI.
- **Bitmaps (tint in `resource/twarch/`):** `twarch_background.png` → navy; `twarch_knob_red_.png` & `twarch_knob_yellow_.png` (63-frame filmstrips) → blue accent (preserve frame geometry); `twarch_handle.png`, `twarch_btn_IN/SPLIT/CLIP.png`, `twarch_meter_*.png` → palette. Keep the Original GUI assets untouched.

### A2. Starfield
- New `StarfieldView : VSTGUI::CView` — owns a `CVSTGUITimer` (~30 fps), ~150 stars (whitish-blue, sizes 0.5–2.5 px), slow constant drift; `draw()` paints navy base + stars, `invalid()` per tick. No audio reactivity.
- Register the view (controller `createView`/sub-controller or a `ViewCreator`) and place it as the **bottom child** of the twarch `CViewContainer` (:736), sized to the window.
- Recolored `twarch_background.png` is re-authored with **transparency in the empty navy regions** so stars show through; opaque panel/control graphics remain.

**Verify:** twarch GUI renders the navy/blue Arcorrect palette with drifting stars behind the panel; all controls remain aligned and functional; Original GUI unaffected; switching GUI Switch works.

---

## Area D — Build & verify (Windows)

Per `How_to_build.md`: CMake configure (fetches VST3 SDK + VSTGUI), VS2022 generator, build x64 Release. Run the VST3 validator; confirm the plugin loads. If VS2022 / CMake are absent → halt and report. DAW listening test done by user.

**Success gate:** CMake configure + build x64 Release succeed with no errors; VST3 validator passes; plugin instantiates.

---

## Sequencing & verification order

1. **C (faders)** — smallest, isolated; build + check param range.
2. **B (oversampling)** — DSP + params; build + aliasing/latency checks per mode.
3. **A (skin)** — most iterative/visual; build + visual confirmation in real plugin.
4. **D** — full build + validator at each stage and final.

Each area is independently buildable/verifiable. Changes are surgical: every edit traces to one of the three requests. `version.h` bumped once at the end.

## Risks

- **Min-phase 16/32/64 cascade tap/cutoff tuning** is the only DSP-judgment part; mitigated by following the existing Kaiser pattern and verifying aliasing via FFT.
- **r8brain high-ratio latency** grows; reported correctly, but linear-phase x64 has large latency by nature.
- **Output remap** shifts old session recall (accepted).
- **Offline switching** depends on host honoring `kOffline`.
- **Starfield compositing** requires a transparent-region background asset; if alpha authoring is impractical, fallback = draw stars only in fixed gap rectangles over an opaque background.
