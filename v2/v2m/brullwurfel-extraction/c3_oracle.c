// C3 oracle: fr-028 brullwurfel's V2 synth (from the mapped unpacked binary)
// driven by the GENUINE source-code V2M player (ported verbatim from
// genthree/_viruz2.cpp, same port as candytron's c2_oracle.c). Renders a carved
// brullwurfel song (default song1 = the fr08 ".the .product" song re-exported to
// format v5) to f32.
//
// This is the v5-era counterpart of the fr08 C1 (v0) oracle: song1 is the SAME
// music as fr08.v2m, so a v5 render here vs the bit-exact v0 oracle isolates the
// 2002 engine rewrite on identical input.
//
// brullwurfel is SINGLE-INSTANCE: the SYN state lives at a fixed .bss base
// (0x55eeb9), so no `this` block is allocated -- the synth API funcs use baked
// global addresses. Calling conventions read off the unpacked image + the demo's
// own render call site @0x403288 (see brullwurfel-extraction/NOTES.md):
//   synthInit(patchmap, samplerate)  @0x4057b9 ret8   (zeroes SYN.size=0x1e2dac)
//   synthRender(outbuf, count, 0, 0) @0x40588f ret16  (count nonzero -> render)
//   synthProcessMIDI(midiptr)        @0x405c6c ret4   (0xfd-terminated buf)
//   synthSetGlobals(globals)         @0x405f14 ret4   (22 fild'd globals)
//   rdtsc seeds @0x403bb2, 0x40423f  (osc/LFO) -> pinned to xor eax,eax
// No Ronan (musicdisk; song1 is instrumental).
//
// Build: gcc -m32 -no-pie -O0 c3_oracle.c -o c3_oracle
// Run:   ./c3_oracle [unpacked.bin] [song.v2m] [out.f32] [seconds]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define __USE_GNU 1
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
typedef uint8_t u8; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64;

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x376000u
static const u32 RDTSC_SITES[] = { 0x403bb2u, 0x40423fu };

// binary synth entry points (brullwurfel, base 0x400000)
#define VA_synthInit        0x4057b9u  // (patchmap, samplerate) stdcall ret8
#define VA_synthSetGlobals  0x405f14u  // (globals) stdcall ret4
#define VA_synthProcessMIDI 0x405c6cu  // (midibuf) stdcall ret4
#define VA_synthRender      0x40588fu  // (outbuf, count, 0, 0) stdcall ret16

static void segv(int sig, siginfo_t *si, void *uc_){
    ucontext_t *uc=(ucontext_t*)uc_; unsigned eip=uc->uc_mcontext.gregs[REG_EIP];
    fprintf(stderr,"[c3] SIG%d addr=%p eip=0x%08x\n",sig,si->si_addr,eip);
    if (eip>=IMG_BASE && eip<IMG_BASE+IMG_SIZE){ unsigned char*p=(unsigned char*)(uintptr_t)eip;
        fprintf(stderr,"[c3]   bytes: %02x %02x %02x %02x %02x %02x\n",p[0],p[1],p[2],p[3],p[4],p[5]); }
    _exit(42);
}

// synth API wrappers (stdcall: callee cleans, so just push args + call)
static void synthInit(void*pm){ __asm__ volatile(
    "pushl $44100\n\t pushl %0\n\t mov $0x4057b9,%%eax\n\t call *%%eax\n\t"
    ::"g"(pm):"eax","ecx","edx","cc","memory"); }
static void synthSetGlobals(void*g){ __asm__ volatile(
    "pushl %0\n\t mov $0x405f14,%%eax\n\t call *%%eax\n\t"
    ::"g"(g):"eax","ecx","edx","cc","memory"); }
static void synthProcessMIDI(void*p){ __asm__ volatile(
    "pushl %0\n\t mov $0x405c6c,%%eax\n\t call *%%eax\n\t"
    ::"g"(p):"eax","ecx","edx","cc","memory"); }
static void synthRender(void*buf,int smp){ __asm__ volatile(
    "pushl $0\n\t pushl $0\n\t pushl %1\n\t pushl %0\n\t mov $0x40588f,%%eax\n\t call *%%eax\n\t"
    ::"g"(buf),"g"(smp):"eax","ecx","edx","cc","memory"); }

// ---- player ported verbatim from genthree/_viruz2.cpp (same as c2_oracle) ----
typedef struct { u32 ccnum; u8* ccptr; } bcctl;
typedef struct { u32 notenum; u8* noteptr; u32 pcnum; u8* pcptr; u32 pbnum; u8* pbptr; bcctl ctl[7]; } basech;
static struct { u8* patchmap; u8* globals; u32 timediv,timediv2,maxtime; u8* gptr; u32 gdnum; basech chan[16];
    const char* speechdata; const char* speechptrs[256]; } base;
