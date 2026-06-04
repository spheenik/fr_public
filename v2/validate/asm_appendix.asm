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
MIXTAP_SNAP_ROUTINE premix
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
    ; six per-channel-chain sub-stage accumulators (stereo)
    mov   edi, chtap_dcf1
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, chtap_comp
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, chtap_boost
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, chtap_dist
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, chtap_chorus
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
    rep   stosd
    mov   edi, chtap_dcf2
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
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

; pre-reverb dry-mix accessor (mirrors synthDebugGetPreMix in synth_core.cpp).
global _synthDebugGetPreMix@12
_synthDebugGetPreMix@12:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+12]         ; **premix
    mov   dword [eax], mixtap_premix
    mov   eax, [ebp+16]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   12

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

; --- per-channel-chain SUB-STAGE ACCUMULATORS (after each chain block) ---
; Mirror of synth_core.cpp's g_chtap_* / CHTAP_SNAP. build.sh sed-injects a
; `call chtap_snap_<stage>` after each channel-chain render call in syChanProcess
; (anchored on the unique `lea ebp,[...]` that follows each call). Each routine
; ADDS the live chanbuf (STEREO, 2*SRcFrameSize floats) into its accumulator;
; reset once per frame by chantap_reset. FPU add at ambient PC=24; the chain
; render routines return with an empty x87 stack, and pushad/popad preserves all
; GP regs (incl. ebx=chanbuf base and ebp=workspace, both live across the chain).
%macro CHTAP_SNAP_ROUTINE 1
chtap_snap_%1:
    pushad
    mov   esi, [this]
    lea   esi, [esi + SYN.chanbuf]
    mov   edi, chtap_%1
    mov   ecx, [SRcFrameSize]
    add   ecx, ecx
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
CHTAP_SNAP_ROUTINE dcf1
CHTAP_SNAP_ROUTINE comp
CHTAP_SNAP_ROUTINE boost
CHTAP_SNAP_ROUTINE dist
CHTAP_SNAP_ROUTINE chorus
CHTAP_SNAP_ROUTINE dcf2

; --- chain sub-stage tap accessor: returns the six snapshot ptrs + frame size ---
; Mirrors synthDebugGetChainTap in synth_core.cpp.
; stdcall(pthis,**pd1,**pc,**pb,**pdi,**pch,**pd2,*fs) = 8 dword args = 32 bytes.
; The redef step renames _synthDebugGetChainTap@32 -> synthDebugGetChainTap.
global _synthDebugGetChainTap@32
_synthDebugGetChainTap@32:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+12]         ; **postDcf1
    mov   dword [eax], chtap_dcf1
    mov   eax, [ebp+16]         ; **postComp
    mov   dword [eax], chtap_comp
    mov   eax, [ebp+20]         ; **postBoost
    mov   dword [eax], chtap_boost
    mov   eax, [ebp+24]         ; **postDist
    mov   dword [eax], chtap_dist
    mov   eax, [ebp+28]         ; **postChorus
    mov   dword [eax], chtap_chorus
    mov   eax, [ebp+32]         ; **postDcf2
    mov   dword [eax], chtap_dcf2
    mov   eax, [ebp+36]         ; *framesize
    mov   ecx, [SRcFrameSize]
    mov   [eax], ecx
    pop   ebp
    ret   32

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

; sed-injected `call reverbdbg_snap` at the end of syReverbSet (ebp = syWReverb
; base, FPU stack empty). Forwards the 9 setup coeffs (raw bits) to reverbdbg_c.
extern reverbdbg_c
reverbdbg_snap:
    pushad
    pushfd
    push  dword [ebp + syWReverb.setup + syCReverb.lowcut]
    push  dword [ebp + syWReverb.setup + syCReverb.gainin]
    push  dword [ebp + syWReverb.setup + syCReverb.damp]
    push  dword [ebp + syWReverb.setup + syCReverb.gaina1]
    push  dword [ebp + syWReverb.setup + syCReverb.gaina0]
    push  dword [ebp + syWReverb.setup + syCReverb.gainc3]
    push  dword [ebp + syWReverb.setup + syCReverb.gainc2]
    push  dword [ebp + syWReverb.setup + syCReverb.gainc1]
    push  dword [ebp + syWReverb.setup + syCReverb.gainc0]
    call  reverbdbg_c
    add   esp, 36
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

