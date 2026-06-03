; ============================================================================
; Register-ABI trampolines: cdecl C wrappers around the asm block routines,
; which use a register convention (ebp = workspace ptr, esi/edi = buffers,
; ecx = count/index). The called routines are pushad/popad-wrapped, so they
; preserve all registers; we only save/restore the C callee-saved regs we set
; (ebp, esi, edi). ebx is untouched.
; ============================================================================
bits 32
section .text

extern calcNewSampleRate
extern syOscInit
extern syOscSet
extern syOscRender
extern syFltInit
extern syFltSet
extern syFltRender
extern syEnvInit
extern syEnvSet
extern syEnvTick
extern syLFOInit
extern syLFOSet
extern syLFOKeyOn
extern syLFOTick
extern syDistInit
extern syDistSet
extern syDistRenderMono
extern syDCFInit
extern syDCFRenderMono
extern syBoostInit
extern syBoostSet
extern syBoostRender
extern fastatan

; --- helper macros: wrap a register-ABI routine as a cdecl function ---
; W1: routine taking ebp=arg0
%macro TRAMP_W1 2     ; %1=cname %2=routine
global %1
%1:
    push ebp
    push esi
    push edi
    mov  ebp, [esp+16]
    call %2
    pop edi
    pop esi
    pop ebp
    ret
%endmacro
; W2: routine taking ebp=arg0, esi=arg1
%macro TRAMP_W2 2     ; %1=cname %2=routine
global %1
%1:
    push ebp
    push esi
    push edi
    mov  ebp, [esp+16]
    mov  esi, [esp+20]
    call %2
    pop edi
    pop esi
    pop ebp
    ret
%endmacro
; RENDER: ebp=arg0(W), edi=arg1(dst), esi=arg2(src), ecx=arg3(n)
%macro TRAMP_RENDER 2
global %1
%1:
    push ebp
    push esi
    push edi
    mov  ebp, [esp+16]
    mov  edi, [esp+20]
    mov  esi, [esp+24]
    mov  ecx, [esp+28]
    call %2
    pop edi
    pop esi
    pop ebp
    ret
%endmacro

TRAMP_W1 v2x_envInit, syEnvInit
TRAMP_W2 v2x_envSet,  syEnvSet
TRAMP_W1 v2x_envTick, syEnvTick
TRAMP_W1 v2x_lfoInit, syLFOInit
TRAMP_W2 v2x_lfoSet,  syLFOSet
TRAMP_W1 v2x_lfoKeyOn, syLFOKeyOn
TRAMP_W1 v2x_lfoTick, syLFOTick
TRAMP_W1 v2x_distInit, syDistInit
TRAMP_W2 v2x_distSet,  syDistSet
TRAMP_RENDER v2x_distRenderMono, syDistRenderMono
TRAMP_W1 v2x_dcfInit,  syDCFInit
TRAMP_RENDER v2x_dcfRenderMono, syDCFRenderMono
TRAMP_W1 v2x_boostInit, syBoostInit
TRAMP_W2 v2x_boostSet,  syBoostSet

; boost render is in-place stereo: ebp=W, esi=buf, ecx=n
global v2x_boostRender
v2x_boostRender:
    push ebp
    push esi
    push edi
    mov  ebp, [esp+16]
    mov  esi, [esp+20]
    mov  ecx, [esp+24]
    call syBoostRender
    pop edi
    pop esi
    pop ebp
    ret

; void v2x_calcSR(int samplerate)   -- eax = sr
global v2x_calcSR
v2x_calcSR:
    mov     eax, [esp+4]
    call    calcNewSampleRate
    ret

; void v2x_oscInit(void *W, int idx)   -- ebp=W, ecx=idx
global v2x_oscInit
v2x_oscInit:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W   (arg0; +16 = +4 ret/args base after 3 pushes)
    mov     ecx, [esp+20]      ; idx (arg1)
    call    syOscInit
    pop     edi
    pop     esi
    pop     ebp
    ret

; void v2x_oscSet(void *W, const void *V)   -- ebp=W, esi=&syVOsc
global v2x_oscSet
v2x_oscSet:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W
    mov     esi, [esp+20]      ; V
    call    syOscSet
    pop     edi
    pop     esi
    pop     ebp
    ret

; void v2x_oscRender(void *W, float *dst, const float *src, int n)
;   ebp=W, edi=dst, esi=src, ecx=n
global v2x_oscRender
v2x_oscRender:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W
    mov     edi, [esp+20]      ; dst
    mov     esi, [esp+24]      ; src
    mov     ecx, [esp+28]      ; n
    call    syOscRender
    pop     edi
    pop     esi
    pop     ebp
    ret

; void v2x_fltInit(void *W)   -- ebp=W
global v2x_fltInit
v2x_fltInit:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W
    call    syFltInit
    pop     edi
    pop     esi
    pop     ebp
    ret

; void v2x_fltSet(void *W, const void *V)   -- ebp=W, esi=&syVFlt
global v2x_fltSet
v2x_fltSet:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W
    mov     esi, [esp+20]      ; V
    call    syFltSet
    pop     edi
    pop     esi
    pop     ebp
    ret

; void v2x_fltRender(void *W, float *dst, const float *src, int n)
;   ebp=W, edi=dst, esi=src, ecx=n
global v2x_fltRender
v2x_fltRender:
    push    ebp
    push    esi
    push    edi
    mov     ebp, [esp+16]      ; W
    mov     edi, [esp+20]      ; dst
    mov     esi, [esp+24]      ; src
    mov     ecx, [esp+28]      ; n
    call    syFltRender
    pop     edi
    pop     esi
    pop     ebp
    ret

; float v2x_fastatan(float x)
; ASM fastatan ABI: st0=value, st1=-1, ax=high 16 bits of value, ebx=8.
; Returns result in st0 (cdecl float return).
global v2x_fastatan
v2x_fastatan:
    push ebx
    fld1
    fchs                 ; st0 = -1
    fld  dword [esp+8]   ; st0 = x, st1 = -1   (arg at esp+8 after push ebx + ret)
    mov  eax, [esp+8]
    shr  eax, 16         ; ax = high 16 bits of x
    mov  ebx, 8
    call fastatan        ; st0 = result, st1 = -1
    fstp st1             ; collapse: st1 := result, pop  -> st0 = result
    pop  ebx
    ret
