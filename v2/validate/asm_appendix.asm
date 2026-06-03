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
; --- fast sine (used by LFO/osc sin); st0 in -> st0 out ---
global fastsin
global fastsinrc
; --- pow2 (2^x); st0 in -> st0 out ---
global pow2

; --- bus-tap accessor: returns live aux1/aux2/mix buffer ptrs + frame size ---
; Mirrors synthDebugGetBus in synth_core.cpp. stdcall(pthis,**a1,**a2,**mix,*fs).
; The redef step renames _synthDebugGetBus@20 -> synthDebugGetBus for the harness.
global _synthDebugGetBus@20
_synthDebugGetBus@20:
    push  ebp
    mov   ebp, esp
    mov   edx, [ebp+8]              ; edx = SYN base (pthis)
    mov   eax, [ebp+12]            ; **a1
    lea   ecx, [edx + SYN.aux1buf]
    mov   [eax], ecx
    mov   eax, [ebp+16]           ; **a2
    lea   ecx, [edx + SYN.aux2buf]
    mov   [eax], ecx
    mov   eax, [ebp+20]          ; **mix
    lea   ecx, [edx + SYN.mixbuf]
    mov   [eax], ecx
    mov   eax, [ebp+24]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   20

; --- per-stage mix-chain snapshot tap (validation localization) ---
; Mirror of synth_core.cpp's g_mixtap_* / MIXTAP_SNAP. build.sh sed-injects a
; `call mixtap_snap_<stage>` after each global FX stage in the render frame; each
; routine copies the live mixbuf (2*SRcFrameSize dwords, interleaved stereo) into
; its snapshot buffer. Pure integer copy: pushad/popad preserves all GP regs and
; touches no FPU state, so it is safe to splice between stages regardless of the
; render frame's register/FPU state. `this` holds the SYN base (synth_noronan.asm).
%macro MIXTAP_SNAP_ROUTINE 1
mixtap_snap_%1:
    pushad
    cld
    mov   esi, [this]
    lea   esi, [esi + SYN.mixbuf]
    mov   edi, mixtap_%1
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx                 ; 2*framesize dwords (stereo interleaved)
    rep   movsd
    popad
    ret
%endmacro
MIXTAP_SNAP_ROUTINE reverb
MIXTAP_SNAP_ROUTINE delay
MIXTAP_SNAP_ROUTINE dcf
MIXTAP_SNAP_ROUTINE lchc
MIXTAP_SNAP_ROUTINE compr

; --- per-stage tap accessor: returns the five snapshot buffer ptrs + frame size ---
; Mirrors synthDebugGetMixTap in synth_core.cpp.
; stdcall(pthis,**pr,**pd,**pf,**plh,**pc,*fs) = 7 dword args = 28 bytes.
; The redef step renames _synthDebugGetMixTap@28 -> synthDebugGetMixTap.
global _synthDebugGetMixTap@28
_synthDebugGetMixTap@28:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+12]         ; **postReverb
    mov   dword [eax], mixtap_reverb
    mov   eax, [ebp+16]         ; **postDelay
    mov   dword [eax], mixtap_delay
    mov   eax, [ebp+20]         ; **postDcf
    mov   dword [eax], mixtap_dcf
    mov   eax, [ebp+24]         ; **postLcHc
    mov   dword [eax], mixtap_lchc
    mov   eax, [ebp+28]         ; **postCompr
    mov   dword [eax], mixtap_compr
    mov   eax, [ebp+32]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   28

; --- per-voice-substage snapshot tap (validation localization, level 2) ---
; Mirror of synth_core.cpp's g_vcetap_* / VCETAP_SNAP. build.sh sed-injects a
; `call vcetap_snap_<stage>` after each voice sub-stage in syV2Render. vcebuf is
; MONO, so each routine copies SRcFrameSize dwords. Same safety as mixtap_snap_*:
; pure integer copy, pushad/popad preserves all GP regs (incl. ebx=vcebuf base and
; ebp=workspace, both live across syV2Render), touches no FPU state.
; ACCUMULATE each voice's vcebuf (mono) into the per-substage tap (reset once per
; frame by chantap_reset). Unmasked across all voices. FPU add at ambient PC=24.
%macro VCETAP_SNAP_ROUTINE 1
vcetap_snap_%1:
    pushad
    mov   esi, [this]
    lea   esi, [esi + SYN.vcebuf]
    mov   edi, vcetap_%1
    mov   ecx, [SRcFrameSize]      ; mono: SRcFrameSize dwords