; --- freq-divergence ledger (validation; mirrors C++ freqlog_emit) -----------
; sed-injected `call freqlog_emit_osc` after `fistp [ebp+syWOsc.freq]` in
; syOscChgPitch, and `call freqlog_emit_lfo` after `fistp [ebp+syWLFO.freq]` in
; syLFOSet. At each site ebp = the osc/LFO struct base, the freq is already stored,
; and the FPU stack is empty. Each routine appends one 32-byte FreqLogRec (compat.h:
; kind, offset, freq, pno, preround, r0,r1,r2) to freqlog_buf. Pure integer copy
; under pushad/popad -- preserves all GP regs, touches no FPU state, writes only to
; its own buffer, so it is inert w.r.t. the synth's DSP output. Drained per render
; by _synthDebugGetFreqLog@8 (read-and-clear). `this` = SYN base (synth_noronan.asm).
FREQLOG_CAP equ 16384

freqlog_emit_osc:
    pushad
    mov   eax, [freqlog_count]
    cmp   eax, FREQLOG_CAP
    jae   .full
    mov   edx, eax
    shl   edx, 5                    ; record size = 32 bytes
    lea   edi, [freqlog_buf + edx]
    mov   dword [edi + 0], 0        ; kind = osc
    mov   esi, ebp
    sub   esi, [this]
    mov   [edi + 4], esi            ; offset within instance
    mov   ecx, [ebp + syWOsc.freq]
    mov   [edi + 8], ecx            ; freq (post-fistp bits)
    mov   ecx, [ebp + syWOsc.pitch]
    mov   [edi + 12], ecx           ; pno = pitch bits (upstream input)
    mov   ecx, [ebp + syWOsc.nffrq]
    mov   [edi + 16], ecx           ; preround = noise-filter freq bits
    mov   ecx, [ebp + syWOsc.cnt]
    mov   [edi + 20], ecx           ; r0 = osc phase accumulator
    mov   ecx, [ebp + syWOsc.mode]
    mov   [edi + 24], ecx           ; r1 = osc mode
    mov   ecx, [ebp + syWOsc.nfb]
    mov   [edi + 28], ecx           ; r2 = noise-filter band state nf.b
    inc   eax
    mov   [freqlog_count], eax
.full:
    popad
    ret

freqlog_emit_lfo:
    pushad
    mov   eax, [freqlog_count]
    cmp   eax, FREQLOG_CAP
    jae   .full
    mov   edx, eax
    shl   edx, 5
    lea   edi, [freqlog_buf + edx]
    mov   dword [edi + 0], 1        ; kind = lfo
    mov   esi, ebp
    sub   esi, [this]
    mov   [edi + 4], esi
    mov   ecx, [ebp + syWLFO.freq]
    mov   [edi + 8], ecx
    xor   ecx, ecx
    mov   [edi + 12], ecx
    mov   [edi + 16], ecx
    mov   [edi + 20], ecx
    mov   [edi + 24], ecx
    mov   [edi + 28], ecx
    inc   eax
    mov   [freqlog_count], eax
.full:
    popad
    ret

