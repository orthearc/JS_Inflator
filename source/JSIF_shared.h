#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace yg331 {
//------------------------------------------------------------------------
typedef enum {
	overSample_1x,
	overSample_2x,
	overSample_4x,
	overSample_8x,
	overSample_16x,
	overSample_32x,
	overSample_64x,
	overSample_128x,
	overSample_256x,
	overSample_num = 8
} overSample;

typedef struct _SVF {
	Steinberg::Vst::Sample64 C = 0.0;
	Steinberg::Vst::Sample64 R = 0.0;
	Steinberg::Vst::Sample64 I = 0.0;
} SVF;

typedef struct _BS {
	SVF LP;
	SVF HP;
	Steinberg::Vst::Sample64 G = 0.0;
	Steinberg::Vst::Sample64 GR = 0.0;
	Steinberg::Vst::SampleRate SR = 0.0;
} Band_Split;

// 2nd-order antiderivative-antialiasing state for one waveshaper instance.
typedef struct _ADAA2 {
	Steinberg::Vst::Sample64 x1    = 0.0;   // previous input
	Steinberg::Vst::Sample64 x2    = 0.0;   // input two samples ago
	Steinberg::Vst::Sample64 ad2x1 = 0.0;   // cached F2(x1)
	Steinberg::Vst::Sample64 d2    = 0.0;   // previous inner divided difference
} ADAA2;

static const Steinberg::Vst::ParamValue
init_Input = 0.5,
init_Effect = 0.0,
init_Curve = 0.5,
init_Output = 0.5,
init_VU = 0.0,
init_Zoom = 0.0 / 6.0;

static const Steinberg::Vst::Sample64
init_curvepct = init_Curve - 0.5,
init_curveA = 1.5 + init_curvepct,
init_curveB = -(init_curvepct + init_curvepct),
init_curveC = init_curvepct - 0.5,
init_curveD = 0.0625 - init_curvepct * 0.25 + (init_curvepct * init_curvepct) * 0.25;

static const bool
init_Clip = false,
init_Bypass = false,
init_In = true,
init_Split = false,
init_Link = false,   // unity-gain fader link off by default
init_Phase = false; // Min phase

static const int
init_ADAA_order = 0;   // antiderivative antialiasing: 0=Off, 1=1st-order, 2=2nd-order

static const overSample
init_OS = overSample_1x,
init_OSOffline = overSample_16x;

enum VuMeterTag {
    VuMeter_inL,
    VuMeter_inR,
    VuMeter_outL,
    VuMeter_outR,
    VuMeter_effect
};
//------------------------------------------------------------------------
} // namespace yg331