static struct {
    int running, silent; u32 time, nexttime; u8* gptr; u32 gnt,gnr,usecs,num,den,tpq,bar,beat,tick;
    struct { u8* noteptr; u32 notenr,notent; u8 lastnte,lastvel; u8* pcptr; u32 pcnr,pcnt; u8 lastpc;
             u8* pbptr; u32 pbnr,pbnt; u8 lastpb0,lastpb1;
             struct { u8* ccptr; u32 ccnt,ccnr; u8 lastcc; } ctl[7]; } chan[16];
    u32 cursmpl, smpldelta, smplrem, tdif;
} state;
static u8 midibuf[4096];

#define GETDELTA(p,w) ((u32)((p)[0]+((p)[w]<<8)+((p)[2*(w)]<<16)))
#define UPDATENT(n,v,p,w)  do{ if((n)<(w)){ (v)=state.time+GETDELTA((p),(w)); if((v)<state.nexttime) state.nexttime=(v);} }while(0)
#define UPDATENT2(n,v,p,w) do{ if((n)<(w) && GETDELTA((p),(w))){ (v)=state.time+GETDELTA((p),(w)); } }while(0)
#define UPDATENT3(n,v,p,w) do{ if((n)<(w) && (v)<state.nexttime) state.nexttime=(v); }while(0)
#define PUTSTAT(s) do{ u8 bla=(s); if(laststat!=bla){ laststat=bla; *mptr++=(u8)laststat; } }while(0)