; --- control-source ledger emit (validation; mirrors C++ ctrllog_emit) ---------
; sed-injected `call ctrllog_emit` in syV2Tick after the LFOs, where ebp = the voice
; (syWV2) base and the FPU stack is empty. Captures the four accumulating modulation
; sources (aenv/env2 out, lfo1/lfo2 out) for this voice this tick. kind=2. Same
; pushad/popad integer-copy safety as freqlog_emit.
ctrllog_emit:
    pushad
    mov   eax, [ctrllog_count]
    cmp   eax, FREQLOG_CAP
    jae   .full
    mov   edx, eax
    shl   edx, 5
    lea   edi, [ctrllog_buf + edx]
    mov   dword [edi + 0], 2        ; kind = voice control
    mov   esi, ebp
    sub   esi, [this]
    mov   [edi + 4], esi            ; voice offset within instance
    mov   ecx, [ebp + syWV2.aenv + syWEnv.out]
    mov   [edi + 8], ecx            ; freq slot = aenv.out
    mov   ecx, [ebp + syWV2.env2 + syWEnv.out]
    mov   [edi + 12], ecx           ; pno slot  = env2.out
    mov   ecx, [ebp + syWV2.lfo1 + syWLFO.out]
    mov   [edi + 16], ecx           ; preround slot = lfo1.out
    mov   ecx, [ebp + syWV2.lfo2 + syWLFO.out]
    mov   [edi + 20], ecx           ; r0 slot = lfo2.out
    mov   ecx, [ebp + syWV2.env2 + syWEnv.suf]
    mov   [edi + 24], ecx           ; r1 slot = env2.suf bits
    ; r2 = aenv.state | (env2.state << 8)
    movzx ecx, byte [ebp + syWV2.aenv + syWEnv.state]
    movzx esi, byte [ebp + syWV2.env2 + syWEnv.state]
    shl   esi, 8
    or    ecx, esi
    mov   [edi + 28], ecx
    inc   eax
    mov   [ctrllog_count], eax
.full:
    popad
    ret

; --- filter ledger emit (validation; mirrors C++ V2Flt::render fltlog) ---
; sed-injected `call fltlog_emit` in syFltRender after the regular-filter state store
; (b then l), where ebp = the filter workspace base. Captures the post-block IIR
; STATE (l, b) + coeffs (cfreq, res). kind=3. pushad/popad integer copy, no FPU touch
; (the FPU still holds <f> <r> here, untouched).
fltlog_emit:
    pushad
    mov   eax, [fltlog_count]
    cmp   eax, FREQLOG_CAP
    jae   .full
    mov   edx, eax
    shl   edx, 5
    lea   edi, [fltlog_buf + edx]
    mov   dword [edi + 0], 3        ; kind = filter
    mov   ecx, ebp
    sub   ecx, [this]
    mov   [edi + 4], ecx            ; filter offset within instance
    mov   ecx, [ebp + syWFlt.l]
    mov   [edi + 8], ecx            ; lrc.l (post-block state)
    mov   ecx, [ebp + syWFlt.b]
    mov   [edi + 12], ecx           ; lrc.b
    mov   ecx, [ebp + syWFlt.cfreq]
    mov   [edi + 16], ecx           ; cfreq
    mov   ecx, [ebp + syWFlt.res]
    mov   [edi + 20], ecx           ; res
    mov   ecx, [g_fltin]
    mov   [edi + 24], ecx           ; r1 = first input sample (saved by fltlog_capin)
    mov   ecx, [ebp + syWFlt.mode]
    and   ecx, 7
    mov   [edi + 28], ecx           ; r2 = filter mode
    inc   eax
    mov   [fltlog_count], eax
.full:
    popad
    ret

; --- osc-output ledger emit (validation; mirrors C++ V2Voice::render osclog) ---
; sed-injected `call osclog_emit` in syV2Render after the three oscs render, where
; ebp = the voice (syWV2) base and ebx = the voice's vcebuf. Captures the first three
; osc-output samples (before any filter). kind=4.
osclog_emit:
    pushad
    mov   eax, [osclog_count]
    cmp   eax, FREQLOG_CAP
    jae   .full
    mov   edx, eax
    shl   edx, 5
    lea   edi, [osclog_buf + edx]
    mov   dword [edi + 0], 4        ; kind = osc output
    mov   ecx, ebp
    sub   ecx, [this]
    mov   [edi + 4], ecx            ; voice offset within instance
    mov   ecx, [ebx]
    mov   [edi + 8], ecx            ; voice[0]
    mov   ecx, [ebx + 4]
    mov   [edi + 12], ecx           ; voice[1]
    mov   ecx, [ebx + 8]
    mov   [edi + 16], ecx           ; voice[2]
    mov   ecx, [ebp + syWV2.f1gain]
    mov   [edi + 20], ecx           ; r0 = parallel combine gain 1
    mov   ecx, [ebp + syWV2.f2gain]
    mov   [edi + 24], ecx           ; r1 = parallel combine gain 2
    mov   ecx, [ebp + syWV2.fmode]
    mov   [edi + 28], ecx           ; r2 = filter routing mode
    inc   eax
    mov   [osclog_count], eax