%%acc:
    fld   dword [esi]
    fadd  dword [edi]
    fstp  dword [edi]
    add   esi, 4
    add   edi, 4
    dec   ecx
    jnz   %%acc
    popad
    ret
%endmacro
VCETAP_SNAP_ROUTINE osc
VCETAP_SNAP_ROUTINE flt
VCETAP_SNAP_ROUTINE dist
VCETAP_SNAP_ROUTINE dcf

; --- voice-substage tap accessor ---
; Mirrors synthDebugGetVceTap in synth_core.cpp.
; stdcall(pthis,**po,**pf,**pd,**pc,*fs) = 6 dword args = 24 bytes.
; The redef step renames _synthDebugGetVceTap@24 -> synthDebugGetVceTap.
global _synthDebugGetVceTap@24
_synthDebugGetVceTap@24:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+12]         ; **postOsc
    mov   dword [eax], vcetap_osc
    mov   eax, [ebp+16]         ; **postFlt
    mov   dword [eax], vcetap_flt
    mov   eax, [ebp+20]         ; **postDist
    mov   dword [eax], vcetap_dist
    mov   eax, [ebp+24]         ; **postDcf
    mov   dword [eax], vcetap_dcf
    mov   eax, [ebp+28]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   24

; --- channel voice-sum ACCUMULATOR (post-curvol, after each voice loop) ---
; Mirror of synth_core.cpp's g_chantap. chantap_reset zeroes it once per frame
; (before the channel loop); chantap_snap ADDS each channel's chanbuf into it
; (before that channel's channel-chain), so the result is the UNMASKED total
; voice contribution across all channels. chanbuf is STEREO (2*SRcFrameSize
; floats). FPU add at ambient PC=24 (stack is empty between the voice loop and
; syChanProcess; pushad/popad preserves GP regs).
chantap_reset:
    pushad
    cld
    xor   eax, eax
    mov   edi, chantap            ; stereo
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, vcetap_osc         ; four mono vce accumulators
    mov   ecx, [SRcFrameSize]
    rep   stosd
    mov   edi, vcetap_flt
    mov   ecx, [SRcFrameSize]
    rep   stosd
    mov   edi, vcetap_dist
    mov   ecx, [SRcFrameSize]
    rep   stosd
    mov   edi, vcetap_dcf
    mov   ecx, [SRcFrameSize]
    rep   stosd
    popad
    ret
chantap_snap:
    pushad
    mov   esi, [this]
    lea   esi, [esi + SYN.chanbuf]
    mov   edi, chantap
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
.acc:
    fld   dword [esi]
    fadd  dword [edi]
    fstp  dword [edi]
    add   esi, 4
    add   edi, 4
    dec   ecx
    jnz   .acc
    popad
    ret

global _synthDebugGetChanTap@12
_synthDebugGetChanTap@12:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+12]         ; **chan
    mov   dword [eax], chantap
    mov   eax, [ebp+16]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   12

; --- asm-side chorus-integer dump (validation; mirrors C++ CHORUSTRACE) ---
; sed-injected `call chorusdbg_snap` at the end of syModDelSet, where ebp = the
; syWModDel struct base and the FPU stack is empty. Forwards the just-computed
; integers to chorusdbg_c (cdecl, asm_stubs.cpp). Preserves all GP regs + flags.
extern chorusdbg_c
chorusdbg_snap:
    pushad
    pushfd
    push  dword [ebp + syWModDel.mphase]
    push  dword [ebp + syWModDel.mmaxoffs]
    push  dword [ebp + syWModDel.db2offs]
    push  dword [ebp + syWModDel.db1offs]
    push  dword [ebp + syWModDel.mfreq]
    call  chorusdbg_c
    add   esp, 20
    popfd
    popad
    ret