static void ssInitBase(u8* d){
    base.timediv=*(u32*)d; base.timediv2=10000*base.timediv; base.maxtime=*(u32*)(d+4);
    base.gdnum=*(u32*)(d+8); d+=12; base.gptr=d; d+=10*base.gdnum;
    for(int ch=0;ch<16;ch++){ basech*c=&base.chan[ch]; c->notenum=*(u32*)d; d+=4;
        if(c->notenum){ c->noteptr=d; d+=5*c->notenum; c->pcnum=*(u32*)d; d+=4; c->pcptr=d; d+=4*c->pcnum;
            c->pbnum=*(u32*)d; d+=4; c->pbptr=d; d+=5*c->pbnum;
            for(int cn=0;cn<7;cn++){ c->ctl[cn].ccnum=*(u32*)d; d+=4; c->ctl[cn].ccptr=d; d+=4*c->ctl[cn].ccnum; } } }
    u32 size=*(u32*)d; d+=4; base.globals=d; d+=size;
    size=*(u32*)d; d+=4; base.patchmap=d; d+=size;
    u32 spsize=*(u32*)d; d+=4;
    if(!spsize){ for(int i=0;i<256;i++) base.speechptrs[i]=" "; }
    else { base.speechdata=(const char*)d; d+=spsize; u32*p32=(u32*)base.speechdata; u32 n=*p32++;
        for(u32 i=0;i<n;i++) base.speechptrs[i]=base.speechdata+*p32++; }
}
static void ssReset(){
    state.time=0; state.nexttime=(u32)-1; state.gptr=base.gptr; state.gnr=0;
    UPDATENT(state.gnr,state.gnt,state.gptr,base.gdnum);
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        state.chan[ch].noteptr=bc->noteptr; state.chan[ch].notenr=state.chan[ch].lastnte=state.chan[ch].lastvel=0;
        UPDATENT(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum);
        state.chan[ch].pcptr=bc->pcptr; state.chan[ch].pcnr=state.chan[ch].lastpc=0;
        UPDATENT(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum);
        state.chan[ch].pbptr=bc->pbptr; state.chan[ch].pbnr=state.chan[ch].lastpb0=state.chan[ch].lastpb1=0;
        UPDATENT(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pcnum);
        for(int cn=0;cn<7;cn++){ state.chan[ch].ctl[cn].ccptr=bc->ctl[cn].ccptr; state.chan[ch].ctl[cn].ccnr=state.chan[ch].ctl[cn].lastcc=0;
            UPDATENT(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bc->ctl[cn].ccnum); } }
    state.usecs=500000*441; state.num=4; state.den=4; state.tpq=8; state.bar=0; state.beat=0; state.tick=0; state.smplrem=0;
    synthInit(base.patchmap); synthSetGlobals(base.globals);
}
static void ssTick(){
    if(!state.running) return;
    state.tick+=state.nexttime-state.time;
    while(state.tick>=base.timediv){ state.tick-=base.timediv; state.beat++; }
    u32 qpb=(state.num*4/state.den);
    while(state.beat>=qpb){ state.beat-=qpb; state.bar++; }
    state.time=state.nexttime; state.nexttime=(u32)-1;
    u8*mptr=midibuf; u32 laststat=(u32)-1;
    if(state.gnr<base.gdnum && state.time==state.gnt){
        state.usecs=(*(u32*)(state.gptr+3*base.gdnum+4*state.gnr))*441;
        state.num=state.gptr[7*base.gdnum+state.gnr]; state.den=state.gptr[8*base.gdnum+state.gnr];
        state.tpq=state.gptr[9*base.gdnum+state.gnr]; state.gnr++;
        UPDATENT2(state.gnr,state.gnt,state.gptr+state.gnr,base.gdnum); }
    UPDATENT3(state.gnr,state.gnt,state.gptr+state.gnr,base.gdnum);
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        if(state.chan[ch].pcnr<bc->pcnum && state.time==state.chan[ch].pcnt){
            PUTSTAT(0xc0|ch); *mptr++=(state.chan[ch].lastpc+=state.chan[ch].pcptr[3*bc->pcnum]);
            state.chan[ch].pcnr++; state.chan[ch].pcptr++;
            UPDATENT2(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum); }
        UPDATENT3(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum);
        for(int cn=0;cn<7;cn++){ bcctl*bcc=&bc->ctl[cn];
            if(state.chan[ch].ctl[cn].ccnr<bcc->ccnum && state.time==state.chan[ch].ctl[cn].ccnt){
                PUTSTAT(0xb0|ch); *mptr++=cn+1; *mptr++=(state.chan[ch].ctl[cn].lastcc+=state.chan[ch].ctl[cn].ccptr[3*bcc->ccnum]);
                state.chan[ch].ctl[cn].ccnr++; state.chan[ch].ctl[cn].ccptr++;
                UPDATENT2(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bcc->ccnum); }
            UPDATENT3(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bcc->ccnum); }
        if(state.chan[ch].pbnr<bc->pbnum && state.time==state.chan[ch].pbnt){
            PUTSTAT(0xe0|ch); *mptr++=(state.chan[ch].lastpb0+=state.chan[ch].pbptr[3*bc->pcnum]);
            *mptr++=(state.chan[ch].lastpb1+=state.chan[ch].pbptr[4*bc->pcnum]);
            state.chan[ch].pbnr++; state.chan[ch].pbptr++;
            UPDATENT2(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pbnum); }
        UPDATENT3(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pbnum);
        while(state.chan[ch].notenr<bc->notenum && state.time==state.chan[ch].notent){
            PUTSTAT(0x90|ch); *mptr++=(state.chan[ch].lastnte+=state.chan[ch].noteptr[3*bc->notenum]);
            *mptr++=(state.chan[ch].lastvel+=state.chan[ch].noteptr[4*bc->notenum]);
            state.chan[ch].notenr++; state.chan[ch].noteptr++;
            UPDATENT2(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum); }
        UPDATENT3(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum); }
    *mptr++=0xfd;
    synthProcessMIDI(midibuf);
    if(state.nexttime==(u32)-1) state.running=0;
}
static void ssRender(float* outbuf, u32 len){
    if(state.running && !state.silent){
        while(len){ u32 torender=(len>state.smpldelta)?state.smpldelta:len;
            if(torender) synthRender(outbuf,torender);
            outbuf+=2*torender; len-=torender; state.smpldelta-=torender; state.cursmpl+=torender;
            if(!state.smpldelta){ ssTick();
                if(state.running){ u64 prod=(u64)(state.nexttime-state.time)*(u64)state.usecs;
                    u32 q=(u32)(prod/base.timediv2), r=(u32)(prod%base.timediv2);
                    u32 nr=state.smplrem+r, carry=(nr<state.smplrem)?1:0; state.smplrem=nr; state.smpldelta=q+carry; }
                else state.smpldelta=(u32)-1; } }
    } else { memset(outbuf,0,len*2*sizeof(float)); state.cursmpl+=len; }
}

