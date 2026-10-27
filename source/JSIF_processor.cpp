//------------------------------------------------------------------------
// Copyright(c) 2024 yg331.
//------------------------------------------------------------------------

#include "JSIF_processor.h"
#include "JSIF_cids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/futils.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vsthelpers.h"

using namespace Steinberg;

namespace yg331 {
	//------------------------------------------------------------------------
	// JSIF_Processor
	//------------------------------------------------------------------------
	JSIF_Processor::JSIF_Processor() :
		fInput(init_Input),
		fOutput(init_Output),
		fEffect(init_Effect),
		fCurve(init_Curve),
		fParamZoom(init_Zoom),
		fParamPhase(init_Phase),
		bBypass(init_Bypass),
		bIn(init_In),
		bClip(init_Clip),
		bSplit(init_Split),
		adaaOrder(init_ADAA_order),
		curvepct(init_curvepct),
		curveA(init_curveA),
		curveB(init_curveB),
		curveC(init_curveC),
		curveD(init_curveD),
		fParamOS(init_OS),
		fParamOSOffline(init_OSOffline)
	{
		//--- set the wanted controller for our processor
		setControllerClass(kJSIF_ControllerUID);
	}

	//------------------------------------------------------------------------
	JSIF_Processor::~JSIF_Processor()
	{}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::initialize(FUnknown* context)
	{
		// Here the Plug-in will be instantiated

		//---always initialize the parent-------
		tresult result = AudioEffect::initialize(context);
		// if everything Ok, continue
		if (result != kResultOk)
		{
			return result;
		}

		//--- create Audio IO ------
		addAudioInput (STR16("Audio Input"),  Vst::SpeakerArr::kStereo);
		addAudioOutput(STR16("Audio Output"), Vst::SpeakerArr::kStereo);

		/* If you don't need an event bus, you can remove the next line */
		// addEventInput(STR16("Event In"), 1);

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::terminate()
	{
		// Here the Plug-in will be de-instantiated, last possibility to remove some memory!

#define clear_delete(vec) {vec.clear(); vec.shrink_to_fit();}

		clear_delete(fInputVu);
		clear_delete(fOutputVu);

		for (auto& loop : buff) 
			clear_delete(loop);
		clear_delete(buff);
		clear_delete(buff_head);

		for (auto& loop : upSample_2x_Lin) delete loop;
		for (auto& loop : upSample_4x_Lin) delete loop;
		for (auto& loop : upSample_8x_Lin) delete loop;
		for (auto& loop : dnSample_2x_Lin) delete loop;
		for (auto& loop : dnSample_4x_Lin) delete loop;
		for (auto& loop : dnSample_8x_Lin) delete loop;

		clear_delete(upSample_2x_Lin);
		clear_delete(upSample_4x_Lin);
		clear_delete(upSample_8x_Lin);
		clear_delete(dnSample_2x_Lin);
		clear_delete(dnSample_4x_Lin);
		clear_delete(dnSample_8x_Lin);

		clear_delete(upSample_21);
		clear_delete(upSample_41);
		clear_delete(upSample_42);
		clear_delete(upSample_81);
		clear_delete(upSample_82);
		clear_delete(upSample_83);
		clear_delete(dnSample_21);
		clear_delete(dnSample_41);
		clear_delete(dnSample_42);
		clear_delete(dnSample_81);
		clear_delete(dnSample_82);
		clear_delete(dnSample_83);

		for (auto& v : { &upSample_16x_Lin,&upSample_32x_Lin,&upSample_64x_Lin,&upSample_128x_Lin,&upSample_256x_Lin,
		                 &dnSample_16x_Lin,&dnSample_32x_Lin,&dnSample_64x_Lin,&dnSample_128x_Lin,&dnSample_256x_Lin })
			for (auto* p : *v) delete p;
		clear_delete(upSample_16x_Lin); clear_delete(upSample_32x_Lin); clear_delete(upSample_64x_Lin); clear_delete(upSample_128x_Lin); clear_delete(upSample_256x_Lin);
		clear_delete(dnSample_16x_Lin); clear_delete(dnSample_32x_Lin); clear_delete(dnSample_64x_Lin); clear_delete(dnSample_128x_Lin); clear_delete(dnSample_256x_Lin);
		for (int s = 0; s < kMaxStages; s++) { clear_delete(upStage[s]); clear_delete(dnStage[s]); }

		//---do not forget to call parent ------
		return AudioEffect::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::canProcessSampleSize(int32 symbolicSampleSize)
	{
		// by default kSample32 is supported
		if (symbolicSampleSize == Vst::kSample32)
			return kResultTrue;

		// disable the following comment if your processing support kSample64
		if (symbolicSampleSize == Vst::kSample64)
			return kResultTrue;

		return kResultFalse;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::setBusArrangements(
		Vst::SpeakerArrangement* inputs,  int32 numIns,
		Vst::SpeakerArrangement* outputs, int32 numOuts)
	{
		if (numIns == 1 && numOuts == 1)
		{
			// the host wants Mono => Mono (or 1 channel -> 1 channel)
			if (Vst::SpeakerArr::getChannelCount(inputs[0]) == 1 &&
				Vst::SpeakerArr::getChannelCount(outputs[0]) == 1)
			{
				auto* bus = FCast<Vst::AudioBus>(audioInputs.at(0));
				if (bus)
				{
					// check if we are Mono => Mono, if not we need to recreate the busses
					if (bus->getArrangement() != inputs[0])
					{
						getAudioInput(0)->setArrangement(inputs[0]);
						getAudioInput(0)->setName(STR16("Mono In"));
						getAudioOutput(0)->setArrangement(outputs[0]);
						getAudioOutput(0)->setName(STR16("Mono Out"));
					}
					return kResultOk;
				}
			}
			// the host wants something else than Mono => Mono,
			// in this case we are always Stereo => Stereo
			else
			{
				auto* bus = FCast<Vst::AudioBus>(audioInputs.at(0));
				if (bus)
				{
					tresult result = kResultFalse;

					// the host wants 2->2 (could be LsRs -> LsRs)
					if (Vst::SpeakerArr::getChannelCount(inputs[0]) == 2 &&
						Vst::SpeakerArr::getChannelCount(outputs[0]) == 2)
					{
						getAudioInput(0)->setArrangement(inputs[0]);
						getAudioInput(0)->setName(STR16("Stereo In"));
						getAudioOutput(0)->setArrangement(outputs[0]);
						getAudioOutput(0)->setName(STR16("Stereo Out"));
						result = kResultTrue;
					}
					// the host want something different than 1->1 or 2->2 : in this case we want stereo
					else if (bus->getArrangement() != Vst::SpeakerArr::kStereo)
					{
						getAudioInput(0)->setArrangement(Vst::SpeakerArr::kStereo);
						getAudioInput(0)->setName(STR16("Stereo In"));
						getAudioOutput(0)->setArrangement(Vst::SpeakerArr::kStereo);
						getAudioOutput(0)->setName(STR16("Stereo Out"));
						result = kResultFalse;
					}

					return result;
				}
			}
		}
		return kResultFalse;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::setupProcessing(Vst::ProcessSetup& newSetup)
	{
		isOffline = (newSetup.processMode == Steinberg::Vst::kOffline);

		Vst::SpeakerArrangement arr;
		getBusArrangement(Vst::BusDirections::kInput, 0, arr);
        uint16_t numChannels = static_cast<uint16_t> (Vst::SpeakerArr::getChannelCount(arr));

		Band_Split.resize(numChannels);
		for (int32 channel = 0; channel < numChannels; channel++)
			Band_Split_set(&this->Band_Split[channel], 240.0, 2400.0, newSetup.sampleRate);

		adaaFull.assign(numChannels, ADAA2());   // ADAA history, one per channel + per split band
		adaaL.assign(numChannels, ADAA2());
		adaaM.assign(numChannels, ADAA2());
		adaaH.assign(numChannels, ADAA2());

		const int32 maxBlock = newSetup.maxSamplesPerBlock > 0 ? newSetup.maxSamplesPerBlock : 4096;
		smInArr.assign(maxBlock, 0.0);           // per-block smoothed parameter trajectories
		smOutArr.assign(maxBlock, 0.0);
		smEffArr.assign(maxBlock, 0.0);
		smCurveArr.assign(maxBlock, 0.0);
		smInit = false;                          // snap smoothers to target on the next block

		upSample_21.resize(numChannels);
		upSample_41.resize(numChannels);
		upSample_42.resize(numChannels);
		upSample_81.resize(numChannels);
		upSample_82.resize(numChannels);
		upSample_83.resize(numChannels);

		dnSample_21.resize(numChannels);
		dnSample_41.resize(numChannels);
		dnSample_42.resize(numChannels);
		dnSample_81.resize(numChannels);
		dnSample_82.resize(numChannels);
		dnSample_83.resize(numChannels);

		upSample_2x_Lin.resize(numChannels);
		upSample_4x_Lin.resize(numChannels);
		upSample_8x_Lin.resize(numChannels);
		dnSample_2x_Lin.resize(numChannels);
		dnSample_4x_Lin.resize(numChannels);
		dnSample_8x_Lin.resize(numChannels);

		// ---- Generic min-phase cascade stages (x16/32/64) ----
		for (int s = 0; s < kMaxStages; s++) { upStage[s].resize(numChannels); dnStage[s].resize(numChannels); }
		// ---- Linear-phase r8brain for new factors ----
		upSample_16x_Lin.resize(numChannels); upSample_32x_Lin.resize(numChannels); upSample_64x_Lin.resize(numChannels); upSample_128x_Lin.resize(numChannels); upSample_256x_Lin.resize(numChannels);
		dnSample_16x_Lin.resize(numChannels); dnSample_32x_Lin.resize(numChannels); dnSample_64x_Lin.resize(numChannels); dnSample_128x_Lin.resize(numChannels); dnSample_256x_Lin.resize(numChannels);

		for (int channel = 0; channel < numChannels; channel++)
		{
			memset(upSample_21[channel].coef, 0, sizeof(upSample_21[channel].coef));
			memset(upSample_41[channel].coef, 0, sizeof(upSample_41[channel].coef));
			memset(upSample_42[channel].coef, 0, sizeof(upSample_42[channel].coef));
			memset(upSample_81[channel].coef, 0, sizeof(upSample_81[channel].coef));
			memset(upSample_82[channel].coef, 0, sizeof(upSample_82[channel].coef));
			memset(upSample_83[channel].coef, 0, sizeof(upSample_83[channel].coef));
            
			memset(dnSample_21[channel].coef, 0, sizeof(dnSample_21[channel].coef));
			memset(dnSample_41[channel].coef, 0, sizeof(dnSample_41[channel].coef));
			memset(dnSample_42[channel].coef, 0, sizeof(dnSample_42[channel].coef));
			memset(dnSample_81[channel].coef, 0, sizeof(dnSample_81[channel].coef));
			memset(dnSample_82[channel].coef, 0, sizeof(dnSample_82[channel].coef));
			memset(dnSample_83[channel].coef, 0, sizeof(dnSample_83[channel].coef));
            
			// Strictly HALF_BAND
			Kaiser::calcFilter(96000.0,  0.0, 24000.0, upTap_21, 100.0, upSample_21[channel].coef); //
			Kaiser::calcFilter(96000.0,  0.0, 24000.0, upTap_41, 100.0, upSample_41[channel].coef); //
			Kaiser::calcFilter(192000.0, 0.0, 48000.0, upTap_42, 100.0, upSample_42[channel].coef);
			Kaiser::calcFilter(96000.0,  0.0, 24000.0, upTap_81, 100.0, upSample_81[channel].coef); //
			Kaiser::calcFilter(192000.0, 0.0, 48000.0, upTap_82, 100.0, upSample_82[channel].coef);
			Kaiser::calcFilter(384000.0, 0.0, 96000.0, upTap_83, 100.0, upSample_83[channel].coef);

			Kaiser::calcFilter(96000.0,  0.0, 24000.0, dnTap_21, 100.0, dnSample_21[channel].coef); //
			Kaiser::calcFilter(96000.0,  0.0, 24000.0, dnTap_41, 100.0, dnSample_41[channel].coef); //
			Kaiser::calcFilter(192000.0, 0.0, 48000.0, dnTap_42, 100.0, dnSample_42[channel].coef);
			Kaiser::calcFilter(96000.0,  0.0, 24000.0, dnTap_81, 100.0, dnSample_81[channel].coef); //
			Kaiser::calcFilter(192000.0, 0.0, 48000.0, dnTap_82, 100.0, dnSample_82[channel].coef);
			Kaiser::calcFilter(384000.0, 0.0, 96000.0, dnTap_83, 100.0, dnSample_83[channel].coef);

			for (int i = 0; i < upTap_21; i++) upSample_21[channel].coef[i] *= 2.0;
			for (int i = 0; i < upTap_41; i++) upSample_41[channel].coef[i] *= 2.0;
			for (int i = 0; i < upTap_42; i++) upSample_42[channel].coef[i] *= 2.0;
			for (int i = 0; i < upTap_81; i++) upSample_81[channel].coef[i] *= 2.0;
			for (int i = 0; i < upTap_82; i++) upSample_82[channel].coef[i] *= 2.0;
			for (int i = 0; i < upTap_83; i++) upSample_83[channel].coef[i] *= 2.0;


#define FLT_SET(filter, tap_size) \
filter[channel].TAP_SIZE = tap_size; \
filter[channel].TAP_HALF = tap_size / 2; \
filter[channel].TAP_HALF_HALF = filter[channel].TAP_HALF / 2; \
filter[channel].TAP_CONDITION = tap_size % 4; \
filter[channel].pos = maxTap / 2; \
if (filter[channel].TAP_CONDITION == 1) \
{ \
	/* 0 == coef[0], coef[2], coef[4], coef[6], ... */ \
	filter[channel].START = 1; \
} \
else if (filter[channel].TAP_CONDITION == 3) \
{ \
	/* 0 == coef[1], coef[3], coef[5], coef[7], ... */ \
	filter[channel].START = 0; \
} \

			FLT_SET(upSample_21, upTap_21);
			FLT_SET(upSample_41, upTap_41);
			FLT_SET(upSample_42, upTap_42);
			FLT_SET(upSample_81, upTap_81);
			FLT_SET(upSample_82, upTap_82);
			FLT_SET(upSample_83, upTap_83);

			FLT_SET(dnSample_21, dnTap_21);
			FLT_SET(dnSample_41, dnTap_41);
			FLT_SET(dnSample_42, dnTap_42);
			FLT_SET(dnSample_81, dnTap_81);
			FLT_SET(dnSample_82, dnTap_82);
			FLT_SET(dnSample_83, dnTap_83);

			upSample_2x_Lin[channel] = new r8b::CDSPResampler24(1.0, 2.0, 1 * 2, 2.0);
			upSample_4x_Lin[channel] = new r8b::CDSPResampler24(1.0, 4.0, 1 * 4, 2.1);
			upSample_8x_Lin[channel] = new r8b::CDSPResampler24(1.0, 8.0, 1 * 8, 2.2);
			dnSample_2x_Lin[channel] = new r8b::CDSPResampler24(2.0, 1.0, 1 * 2, 2.0);
			dnSample_4x_Lin[channel] = new r8b::CDSPResampler24(4.0, 1.0, 1 * 4, 2.1);
			dnSample_8x_Lin[channel] = new r8b::CDSPResampler24(8.0, 1.0, 1 * 8, 2.2);

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
			upSample_128x_Lin[channel] = new r8b::CDSPResampler24(1.0, 128.0, 1 * 128, 2.6);
			upSample_256x_Lin[channel] = new r8b::CDSPResampler24(1.0, 256.0, 1 * 256, 2.7);
			dnSample_16x_Lin[channel] = new r8b::CDSPResampler24(16.0, 1.0, 1 * 16, 2.3);
			dnSample_32x_Lin[channel] = new r8b::CDSPResampler24(32.0, 1.0, 1 * 32, 2.4);
			dnSample_64x_Lin[channel] = new r8b::CDSPResampler24(64.0, 1.0, 1 * 64, 2.5);
			dnSample_128x_Lin[channel] = new r8b::CDSPResampler24(128.0, 1.0, 1 * 128, 2.6);
			dnSample_256x_Lin[channel] = new r8b::CDSPResampler24(256.0, 1.0, 1 * 256, 2.7);

			if (channel == 0) {
				// Exact linear-phase (r8brain) latencies (mirrors the commented formula at processor.h:366-368).
				latency_r8b_x16 = -1 + 2 * (int32)upSample_16x_Lin[0]->getInLenBeforeOutPos(1);
				latency_r8b_x32 = -1 + 2 * (int32)upSample_32x_Lin[0]->getInLenBeforeOutPos(1);
				latency_r8b_x64 = -1 + 2 * (int32)upSample_64x_Lin[0]->getInLenBeforeOutPos(1);
				latency_r8b_x128 = -1 + 2 * (int32)upSample_128x_Lin[0]->getInLenBeforeOutPos(1);
				latency_r8b_x256 = -1 + 2 * (int32)upSample_256x_Lin[0]->getInLenBeforeOutPos(1);
			}
		}

		VuInput.setChannel(numChannels);
		VuInput.setType(LevelEnvelopeFollower::Peak);
		VuInput.setDecay(3.0);
		VuInput.prepare(newSetup.sampleRate);

		VuOutput.setChannel(numChannels);
		VuOutput.setType(LevelEnvelopeFollower::Peak);
		VuOutput.setDecay(3.0);
		VuOutput.prepare(newSetup.sampleRate);

		fInputVu.resize(numChannels, 0.0);
		fOutputVu.resize(numChannels, 0.0);

		buff.resize(numChannels);
		buff_head.resize(numChannels);
		for (int channel = 0; channel < numChannels; channel++)
		{
			buff[channel].resize(newSetup.maxSamplesPerBlock, 0.0);
			buff_head[channel] = buff[channel].data();
		}

		//--- called before any processing ----
		return AudioEffect::setupProcessing(newSetup);
	}

	uint32 PLUGIN_API JSIF_Processor::getLatencySamples()
	{
        //FDebugPrint("[ FDebugPrint ] getLatencySamples\n");
		overSample os = effectiveOS();
		int32 lat = 0;
		if (fParamPhase) {
			if      (os == overSample_1x)  lat = 0;
			else if (os == overSample_2x)  lat = latency_r8b_x2;
			else if (os == overSample_4x)  lat = latency_r8b_x4;
			else if (os == overSample_8x)  lat = latency_r8b_x8;
			else if (os == overSample_16x) lat = latency_r8b_x16;
			else if (os == overSample_32x) lat = latency_r8b_x32;
			else if (os == overSample_64x) lat = latency_r8b_x64;
			else if (os == overSample_128x) lat = latency_r8b_x128;
			else                                 lat = latency_r8b_x256;
		}
		else {
			if      (os == overSample_1x)  lat = 0;
			else if (os == overSample_2x)  lat = latency_Fir_x2;
			else if (os == overSample_4x)  lat = latency_Fir_x4;
			else if (os == overSample_8x)  lat = latency_Fir_x8;
			else if (os == overSample_16x) lat = latency_Fir_x16;
			else if (os == overSample_32x) lat = latency_Fir_x32;
			else if (os == overSample_64x) lat = latency_Fir_x64;
			else if (os == overSample_128x) lat = latency_Fir_x128;
			else                                 lat = latency_Fir_x256;
		}
		// 2nd-order ADAA adds 1 sample of group delay in the oversampled domain. At x1
		// that is exactly 1 base-rate sample; at higher OS it is sub-sample (negligible).
		// 1st-order ADAA has only a half-sample group delay -> reported as 0 (non-integer).
		if (adaaOrder == 2 && os == overSample_1x) lat += 1;
		return lat;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::setActive(TBool state)
	{
		//--- called when the Plug-in is enable/disable (On/Off) -----
		if (state) {                                   // clear ADAA history on (re)activation
			for (auto& s : adaaFull) s = ADAA2();
			for (auto& s : adaaL)    s = ADAA2();
			for (auto& s : adaaM)    s = ADAA2();
			for (auto& s : adaaH)    s = ADAA2();
			smInit = false;                            // snap parameter smoothers on next block
		}
		return AudioEffect::setActive(state);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::process(Vst::ProcessData& data)
	{		        
		Vst::IParameterChanges* paramChanges = data.inputParameterChanges;

		if (paramChanges)
		{
			int32 numParamsChanged = paramChanges->getParameterCount();

			for (int32 index = 0; index < numParamsChanged; index++)
			{
				Vst::IParamValueQueue* paramQueue = paramChanges->getParameterData(index);

				if (paramQueue)
				{
					Vst::ParamValue value;
					int32 sampleOffset;
					int32 numPoints = paramQueue->getPointCount();

					/*/*/
					if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) {
						switch (paramQueue->getParameterId()) {
						case kParamInput:  fInput      = value;          break;
						case kParamEffect: fEffect     = value;          break;
						case kParamCurve:  fCurve      = value;          break;
						case kParamClip:   bClip       = (value > 0.5f); break;
						case kParamOutput: fOutput     = value;          break;
						case kParamBypass: bBypass     = (value > 0.5f); break;
						case kParamIn:     bIn         = (value > 0.5f); break;
						case kParamZoom:   fParamZoom  = value;          break;
						case kParamSplit:  bSplit      = (value > 0.5f); break;
						case kParamADAA:
						                   adaaOrder   = (int32)(value * 2.0 + 0.5);   // 0=Off,1=1st,2=2nd
						                   sendTextMessage("OS\n");   // latency changes (2nd order x1: +1 sample)
						                   break;
						case kParamPhase:
						                   fParamPhase = (value > 0.5f);
						                   sendTextMessage("OS\n");
						                   break;
						case kParamOS:
						                   fParamOS    = static_cast<overSample>(Steinberg::FromNormalized<ParamValue> (value, overSample_num));
						                   sendTextMessage("OS\n");
						                   break;
						case kParamOSOffline:
						                   fParamOSOffline = static_cast<overSample>(Steinberg::FromNormalized<ParamValue> (value, overSample_num));
						                   sendTextMessage("OS\n");
						                   break;
						}
					}
				}
			}
		}

		if (data.numInputs == 0 || data.numOutputs == 0) 
		{
			return kResultOk;
		}

		//---get audio buffers----------------
		uint32 sampleFramesSize = getSampleFramesSizeInBytes(processSetup, data.numSamples);
		void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
		void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
		Vst::SampleRate SampleRate = processSetup.sampleRate;
        int32 numChannels = data.inputs[0].numChannels;

		// init VuMeters
		for (auto& loop : fInputVu) loop = init_meter;
		for (auto& loop : fOutputVu) loop = init_meter;
		fMeterVu = init_meter;

		//---check if silence---------------
		// check if all channel are silent then process silent
		if (data.inputs[0].silenceFlags == Vst::getChannelMask(data.inputs[0].numChannels))
		{
			// mark output silence too (it will help the host to propagate the silence)
			data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;

			// the plug-in has to be sure that if it sets the flags silence that the output buffer are
			// clear
			for (int32 channel = 0; channel < numChannels; channel++)
			{
				// do not need to be cleared if the buffers are the same (in this case input buffer are already cleared by the host)
				if (in[channel] != out[channel])
				{
					memset(out[channel], 0, sampleFramesSize);
				}
			}
			// Even Silence, we should process VU meter and send data. 
			// return kResultOk;
		}
		else
		{
			data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;

			//---in bypass mode outputs should be like inputs-----
			if (bBypass)
			{
				if (data.symbolicSampleSize == Vst::kSample32)
				{
					latencyBypass<Vst::Sample32>((Vst::Sample32**)in, (Vst::Sample32**)out, numChannels, SampleRate, data.numSamples);
				}
				else if (data.symbolicSampleSize == Vst::kSample64)
				{
					latencyBypass<Vst::Sample64>((Vst::Sample64**)in, (Vst::Sample64**)out, numChannels, SampleRate, data.numSamples);
				}

				fMeterVu = 0.0;
			}
			else {
				if (data.symbolicSampleSize == Vst::kSample32)
				{
					processAudio<Vst::Sample32>((Vst::Sample32**)in, (Vst::Sample32**)out, numChannels, SampleRate, data.numSamples);
				}
				else if (data.symbolicSampleSize == Vst::kSample64)
				{
					processAudio<Vst::Sample64>((Vst::Sample64**)in, (Vst::Sample64**)out, numChannels, SampleRate, data.numSamples);
				}

				long div = data.numSamples;

				Meter /= (double)div;
				Meter /= (double)numChannels;
				Meter *= 1000.0;

				fMeterVu = 0.4 * log10(std::abs(Meter));
				fMeterVu *= fEffect;
				fMeterVu *= bIn;
				// FDebugPrint("Meter = %f \n", fMeterVu);
			}

			double monoIn = 0.0;
			for (auto& loop : fInputVu) { loop = VuPPMconvert(loop);  monoIn += loop; }
			for (auto& loop : fOutputVu) loop = VuPPMconvert(loop);
			monoIn /= (double)numChannels;
			fMeterVu *= monoIn;
		}
        
        //---send a message
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            double data = (numChannels > 0) ? fInputVu[0] : 0.0;
            message->getAttributes()->setFloat ("vuInL", data);
            sendMessage (message);
        }
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            double data = (numChannels > 1) ? fInputVu[1] : ((numChannels > 0) ? fInputVu[0] : 0.0);
            message->getAttributes ()->setFloat ("vuInR", data);
            sendMessage (message);
        }
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            double data = (numChannels > 0) ? fOutputVu[0] : 0.0;
            message->getAttributes ()->setFloat ("vuOutL", data);
            sendMessage (message);
        }
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            double data = (numChannels > 1) ? fOutputVu[1] : ((numChannels > 0) ? fOutputVu[0] : 0.0);
            message->getAttributes ()->setFloat ("vuOutR", data);
            sendMessage (message);
        }
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            double data = fMeterVu;
            message->getAttributes ()->setFloat ("vuEffect", data);
            sendMessage (message);
        }
        if (IPtr<Vst::IMessage> message = owned (allocateMessage ()))
        {
            message->setMessageID ("VUmeter");
            message->getAttributes ()->setInt ("update", true);
            sendMessage (message);
        }

		return kResultOk;
	}


	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::setState(IBStream* state)
	{
		// called when we load a preset, the model has to be reloaded
		IBStreamer streamer(state, kLittleEndian);

		Vst::ParamValue savedInput  = 0.0;
		Vst::ParamValue savedEffect = 0.0;
		Vst::ParamValue savedCurve  = 0.0;
		Vst::ParamValue savedOutput = 0.0;
		Vst::ParamValue savedOS     = 0.0;
		int32           savedClip   = 0;
		int32           savedIn     = 0;
		int32           savedSplit  = 0;
		Vst::ParamValue savedZoom   = 0.0;
		Vst::ParamValue savedLin    = 0;
		int32           savedBypass = 0;

		if (streamer.readDouble(savedInput)  == false) return kResultFalse;
		if (streamer.readDouble(savedEffect) == false) return kResultFalse;
		if (streamer.readDouble(savedCurve)  == false) return kResultFalse;
		if (streamer.readDouble(savedOutput) == false) return kResultFalse;
		if (streamer.readDouble(savedOS)     == false) return kResultFalse;
		if (streamer.readInt32(savedClip)    == false) return kResultFalse;
		if (streamer.readInt32(savedIn)      == false) return kResultFalse;
		if (streamer.readInt32(savedSplit)   == false) return kResultFalse;
		if (streamer.readDouble(savedZoom)   == false) return kResultFalse;
		if (streamer.readDouble(savedLin)    == false) return kResultFalse;
		if (streamer.readInt32(savedBypass)  == false) return kResultFalse;

		Vst::ParamValue savedOSOff = Steinberg::ToNormalized<ParamValue>(static_cast<ParamValue>(init_OSOffline), overSample_num);
		streamer.readDouble(savedOSOff);   // tolerant: old states lack this field; keep default on short read

		int32 savedADAA = init_ADAA_order;
		streamer.readInt32(savedADAA);     // tolerant: old states lack this field
		adaaOrder = (savedADAA < 0) ? 0 : (savedADAA > 2) ? 2 : savedADAA;

		fInput     = savedInput;
		fEffect    = savedEffect;
		fCurve     = savedCurve;
		fOutput    = savedOutput;
		fParamOS   = static_cast<overSample>(Steinberg::FromNormalized<ParamValue> (savedOS, overSample_num));
		bClip      = savedClip   > 0;
		bIn        = savedIn     > 0;
		bSplit     = savedSplit  > 0;
		fParamZoom = savedZoom;
		fParamPhase= savedLin;
		bBypass    = savedBypass > 0;
		fParamOSOffline = static_cast<overSample>(Steinberg::FromNormalized<ParamValue> (savedOSOff, overSample_num));

		if (Vst::Helpers::isProjectState(state) == kResultTrue)
		{
			// we are in project loading context...

			// Example of using the IStreamAttributes interface
			FUnknownPtr<Vst::IStreamAttributes> stream(state);
			if (stream)
			{
				if (Vst::IAttributeList* list = stream->getAttributes())
				{
					// get the full file path of this state
					Vst::TChar fullPath[1024];
					memset(fullPath, 0, 1024 * sizeof(Vst::TChar));
					if (list->getString(Vst::PresetAttributes::kFilePathStringType, fullPath,
						1024 * sizeof(Vst::TChar)) == kResultTrue)
					{
						// here we have the full path ...
					}
				}
			}
		}


		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API JSIF_Processor::getState(IBStream* state)
	{
		// here we need to save the model
		IBStreamer streamer(state, kLittleEndian);

		streamer.writeDouble(fInput);
		streamer.writeDouble(fEffect);
		streamer.writeDouble(fCurve);
		streamer.writeDouble(fOutput);
		streamer.writeDouble(Steinberg::ToNormalized<ParamValue> (static_cast<ParamValue>(fParamOS), overSample_num));
		streamer.writeInt32(bClip ? 1 : 0);
		streamer.writeInt32(bIn ? 1 : 0);
		streamer.writeInt32(bSplit ? 1 : 0);
		streamer.writeDouble(fParamZoom);
		streamer.writeDouble(fParamPhase);
		streamer.writeInt32(bBypass ? 1 : 0);
		streamer.writeDouble(Steinberg::ToNormalized<ParamValue> (static_cast<ParamValue>(fParamOSOffline), overSample_num));
		streamer.writeInt32(adaaOrder);

		return kResultOk;
	}


    Vst::ParamValue JSIF_Processor::VuPPMconvert(Vst::ParamValue plainValue)
	{
        Vst::ParamValue dB = 20 * log10(plainValue);
        Vst::ParamValue normValue;

		if      (dB >=  6.0) normValue = 1.0;
		else if (dB >=  5.0) normValue = 0.96;
		else if (dB >=  4.0) normValue = 0.92;
		else if (dB >=  3.0) normValue = 0.88;
		else if (dB >=  2.0) normValue = 0.84;
		else if (dB >=  1.0) normValue = 0.80;
		else if (dB >=  0.0) normValue = 0.76;
		else if (dB >= -1.0) normValue = 0.72;
		else if (dB >= -2.0) normValue = 0.68;
		else if (dB >= -3.0) normValue = 0.64;
		else if (dB >= -4.0) normValue = 0.60;
		else if (dB >= -5.0) normValue = 0.56;
		else if (dB >= -6.0) normValue = 0.52;
		else if (dB >= -8.0) normValue = 0.48;
		else if (dB >= -10 ) normValue = 0.44;
		else if (dB >= -12 ) normValue = 0.40;
		else if (dB >= -14 ) normValue = 0.36;
		else if (dB >= -16 ) normValue = 0.32;
		else if (dB >= -18 ) normValue = 0.28;
		else if (dB >= -22 ) normValue = 0.24;
		else if (dB >= -26 ) normValue = 0.20;
		else if (dB >= -30 ) normValue = 0.16;
		else if (dB >= -34 ) normValue = 0.12;
		else if (dB >= -38 ) normValue = 0.08;
		else if (dB >= -42 ) normValue = 0.04;
		else normValue = 0.0;

		return normValue;
	}

	//------------------------------------------------------------------------
	template <typename SampleType>
	void JSIF_Processor::processVuPPM_In(SampleType** inputs, int32 numChannels, int32 sampleFrames)
	{
		VuInput.update(inputs, numChannels, sampleFrames);
		for (int ch = 0; ch < numChannels; ch++)
		{
			fInputVu[ch] = VuInput.getEnv(ch);
		}
		return;
	}


	Vst::Sample64 JSIF_Processor::process_inflator(Vst::Sample64 inputSample)
	{
		// Vst::Sample64 drySample = inputSample;
		Vst::Sample64 sign;

		if (inputSample > 0.0) sign =  1.0;
		else                   sign = -1.0;

		Vst::Sample64 s1 = fabs(inputSample);
		Vst::Sample64 s2 = s1 * s1;
		Vst::Sample64 s3 = s2 * s1;
		Vst::Sample64 s4 = s2 * s2;

		if      (s1 >= 2.0) inputSample = 0.0;
		else if (s1 >  1.0) inputSample = (2.0 * s1) - s2;
		else                inputSample = (curveA * s1) +
		                                  (curveB * s2) +
		                                  (curveC * s3) -
		                                  (curveD * (s2 - (2.0 * s3) + s4));
		inputSample *= sign;

		return inputSample;
	}

	//------------------------------------------------------------------------
	// Second-order antiderivative antialiasing of process_inflator (Parker/Zavalishin/
	// Le Bivic 2016; Bilbao et al. 2017 divided-difference form). Exact for this
	// piecewise-polynomial shaper. Nested fallbacks handle the near-coincident sample
	// cases. State is per-instance (full-band or one split band, per channel).
	//------------------------------------------------------------------------
	Vst::Sample64 JSIF_Processor::process_inflator_adaa2(Vst::Sample64 x, ADAA2& s)
	{
		const Vst::Sample64 TOL = 1.0e-5;
		const Vst::Sample64 ad2x = inflator_F2(x);                         // F2(x)
		Vst::Sample64 d1;                                                  // divided diff of F2 over (x, x1)
		if (fabs(x - s.x1) > TOL) d1 = (ad2x - s.ad2x1) / (x - s.x1);
		else                      d1 = inflator_F1(0.5 * (x + s.x1));      // inner fallback
		Vst::Sample64 y;
		if (fabs(x - s.x2) > TOL) {
			y = (2.0 / (x - s.x2)) * (d1 - s.d2);                         // outer well-conditioned
		}
		else {
			const Vst::Sample64 xbar  = 0.5 * (x + s.x2);
			const Vst::Sample64 delta = xbar - s.x1;
			if (fabs(delta) > TOL)
				y = (2.0 / delta) * (inflator_F1(xbar) + (s.ad2x1 - inflator_F2(xbar)) / delta);
			else
				y = process_inflator(0.5 * (xbar + s.x1));                // deepest fallback: f at midpoint
		}
		s.d2    = d1;                                                      // advance state
		s.x2    = s.x1;
		s.x1    = x;
		s.ad2x1 = ad2x;
		return y;
	}

	//------------------------------------------------------------------------
	// First-order antiderivative antialiasing of process_inflator. Recomputes F1 on
	// both endpoints with the CURRENT coefficients, so it stays consistent through
	// curve moves with no cached state (no spikes) and has no in-band notch (its only
	// transfer-function zero is at Nyquist). Cheaper than order 2 and zero latency.
	//------------------------------------------------------------------------
	Vst::Sample64 JSIF_Processor::process_inflator_adaa1(Vst::Sample64 x, ADAA2& s)
	{
		const Vst::Sample64 TOL = 1.0e-5;
		Vst::Sample64 y;
		if (fabs(x - s.x1) > TOL) y = (inflator_F1(x) - inflator_F1(s.x1)) / (x - s.x1);
		else                      y = process_inflator(0.5 * (x + s.x1));   // L'Hopital midpoint
		s.x1 = x;
		return y;
	}

	template <typename SampleType>
	void JSIF_Processor::latencyBypass(
		SampleType** inputs,
		SampleType** outputs,
		int32 numChannels,
		Vst::SampleRate SampleRate,
		int32 sampleFrames
	)
	{
		overSample os = effectiveOS();
		int32 latency = 0;
		if (!fParamPhase) {
			if      (os == overSample_2x)  latency = latency_Fir_x2;
			else if (os == overSample_4x)  latency = latency_Fir_x4;
			else if (os == overSample_8x)  latency = latency_Fir_x8;
			else if (os == overSample_16x) latency = latency_Fir_x16;
			else if (os == overSample_32x) latency = latency_Fir_x32;
			else if (os == overSample_64x) latency = latency_Fir_x64;
			else if (os == overSample_128x) latency = latency_Fir_x128;
			else if (os == overSample_256x) latency = latency_Fir_x256;
		}
		else {
			if      (os == overSample_2x)  latency = latency_r8b_x2;
			else if (os == overSample_4x)  latency = latency_r8b_x4;
			else if (os == overSample_8x)  latency = latency_r8b_x8;
			else if (os == overSample_16x) latency = latency_r8b_x16;
			else if (os == overSample_32x) latency = latency_r8b_x32;
			else if (os == overSample_64x) latency = latency_r8b_x64;
			else if (os == overSample_128x) latency = latency_r8b_x128;
			else if (os == overSample_256x) latency = latency_r8b_x256;
		}

		for (int32 channel = 0; channel < numChannels; channel++)
		{
			SampleType* ptrIn  = (SampleType*) inputs[channel];
			SampleType* ptrOut = (SampleType*)outputs[channel];
			int32 samples = sampleFrames;

			if (os == overSample_1x) {
				memcpy(ptrOut, ptrIn, sizeof(SampleType) * sampleFrames);
				continue;
			}

			if (latency != latency_q[channel].size()) {
				int32 diff = latency - (int32)latency_q[channel].size();
				if (diff > 0) 
					for (int i = 0; i <  diff; i++) latency_q[channel].push_back(0.0);
				else 
					for (int i = 0; i < -diff; i++) latency_q[channel].pop_front();
			}

			while (--samples >= 0)
			{
				double inin = *ptrIn;
				*ptrOut = (SampleType)latency_q[channel].front();
				latency_q[channel].pop_front();
				latency_q[channel].push_back(inin);

				ptrIn++;
				ptrOut++;
			}
		}
		VuInput.update(inputs, numChannels, sampleFrames);
		VuOutput.update(outputs, numChannels, sampleFrames);
 
		for (int ch = 0; ch < numChannels; ch++)
		{
			fInputVu[ch]  = VuInput.getEnv(ch);
			fOutputVu[ch] = VuOutput.getEnv(ch);
		}

		return;
	}


	template <typename SampleType>
	void JSIF_Processor::processAudio(
		SampleType** inputs, 
		SampleType** outputs, 
		int32 numChannels,
		Vst::SampleRate SampleRate, 
		int32 sampleFrames
	)
	{
		// --- per-block parameter-smoothing setup ----------------------------------------------
		// Input/Output gain, Effect mix and Curve are one-pole smoothed at the base rate so that
		// moving the knobs introduces THD gradually (no zipper) and keeps ADAA consistent.
		Vst::Sample64 targetIn  = expf(logf(10.f) * (24.0 * fInput  - 12.0) / 20.f);
		Vst::Sample64 targetOut = expf(logf(10.f) * (24.0 * fOutput - 12.0) / 20.f);
		Vst::Sample64 targetEff = fEffect;
		Vst::Sample64 targetCp  = fCurve - 0.5;

		if (!smInit) {                                  // snap on (re)activation / first block
			smInGain = targetIn; smOutGain = targetOut; smEffect = targetEff; smCurvepct = targetCp;
			smInit = true;
		}
		const Vst::Sample64 smCoef = 1.0 - exp(-1.0 / (0.010 * SampleRate));   // ~10 ms one-pole
		const Vst::Sample64 EPS = 1.0e-7;
		bool smoothing = (fabs(targetIn  - smInGain ) > EPS) || (fabs(targetOut - smOutGain) > EPS) ||
		                 (fabs(targetEff - smEffect ) > EPS) || (fabs(targetCp  - smCurvepct) > EPS);
		if (smoothing) {                                // build per-sample trajectories (advance once/sample)
			for (int32 i = 0; i < sampleFrames; i++) {
				smInGain   += (targetIn   - smInGain ) * smCoef;  smInArr[i]    = smInGain;
				smOutGain  += (targetOut  - smOutGain) * smCoef;  smOutArr[i]   = smOutGain;
				smEffect   += (targetEff  - smEffect ) * smCoef;  smEffArr[i]   = smEffect;
				smCurvepct += (targetCp   - smCurvepct) * smCoef; smCurveArr[i] = smCurvepct;
			}
		}
		else {                                          // converged: snap, compute coefficients once
			smInGain = targetIn; smOutGain = targetOut; smEffect = targetEff; smCurvepct = targetCp;
			updateCurveCoeffs(smCurvepct);
			if (adaaOrder == 2 && smCurvepct != adaaCachedCurve) {   // refresh order-2 caches on a discrete curve step
				for (auto& s : adaaFull) adaaRecache(s);
				for (auto& s : adaaL)    adaaRecache(s);
				for (auto& s : adaaM)    adaaRecache(s);
				for (auto& s : adaaH)    adaaRecache(s);
				adaaCachedCurve = smCurvepct;
			}
		}

		overSample os = effectiveOS();
		int32 latency = 0;
		if (!fParamPhase) {
			if      (os == overSample_2x)  latency = latency_Fir_x2;
			else if (os == overSample_4x)  latency = latency_Fir_x4;
			else if (os == overSample_8x)  latency = latency_Fir_x8;
			else if (os == overSample_16x) latency = latency_Fir_x16;
			else if (os == overSample_32x) latency = latency_Fir_x32;
			else if (os == overSample_64x) latency = latency_Fir_x64;
			else if (os == overSample_128x) latency = latency_Fir_x128;
			else if (os == overSample_256x) latency = latency_Fir_x256;
		}
		else {
			if      (os == overSample_2x)  latency = latency_r8b_x2;
			else if (os == overSample_4x)  latency = latency_r8b_x4;
			else if (os == overSample_8x)  latency = latency_r8b_x8;
			else if (os == overSample_16x) latency = latency_r8b_x16;
			else if (os == overSample_32x) latency = latency_r8b_x32;
			else if (os == overSample_64x) latency = latency_r8b_x64;
			else if (os == overSample_128x) latency = latency_r8b_x128;
			else if (os == overSample_256x) latency = latency_r8b_x256;
		}

		int32 oversampling = 1;
		if      (os == overSample_2x)  oversampling = 2;
		else if (os == overSample_4x)  oversampling = 4;
		else if (os == overSample_8x)  oversampling = 8;
		else if (os == overSample_16x) oversampling = 16;
		else if (os == overSample_32x) oversampling = 32;
		else if (os == overSample_64x) oversampling = 64;
		else if (os == overSample_128x) oversampling = 128;
		else if (os == overSample_256x) oversampling = 256;

		Vst::SampleRate targetSampleRate = SampleRate * oversampling;

		for (int32 channel = 0; channel < numChannels; channel++)
			if (this->Band_Split[channel].SR != targetSampleRate)
				Band_Split_set(&this->Band_Split[channel], 240.0, 2400.0, targetSampleRate);

        double up_x[256] = {0.0, };
        double up_y[256] = {0.0, };

		Meter = 0.0;
		double t = 0.0;

		for (int32 channel = 0; channel < numChannels; channel++)
		{
			SampleType* ptrIn  = (SampleType*) inputs[channel];
			SampleType* ptrOut = (SampleType*)outputs[channel];
			double* buff_in = buff[channel].data();
			
			if (latency != latency_q[channel].size()) {
				int32 diff = latency - (int32)latency_q[channel].size();
				if (diff > 0) {
					for (int i = 0; i < diff; i++) latency_q[channel].push_back(0.0);
				}
				else {
					for (int i = 0; i < -diff; i++) latency_q[channel].pop_front();
				}
			}

			int32 samples = sampleFrames;
			int32 n = 0;

			// Per-sample body, parameterized by the (possibly smoothed) gains. Defined once and
			// invoked from two loops below, so the common static-knob path carries ZERO smoothing
			// overhead (block-constant gains, no per-sample branch) while knob moves stay click-free.
			auto processOneSample = [&](Vst::Sample64 gIn, Vst::Sample64 gOut, Vst::Sample64 mEff)
			{
				Vst::Sample64 inputSample = *ptrIn;

				inputSample *= gIn;

				if (bClip) {
					if      (inputSample >  1.0) inputSample =  1.0;
					else if (inputSample < -1.0) inputSample = -1.0;
				}

				if      (inputSample >  2.0) inputSample =  2.0;
				else if (inputSample < -2.0) inputSample = -2.0;

				Vst::Sample64 drySample = inputSample;

				// Upsampling
				if              (os == overSample_1x) up_x[0] = inputSample;
				else {
					if (!fParamPhase) {
						if      (os == overSample_2x)  Fir_x2_up(&inputSample, up_x, channel);
						else if (os == overSample_4x)  Fir_x4_up(&inputSample, up_x, channel);
						else if (os == overSample_8x)  Fir_x8_up(&inputSample, up_x, channel);
						else if (os == overSample_16x) Fir_up_n(&inputSample, up_x, channel, 4);
						else if (os == overSample_32x) Fir_up_n(&inputSample, up_x, channel, 5);
						else if (os == overSample_64x) Fir_up_n(&inputSample, up_x, channel, 6);
						else if (os == overSample_128x) Fir_up_n(&inputSample, up_x, channel, 7);
						else                                 Fir_up_n(&inputSample, up_x, channel, 8);
					}
					else {
						double* upSample_buff;
						if      (os == overSample_2x)  upSample_2x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_4x)  upSample_4x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_8x)  upSample_8x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_16x) upSample_16x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_32x) upSample_32x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_64x) upSample_64x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else if (os == overSample_128x) upSample_128x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						else                                 upSample_256x_Lin[channel]->process(&inputSample, 1, upSample_buff);
						memcpy(up_x, upSample_buff, sizeof(Vst::Sample64) * oversampling);
					}
				}

				// Processing
				for (int k = 0; k < oversampling; k++) {
					if (!bIn) {
						up_y[k] = up_x[k];
						continue;
					}
					Vst::Sample64 sampleOS = up_x[k];
					if (bSplit) {
						this->Band_Split[channel].LP.R =      this->Band_Split[channel].LP.I  + this->Band_Split[channel].LP.C * (sampleOS - this->Band_Split[channel].LP.I);
						this->Band_Split[channel].LP.I =  2 * this->Band_Split[channel].LP.R  - this->Band_Split[channel].LP.I;

						this->Band_Split[channel].HP.R = (1 - this->Band_Split[channel].HP.C) * this->Band_Split[channel].HP.I + this->Band_Split[channel].HP.C * sampleOS;
						this->Band_Split[channel].HP.I =  2 * this->Band_Split[channel].HP.R  - this->Band_Split[channel].HP.I;

						Vst::Sample64 inputSample_L = this->Band_Split[channel].LP.R;
						Vst::Sample64 inputSample_H = sampleOS - this->Band_Split[channel].HP.R;
						Vst::Sample64 inputSample_M = this->Band_Split[channel].HP.R - this->Band_Split[channel].LP.R;

						if (adaaOrder == 2) {
							up_y[k] = process_inflator_adaa2(inputSample_L, adaaL[channel]) +
							          process_inflator_adaa2(inputSample_M * this->Band_Split[channel].G, adaaM[channel]) * this->Band_Split[channel].GR +
							          process_inflator_adaa2(inputSample_H, adaaH[channel]);
						}
						else if (adaaOrder == 1) {
							up_y[k] = process_inflator_adaa1(inputSample_L, adaaL[channel]) +
							          process_inflator_adaa1(inputSample_M * this->Band_Split[channel].G, adaaM[channel]) * this->Band_Split[channel].GR +
							          process_inflator_adaa1(inputSample_H, adaaH[channel]);
						}
						else {
							up_y[k] = process_inflator(inputSample_L) +
							          process_inflator(inputSample_M * this->Band_Split[channel].G) * this->Band_Split[channel].GR +
							          process_inflator(inputSample_H);
						}
					}
					else {
						up_y[k] = (adaaOrder == 2) ? process_inflator_adaa2(sampleOS, adaaFull[channel])
						        : (adaaOrder == 1) ? process_inflator_adaa1(sampleOS, adaaFull[channel])
						        :                    process_inflator(sampleOS);
					}
					if (bClip && up_y[k] >  1.0) up_y[k] =  1.0;
					if (bClip && up_y[k] < -1.0) up_y[k] = -1.0;
				}

				// Downsampling
				if              (os == overSample_1x) inputSample = up_y[0];
				else {
					if (!fParamPhase) {
						if      (os == overSample_2x)  Fir_x2_dn(up_y, &inputSample, channel);
						else if (os == overSample_4x)  Fir_x4_dn(up_y, &inputSample, channel);
						else if (os == overSample_8x)  Fir_x8_dn(up_y, &inputSample, channel);
						else if (os == overSample_16x) Fir_dn_n(up_y, &inputSample, channel, 4);
						else if (os == overSample_32x) Fir_dn_n(up_y, &inputSample, channel, 5);
						else if (os == overSample_64x) Fir_dn_n(up_y, &inputSample, channel, 6);
						else if (os == overSample_128x) Fir_dn_n(up_y, &inputSample, channel, 7);
						else                                 Fir_dn_n(up_y, &inputSample, channel, 8);
					}
					else {
						double* dnSample_buff;
						if      (os == overSample_2x)  dnSample_2x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_4x)  dnSample_4x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_8x)  dnSample_8x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_16x) dnSample_16x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_32x) dnSample_32x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_64x) dnSample_64x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else if (os == overSample_128x) dnSample_128x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						else                                 dnSample_256x_Lin[channel]->process(up_y, oversampling, dnSample_buff);
						inputSample = *dnSample_buff;
					}
				}


				// Latency compensate
				Vst::Sample64 delayed;
				latency_q[channel].push_back(drySample);
				delayed = latency_q[channel].front();
				latency_q[channel].pop_front();
				*buff_in = delayed;  buff_in++;
                
				inputSample = (delayed * (1.0 - mEff)) + (inputSample * mEff);

				t += std::abs(inputSample) - std::abs(delayed);
				// Meter += 20.0 * (std::log10(std::abs(inputSample)) - std::log10(std::abs(delayed)));

				inputSample *= gOut;

				*ptrOut = (SampleType)inputSample;

				ptrIn++;
				ptrOut++;
			};

			if (!smoothing) {                          // static knobs: lean loop, block-constant gains
				while (--samples >= 0)
					[[msvc::forceinline]] processOneSample(smInGain, smOutGain, smEffect);
			}
			else {                                     // a knob is moving: per-sample smoothed coefficients
				while (--samples >= 0) {
					updateCurveCoeffs(smCurveArr[n]);
					if (adaaOrder == 2) {              // only order-2 caches antiderivatives; order-1 recomputes
						adaaRecache(adaaFull[channel]);
						adaaRecache(adaaL[channel]); adaaRecache(adaaM[channel]); adaaRecache(adaaH[channel]);
					}
					[[msvc::forceinline]] processOneSample(smInArr[n], smOutArr[n], smEffArr[n]);
					n++;
				}
			}
		}
		Meter = 80.0 - t;

		VuInput.update(buff_head.data(), numChannels, sampleFrames);
		VuOutput.update(outputs, numChannels, sampleFrames);

		for (int ch = 0; ch < numChannels; ch++)
		{
			fInputVu[ch]  = VuInput.getEnv(ch);
			fOutputVu[ch] = VuOutput.getEnv(ch);
		}

		return;
	}

	// Compaction delay-line push helpers. Instead of memmove-ing the whole delay line every
	// sub-sample, we advance a read cursor (pos) downward and memmove ("compact") only when it
	// nears the buffer start. HB_upsample/HB_dnsample read from buff+pos, so the data the
	// convolution sees is bit-identical to the old per-sample-memmove scheme (verified offline
	// to 0.0 difference over a full 16x/32x/64x cascade), at ~2x lower FIR cost.
	static inline void hb_push_up(Flt& f, Vst::Sample64 x)
	{
		if (f.pos == 0) {                                  // no room below: lift the window to the top
			int win = f.TAP_HALF + 1;
			memmove(f.buff + (maxTap - win), f.buff + f.pos, sizeof(double) * win);
			f.pos = maxTap - win;
		}
		f.pos--;
		f.buff[f.pos] = x;
	}
	static inline void hb_push_dn(Flt& f, Vst::Sample64 in0, Vst::Sample64 in1)
	{
		if (f.pos < 2) {
			int win = f.TAP_SIZE + 2;
			memmove(f.buff + (maxTap - win), f.buff + f.pos, sizeof(double) * win);
			f.pos = maxTap - win;
		}
		f.pos -= 2;
		f.buff[f.pos + 1] = in1;
		f.buff[f.pos + 2] = in0;
		f.buff[f.pos + f.TAP_SIZE + 1] = 0.0;              // replicate the old code's never-written buff[TAP+1]
	}

	/// Fir Linear Oversamplers
	void JSIF_Processor::HB_upsample(Flt* filter, Vst::Sample64* out)
	{
		// half-band
		const double* b = filter->buff + filter->pos;
		double acc = 0.0;
		for (int coef = filter->START, buff = 0; coef < filter->TAP_HALF; coef += 2, buff++)
		{
			acc += filter->coef[coef] * (b[buff] + b[filter->TAP_HALF - buff - filter->START]);
		}

		double acc_1 = 0.0;
		double acc_2 = 0.0;
		if (filter->TAP_CONDITION == 1)
		{
			acc_1 = filter->coef[filter->TAP_HALF] * b[filter->TAP_HALF_HALF];
			acc_2 = acc;
		}
		else // if (filter->TAP_CONDITION == 3)
		{
			acc_1 = acc;
			acc_2 = filter->coef[filter->TAP_HALF] *  b[filter->TAP_HALF_HALF];
		}

		*(out  ) = acc_1;
		*(out+1) = acc_2;
	}
	void JSIF_Processor::HB_dnsample(Flt* filter, Vst::Sample64* out)
	{
		// half-band
		const double* b = filter->buff + filter->pos;
		double acc = 0.0;
		int TAP_SIZE_1 = filter->TAP_SIZE - 1;
		for (int i = filter->START; i < filter->TAP_HALF; i+=2) // buffer + 2 is offset
		{
			acc += filter->coef[i] * (b[2 + i] + b[2 + TAP_SIZE_1 - i]);
		}
		acc += filter->coef[filter->TAP_HALF] * b[2 + filter->TAP_HALF];
		*out = acc;
	}
	
	// 1 in 2 out
	void JSIF_Processor::Fir_x2_up(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		hb_push_up(upSample_21[channel], *in);
		HB_upsample(&upSample_21[channel], out);

		return;
	}
	// 1 in 4 out
	void JSIF_Processor::Fir_x4_up(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		Vst::Sample64 inter_41[2];
		hb_push_up(upSample_41[channel], *in);
		HB_upsample(&upSample_41[channel], &inter_41[0]);

		hb_push_up(upSample_42[channel], inter_41[0]);
		HB_upsample(&upSample_42[channel], &out[0]);

		hb_push_up(upSample_42[channel], inter_41[1]);
		HB_upsample(&upSample_42[channel], &out[2]);

		return;
	}
	// 1 in 8 out
	void JSIF_Processor::Fir_x8_up(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		Vst::Sample64 inter_81[2];
		hb_push_up(upSample_81[channel], *in);
		HB_upsample(&upSample_81[channel], &inter_81[0]);

		Vst::Sample64 inter_82[4];
		hb_push_up(upSample_82[channel], inter_81[0]);
		HB_upsample(&upSample_82[channel], &inter_82[0]);

		hb_push_up(upSample_82[channel], inter_81[1]);
		HB_upsample(&upSample_82[channel], &inter_82[2]);

		hb_push_up(upSample_83[channel], inter_82[0]);
		HB_upsample(&upSample_83[channel], &out[0]);

		hb_push_up(upSample_83[channel], inter_82[1]);
		HB_upsample(&upSample_83[channel], &out[2]);

		hb_push_up(upSample_83[channel], inter_82[2]);
		HB_upsample(&upSample_83[channel], &out[4]);

		hb_push_up(upSample_83[channel], inter_82[3]);
		HB_upsample(&upSample_83[channel], &out[6]);

		return;
	}
	
	
	// 2 in 1 out
	void JSIF_Processor::Fir_x2_dn(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		hb_push_dn(dnSample_21[channel], in[0], in[1]);
		HB_dnsample(&dnSample_21[channel], out);

		return;
	}
	// 4 in 1 out
	void JSIF_Processor::Fir_x4_dn(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		Vst::Sample64 inter_42[2];
		hb_push_dn(dnSample_42[channel], in[0], in[1]);
		HB_dnsample(&dnSample_42[channel], &inter_42[0]);

		hb_push_dn(dnSample_42[channel], in[2], in[3]);
		HB_dnsample(&dnSample_42[channel], &inter_42[1]);

		hb_push_dn(dnSample_41[channel], inter_42[0], inter_42[1]);
		HB_dnsample(&dnSample_41[channel], out);

		return;
	}
	// 8 in 1 out
	void JSIF_Processor::Fir_x8_dn(Vst::Sample64* in, Vst::Sample64* out, int32 channel)
	{
		Vst::Sample64 inter_83[4];
		hb_push_dn(dnSample_83[channel], in[0], in[1]);
		HB_dnsample(&dnSample_83[channel], &inter_83[0]);

		hb_push_dn(dnSample_83[channel], in[2], in[3]);
		HB_dnsample(&dnSample_83[channel], &inter_83[1]);

		hb_push_dn(dnSample_83[channel], in[4], in[5]);
		HB_dnsample(&dnSample_83[channel], &inter_83[2]);

		hb_push_dn(dnSample_83[channel], in[6], in[7]);
		HB_dnsample(&dnSample_83[channel], &inter_83[3]);

		Vst::Sample64 inter_82[2];
		hb_push_dn(dnSample_82[channel], inter_83[0], inter_83[1]);
		HB_dnsample(&dnSample_82[channel], &inter_82[0]);

		hb_push_dn(dnSample_82[channel], inter_83[2], inter_83[3]);
		HB_dnsample(&dnSample_82[channel], &inter_82[1]);

		hb_push_dn(dnSample_81[channel], inter_82[0], inter_82[1]);
		HB_dnsample(&dnSample_81[channel], out);

		return;
	}

	// Generic min-phase half-band interpolation: 1 in -> (1<<nStages) out.
	void JSIF_Processor::Fir_up_n(Vst::Sample64* in, Vst::Sample64* out, int32 channel, int32 nStages)
	{
	    Vst::Sample64 a[256], b[256];
	    a[0] = *in;
	    int32 count = 1;
	    Vst::Sample64* src = a; Vst::Sample64* dst = b;
	    for (int32 k = 0; k < nStages; k++)
	    {
	        Flt& f = upStage[k][channel];
	        for (int32 i = 0; i < count; i++)
	        {
	            hb_push_up(f, src[i]);
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
	    Vst::Sample64 a[256], b[256];
	    int32 count = 1 << nStages;
	    memcpy(a, in, sizeof(Vst::Sample64) * count);
	    Vst::Sample64* src = a; Vst::Sample64* dst = b;
	    for (int32 k = nStages - 1; k >= 0; k--)   // highest-rate stage first
	    {
	        Flt& f = dnStage[k][channel];
	        const int32 outCount = count / 2;
	        for (int32 i = 0; i < outCount; i++)
	        {
	            hb_push_dn(f, src[2 * i], src[2 * i + 1]);
	            HB_dnsample(&f, &dst[i]);
	        }
	        count = outCount;
	        Vst::Sample64* t = src; src = dst; dst = t;
	    }
	    *out = src[0];
	}

} // namespace yg331
