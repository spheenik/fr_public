
#ifndef _SYNTH_H_
#define _SYNTH_H_

extern "C"
{
  extern unsigned int __stdcall synthGetSize();
  
	extern void __stdcall synthInit(void *pthis, const void *patchmap, int samplerate=44100);
	extern void __stdcall synthRender(void *pthis, void *buf, int smp, void *buf2=0, int add=0);
	extern void __stdcall synthProcessMIDI(void *pthis, const void *ptr);
	extern void __stdcall synthSetGlobals(void *pthis, const void *ptr);

	// Era compat: tells the core which v2m FORMAT VERSION the song data was
	// originally authored as (0 = year-2000 fr08 era .. v2version = current),
	// BEFORE any v2mconv upgrade. Gates period DSP behaviors (envelope scaling,
	// osc sine/freq, noise LCG, ...) for faithful playback of period files; see
	// v2/v2m/fr08-extraction/DELTA.md. Call after synthInit (init resets it to
	// modern). Optional -- without it the core renders modern (2004) behavior.
	extern void __stdcall synthSetSourceVersion(void *pthis, int srcver);
//  extern void __stdcall synthSetSampler(void *pthis, const void *bankinfo, const void *samples);
	extern void __stdcall synthGetPoly(void *pthis, void *dest);
	extern void __stdcall synthGetPgm(void *pthis, void *dest);
//	extern void __stdcall synthGetLD(void *pthis, float *l, float *r);

	// only if VUMETER define is set in synth source

	// vu output values are floats, 1.0 == 0dB
	// you might want to clip or logarithmize the values for yourself
	extern void __stdcall synthSetVUMode(void *pthis, int mode); // 0: peak, 1: rms
	extern void __stdcall synthGetChannelVU(void *pthis, int ch, float *l, float *r); // ch: 0..15
	extern void __stdcall synthGetMainVU(void *pthis, float *l, float *r);

	extern long __stdcall synthGetFrameSize(void *pthis);

#ifdef RONAN
	extern void __stdcall synthSetLyrics(void *pthis, const char **ptr);
#endif

}

#endif