int main(int argc,char**argv){
    const char*img=argc>1?argv[1]:"/tmp/erascan_recon/fr-028/unpacked.bin";
    const char*v2m=argc>2?argv[2]:"/tmp/erascan_recon/fr-028/song1.v2m";
    const char*outp=argc>3?argv[3]:"/tmp/erascan_recon/c3_song1_v5.f32";
    u32 secs=argc>4?atoi(argv[4]):60;
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=segv; sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL); sigaction(SIGFPE,&sa,NULL); sigaction(SIGILL,&sa,NULL);
    void*p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p!=(void*)(uintptr_t)IMG_BASE){fprintf(stderr,"mmap fail %p\n",p);return 1;}
    FILE*f=fopen(img,"rb"); if(!f){fprintf(stderr,"no %s\n",img);return 1;}
    size_t in=fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);
    fprintf(stderr,"[c3] image %zu bytes\n",in);
    for(unsigned i=0;i<sizeof(RDTSC_SITES)/sizeof(RDTSC_SITES[0]);i++){ u8*s=(u8*)(uintptr_t)RDTSC_SITES[i];
        if(s[0]==0x0f&&s[1]==0x31){ s[0]=0x31; s[1]=0xc0; fprintf(stderr,"[c3] rdtsc @0x%x pinned\n",RDTSC_SITES[i]); }
        else fprintf(stderr,"[c3] WARN no rdtsc @0x%x (%02x %02x)\n",RDTSC_SITES[i],s[0],s[1]); }

    FILE*vf=fopen(v2m,"rb"); if(!vf){fprintf(stderr,"no %s\n",v2m);return 1;}
    fseek(vf,0,SEEK_END); long vn=ftell(vf); fseek(vf,0,SEEK_SET);
    u8*vbuf=malloc(vn); fread(vbuf,1,vn,vf); fclose(vf);
    fprintf(stderr,"[c3] v2m %ld bytes\n",vn);

    // The demo's audio-init wrapper (@0x402686, behind Win32/DSound imports we
    // don't run) sets the base scaling const [0x55ee71] = 0.25 before synthInit;
    // without it calcNewSampleRate collapses the SR/filter const [0x408030] to 0
    // (=> voices decay to silence under constant envelope). Replicate it.
    *(u32*)(uintptr_t)0x55ee71u = 0x3e800000u; // 0.25f

    ssInitBase(vbuf);
    fprintf(stderr,"[c3] timediv=%u maxtime=%u gdnum=%u patchmap=%p globals=%p\n",
        base.timediv,base.maxtime,base.gdnum,(void*)base.patchmap,(void*)base.globals);
    int nactive=0; for(int ch=0;ch<16;ch++) if(base.chan[ch].notenum) nactive++;
    fprintf(stderr,"[c3] active channels=%d\n",nactive);
    ssReset();
    if(getenv("C3_VOICES")){ fprintf(stderr,"[c3] SR consts after init:");
        for(u32 a=0x408020;a<=0x408060;a+=4) fprintf(stderr," [%x]=%.6g",a,*(float*)(uintptr_t)a);
        fprintf(stderr,"\n[c3] samplerate field [0x55eec5]=%d\n",*(s32*)(uintptr_t)0x55eec5); }
    state.cursmpl=state.smpldelta=0; state.running=1; state.silent=0;

    u32 total=secs*44100, done=0; double peak=0;
    u32 CHUNK=getenv("C3_CHUNK")?atoi(getenv("C3_CHUNK")):2048;
    int dbg=getenv("C3_VOICES")!=NULL;
    const u32 CHANMAP=0x55eec9u; // SYN.chanmap, 32 entries, free if MSB set (js)
    float*buf=malloc(8192*2*sizeof(float));
    FILE*out=fopen(outp,"wb");
    u64 nextlog=0;
    while(done<total){ u32 n=(total-done<CHUNK)?(total-done):CHUNK;
        ssRender(buf,n);
        for(u32 i=0;i<n*2;i++){double a=buf[i];if(a<0)a=-a;if(a>peak)peak=a;}
        fwrite(buf,2*sizeof(float),n,out); done+=n;
        if(dbg && done>=nextlog){ int act=0; for(int v=0;v<32;v++){ s32 c=*(s32*)(uintptr_t)(CHANMAP+v*4); if(c>=0) act++; }
            double crms=0; for(u32 i=0;i<n*2;i++) crms+=buf[i]*buf[i]; crms=__builtin_sqrt(crms/(n*2));
            fprintf(stderr,"[c3] t=%2llus active=%2d rms=%.4f | ",(unsigned long long)(done/44100),act,crms);
            const u32 VB=0x560dcdu, ST=0x228u;
            for(int v=0;v<32 && act;v++){ s32 c=*(s32*)(uintptr_t)(CHANMAP+v*4); if(c<0) continue;
                u32 vb=VB+v*ST; s32 note=*(s32*)(uintptr_t)(vb+0); s32 gate=*(s32*)(uintptr_t)(vb+8);
                s32 est=*(s32*)(uintptr_t)(vb+0x118); float eval=*(float*)(uintptr_t)(vb+0x11c);
                float cv=*(float*)(uintptr_t)(vb+0xc);
                s32 fst=*(s32*)(uintptr_t)(vb+0x13c); float fval=*(float*)(uintptr_t)(vb+0x140);
                fprintf(stderr,"v%d[ch%d n%d est%d ev%.1f cv%.2f fenv:st%d v%.1f] ",v,c,note,est,eval,cv,fst,fval); }
            fprintf(stderr,"\n"); nextlog+=44100; }
        if(!state.running){ fprintf(stderr,"[c3] song ended at %.1fs\n",(double)done/44100); break; } }
    fclose(out);
    fprintf(stderr,"[c3] rendered %.1fs peak=%.4f -> %s\n",(double)done/44100,peak,outp);
    return 0;
}