; sed-injected `call comptrace_snap` at the end of syCompSet (ebp = syWComp base,
; FPU stack empty). Forwards mode/dblen + the 5 coeff floats (as raw bits) to
; compdbg_c (cdecl). Preserves all GP regs + flags.
extern compdbg_c
comptrace_snap:
    pushad
    pushfd
    push  dword [ebp + syWComp.release]
    push  dword [ebp + syWComp.attack]
    push  dword [ebp + syWComp.ratio]
    push  dword [ebp + syWComp.outvol]
    push  dword [ebp + syWComp.invol]
    push  dword [ebp + syWComp.dblen]
    push  dword [ebp + syWComp.mode]
    call  compdbg_c
    add   esp, 28
    popfd
    popad
    ret

; sed-injected `call boostdbg_snap` at the end of syBoostSet (ebp = syWBoost base,
; FPU stack empty). Forwards ena + the 5 biquad coeffs (raw bits) to boostdbg_c.
extern boostdbg_c
boostdbg_snap:
    pushad
    pushfd
    push  dword [ebp + syWBoost.a2]
    push  dword [ebp + syWBoost.a1]
    push  dword [ebp + syWBoost.b2]
    push  dword [ebp + syWBoost.b1]
    push  dword [ebp + syWBoost.b0]
    push  dword [ebp + syWBoost.ena]
    call  boostdbg_c
    add   esp, 24
    popfd
    popad
    ret

; sed-injected `call envdbg_snap` at the end of syEnvSet (ebp = syWEnv base,
; FPU stack empty). Forwards the 6 env coeffs (raw bits) to envdbg_c.
; sed-injected `call allocdbg_snap` at ProcessNoteOn.donoteon (ecx=chan, edx=slot,
; esi->note/vel). Forwards them to allocdbg_c (cdecl). Preserves all regs/flags.
extern allocdbg_c
allocdbg_snap:
    pushad
    pushfd
    movzx eax, byte [esi+1]
    push  eax              ; vel
    movzx eax, byte [esi]
    push  eax              ; note
    push  ecx              ; chan
    push  edx              ; slot
    call  allocdbg_c
    add   esp, 16
    popfd
    popad
    ret

extern envdbg_c
envdbg_snap:
    pushad
    pushfd
    push  dword [ebp + syWEnv.gain]
    push  dword [ebp + syWEnv.ref]
    push  dword [ebp + syWEnv.suf]
    push  dword [ebp + syWEnv.sul]
    push  dword [ebp + syWEnv.dcf]
    push  dword [ebp + syWEnv.atd]
    call  envdbg_c
    add   esp, 24
    popfd
    popad
    ret

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
global v2x_off_syWOsc_freq
v2x_off_syWOsc_freq: dd syWOsc.freq
global v2x_off_syWOsc_brpt
v2x_off_syWOsc_brpt: dd syWOsc.brpt
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

; --- per-stage mix-chain snapshot buffers (validation localization tap) ---
; One interleaved-stereo frame each (2*MAX_FRAME_SIZE dwords), filled by the
; mixtap_snap_* routines above and read out via _synthDebugGetMixTap@28.
section .bss
mixtap_reverb:  resd 2*MAX_FRAME_SIZE
mixtap_delay:   resd 2*MAX_FRAME_SIZE
mixtap_dcf:     resd 2*MAX_FRAME_SIZE
mixtap_lchc:    resd 2*MAX_FRAME_SIZE
mixtap_compr:   resd 2*MAX_FRAME_SIZE
; voice-substage snapshots (mono, one frame each)
vcetap_osc:     resd MAX_FRAME_SIZE
vcetap_flt:     resd MAX_FRAME_SIZE
vcetap_dist:    resd MAX_FRAME_SIZE
vcetap_dcf:     resd MAX_FRAME_SIZE
; channel voice-sum snapshot (stereo, one frame)
chantap:        resd 2*MAX_FRAME_SIZE