.full:
    popad
    ret

global _synthDebugGetOscLog@8
_synthDebugGetOscLog@8:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+8]
    mov   dword [eax], osclog_buf
    mov   eax, [ebp+12]
    mov   ecx, [osclog_count]
    mov   [eax], ecx
    mov   dword [osclog_count], 0
    pop   ebp
    ret   8

; sed-injected `call fltlog_capin` at the start of syFltRender's regular path, where
; esi = the source buffer. Saves the first input sample for the end-of-block emit.
; Preserves eax (= the mode jump index used right after) and all other regs.
fltlog_capin:
    push  eax
    mov   eax, [esi]
    mov   [g_fltin], eax
    pop   eax
    ret

global _synthDebugGetFltLog@8
_synthDebugGetFltLog@8:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+8]
    mov   dword [eax], fltlog_buf
    mov   eax, [ebp+12]
    mov   ecx, [fltlog_count]
    mov   [eax], ecx
    mov   dword [fltlog_count], 0
    pop   ebp
    ret   8

; --- control-source ledger accessor (read-and-clear) ---
global _synthDebugGetCtrlLog@8
_synthDebugGetCtrlLog@8:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+8]
    mov   dword [eax], ctrllog_buf
    mov   eax, [ebp+12]
    mov   ecx, [ctrllog_count]
    mov   [eax], ecx
    mov   dword [ctrllog_count], 0
    pop   ebp
    ret   8

; --- ledger accessor: returns buffer ptr + record count, then clears the count ---
; Mirrors synthDebugGetFreqLog in synth_core.cpp. stdcall(recs**, count*) = 8 bytes.
; The redef step renames _synthDebugGetFreqLog@8 -> synthDebugGetFreqLog.
global _synthDebugGetFreqLog@8
_synthDebugGetFreqLog@8:
    push  ebp
    mov   ebp, esp
    mov   eax, [ebp+8]              ; recs**
    mov   dword [eax], freqlog_buf
    mov   eax, [ebp+12]            ; count*
    mov   ecx, [freqlog_count]
    mov   [eax], ecx
    mov   dword [freqlog_count], 0 ; read-and-clear drain
    pop   ebp
    ret   8

; --- knockout arm: no-op on the asm oracle (it is never overridden) -------------
; Mirrors synthDebugArmFreqKnockout in synth_core.cpp so the shared player links
; against harness_asm. stdcall(recs, count) = 8 bytes.
global _synthDebugArmFreqKnockout@8
_synthDebugArmFreqKnockout@8:
    ret   8

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
mixtap_premix:  resd 2*MAX_FRAME_SIZE
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
; per-channel-chain sub-stage snapshots (stereo, one frame each)
chtap_dcf1:     resd 2*MAX_FRAME_SIZE
chtap_comp:     resd 2*MAX_FRAME_SIZE
chtap_boost:    resd 2*MAX_FRAME_SIZE
chtap_dist:     resd 2*MAX_FRAME_SIZE
chtap_chorus:   resd 2*MAX_FRAME_SIZE
chtap_dcf2:     resd 2*MAX_FRAME_SIZE
; freq-divergence ledger: FREQLOG_CAP records x 8 dwords (32 bytes), drained per
; render. Mirrors g_freqlog_buf/g_freqlog_count in synth_core.cpp.
freqlog_buf:    resd 8*FREQLOG_CAP
freqlog_count:  resd 1
; control-source ledger: voice modulation-source outputs, one record per voice/tick.
ctrllog_buf:    resd 8*FREQLOG_CAP
ctrllog_count:  resd 1
; filter ledger: cutoff/reso/mode input per V2Flt::set call.
fltlog_buf:     resd 8*FREQLOG_CAP
fltlog_count:   resd 1
g_fltin:        resd 1   ; first input sample of the current filter block
osclog_buf:     resd 8*FREQLOG_CAP
osclog_count:   resd 1
