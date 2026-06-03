; ============================================================================
; Validation appendix — appended to the generated synth_noronan.asm before
; assembly. Exposes internal block routines, the sample-rate setup, the SR
; global constants, and struct sizes/offsets as global symbols so the
; per-component equivalence harness can drive blocks in isolation.
; The ORIGINAL synth.asm is never edited; this is concatenated onto the
; generated copy, where all struc constants and labels are already in scope.
; ============================================================================

; --- sample-rate setup + the global SR constants it writes ---
global calcNewSampleRate
global SRfcobasefrq
global SRfclinfreq
global SRfcsamplesperms
global SRfcBoostCos
global SRfcBoostSin
global SRfcdcfilter

; --- oscillator block routines ---
global syOscInit
global syOscSet
global syOscRender

; --- filter (VCF) block routines ---
global syFltInit
global syFltSet
global syFltRender

; --- envelope ---
global syEnvInit
global syEnvSet
global syEnvTick
; --- LFO ---
global syLFOInit
global syLFOSet
global syLFOKeyOn
global syLFOTick
; --- distortion ---
global syDistInit
global syDistSet
global syDistRenderMono
; --- DC filter (no params) ---
global syDCFInit
global syDCFRenderMono
; --- bass/boost ---
global syBoostInit
global syBoostSet
global syBoostRender
; --- fastatan (used by distortion overdrive) ---
global fastatan

; --- struct sizes / field offsets the harness needs (as data) ---
section .data
global v2x_size_syWOsc
v2x_size_syWOsc:    dd syWOsc.size
global v2x_size_syVOsc
v2x_size_syVOsc:    dd syVOsc.size
global v2x_off_syWOsc_note
v2x_off_syWOsc_note: dd syWOsc.note
global v2x_off_syWOsc_pitch
v2x_off_syWOsc_pitch: dd syWOsc.pitch
global v2x_size_syWFlt
v2x_size_syWFlt:    dd syWFlt.size
global v2x_size_syVFlt
v2x_size_syVFlt:    dd syVFlt.size
global v2x_size_syWEnv
v2x_size_syWEnv:    dd syWEnv.size
global v2x_off_syWEnv_state
v2x_off_syWEnv_state: dd syWEnv.state
global v2x_off_syWEnv_out
v2x_off_syWEnv_out:   dd syWEnv.out
global v2x_size_syWLFO
v2x_size_syWLFO:    dd syWLFO.size
global v2x_off_syWLFO_out
v2x_off_syWLFO_out:   dd syWLFO.out
global v2x_size_syWDist
v2x_size_syWDist:   dd syWDist.size
global v2x_size_syWDCF
v2x_size_syWDCF:    dd syWDCF.size
global v2x_size_syWBoost
v2x_size_syWBoost:  dd syWBoost.size
