//------------------------------------------------------------------------
// Copyright(c) 2024 yg331.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace yg331 {
//------------------------------------------------------------------------
static const Steinberg::FUID kJSIF_ProcessorUID  (0xC682CEDB, 0xB8614B93, 0xB4FEB3BE, 0x695156CB);
static const Steinberg::FUID kJSIF_ControllerUID (0xAA25A6C1, 0x1E064F7B, 0xB7E2F0BF, 0x0F4CDD55);

#define JSIF_VST3Category "Fx"
enum {
    kParamInput = 0,    // Input
    kParamEffect,        // Effect
    kParamCurve,        // Curve
    kParamClip,            // Clip
    kParamOutput,        // Output
    kInVuPPML,        // Input VuPPM
    kInVuPPMR,
    kOutVuPPML,    // Output VuPPM
    kOutVuPPMR,
    kMeter,
    kParamOS,
    kParamSplit,
    kParamZoom,
    kParamPhase,
    kParamIn,        // ByPass
    kParamBypass,
    kGuiSwitch = 1000
};
//------------------------------------------------------------------------
} // namespace yg331
