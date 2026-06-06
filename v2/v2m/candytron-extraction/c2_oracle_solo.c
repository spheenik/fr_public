// C2 oracle: candytron's V2 synth (from the mapped unpacked binary) driven by
// the GENUINE source-code V2M player (ported verbatim from genthree/_viruz2.cpp).
// Renders genthree/josie.v2m to f32. This fixes the MIDI-timing the binary's own
// werkkzeug driver made hard to replicate.
//
// Build: gcc -m32 -no-pie -O0 c2_oracle.c -o c2_oracle
// Run:   ./c2_oracle [unpacked.bin] [josie.v2m] [out.f32] [seconds]

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
#define IMG_SIZE 0xb5f000u
static const u32 RDTSC_SITES[] = { 0x41dca8u, 0x41e32du };
#define IAT_ALLOC 0x42003cu
#define IAT_ALLOC2 0x420050u
#define IAT_FREE  0x420038u

// binary synth entry points (see candytron-extraction/NOTES.md)
#define VA_synthInit       0x41f7e4u  // (patchmap, samplerate) stdcall ret8
#define VA_synthSetGlobals 0x41fe84u  // (globals) stdcall ret4
#define VA_synthProcessMIDI 0x41fbdcu // (midibuf) stdcall ret4
#define VA_synthRender     0x41f8c2u  // (buf, smp, buf2, add) stdcall ret16

static void segv(int sig, siginfo_t *si, void *uc_){
    ucontext_t *uc=(ucontext_t*)uc_; unsigned eip=uc->uc_mcontext.gregs[REG_EIP];
    fprintf(stderr,"[c2] SIG%d addr=%p eip=0x%08x\n",sig,si->si_addr,eip); _exit(42);
}
static void* __attribute__((stdcall)) stub_alloc(int one,int size){(void)one;return calloc(1,size?size:1);}
static void* __attribute__((stdcall)) stub_ident(void*p){return p;}
static void  __attribute__((stdcall)) stub_free(void*p){free(p);}

static void synthInit(void*pm){ __asm__ volatile("pushl $44100\n\t pushl %0\n\t mov $0x41f7e4,%%eax\n\t call *%%eax\n\t"::"g"(pm):"eax","ecx","edx","cc","memory"); }
static void synthSetGlobals(void*g){ __asm__ volatile("pushl %0\n\t mov $0x41fe84,%%eax\n\t call *%%eax\n\t"::"g"(g):"eax","ecx","edx","cc","memory"); }
static void synthProcessMIDI(void*p){ __asm__ volatile("pushl %0\n\t mov $0x41fbdc,%%eax\n\t call *%%eax\n\t"::"g"(p):"eax","ecx","edx","cc","memory"); }
static void synthRender(void*buf,int smp){ __asm__ volatile("pushl $0\n\t pushl $0\n\t pushl %1\n\t pushl %0\n\t mov $0x41f8c2,%%eax\n\t call *%%eax\n\t"::"g"(buf),"g"(smp):"eax","ecx","edx","cc","memory"); }
// synthSetLyrics @0x414ab7: eax = ptr to 64 lyric char*; copies to texts@0xefa370, sets ronan ptrs
static void synthSetLyrics(const char**ptr){ __asm__ volatile("mov %0,%%eax\n\t mov $0x414ab7,%%ecx\n\t call *%%ecx\n\t"::"g"(ptr):"eax","ecx","edx","esi","edi","cc","memory"); }

// ---- player ported verbatim from genthree/_viruz2.cpp ----
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
    if(!getenv("C2_NORONAN")) synthSetLyrics(base.speechptrs);  // RONAN
}
// voice-allocation tap (C2_STEAL): dump chanmap changes (alloc / free / steal)
// chanmap[32] @0x4c2cdc, allocpos[32] @0x4c2d5c (SYN: data=0x4c2ccc).
static void pollSteal(const char* tag){
    static int on=-1; if(on<0){ on=getenv("C2_STEAL")?1:0; }
    if(!on) return;
    static int prev[32]; static int init=0;
    int* cm=(int*)(uintptr_t)0x4c2cdcu; u32* ap=(u32*)(uintptr_t)0x4c2d5cu;
    if(!init){ for(int v=0;v<32;v++) prev[v]=cm[v]; init=1; return; }
    for(int v=0;v<32;v++) if(cm[v]!=prev[v]){
        fprintf(stderr,"[binS] smpl=%u %s v=%d chan %d->%d alloc=%u\n",
                state.cursmpl,tag,v,prev[v],cm[v],ap[v]);
        prev[v]=cm[v];
    }
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
    static int g_solo=-2; if(g_solo==-2){const char*e=getenv("C2_SOLO"); g_solo=e?atoi(e):-1;}
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        // solo: mirror v2seq -- run ALL channels' counters + nexttime bookkeeping
        // (so the timing grid is identical to a full render), then rewind mptr to
        // discard non-solo emitted MIDI. Skipping the whole channel (the old
        // `continue`) changed the nexttime grid -> smpldelta subdivision ->
        // ~frame cumulative-rounding drift = a SOLO ARTIFACT, not a real diff.
        u8* solo_mptr0=mptr; u32 solo_laststat0=laststat;
        if(state.chan[ch].pcnr<bc->pcnum && state.time==state.chan[ch].pcnt){
            PUTSTAT(0xc0|ch); *mptr++=(state.chan[ch].lastpc+=state.chan[ch].pcptr[3*bc->pcnum]);
            state.chan[ch].pcnr++; state.chan[ch].pcptr++;
            UPDATENT2(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum); }
        UPDATENT3(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum);
        for(int cn=0;cn<7;cn++){ bcctl*bcc=&bc->ctl[cn];
            if(state.chan[ch].ctl[cn].ccnr<bcc->ccnum && state.time==state.chan[ch].ctl[cn].ccnt){
                PUTSTAT(0xb0|ch); *mptr++=cn+1; *mptr++=(state.chan[ch].ctl[cn].lastcc+=state.chan[ch].ctl[cn].ccptr[3*bcc->ccnum]);
                if(ch==15&&getenv("C2_NTAP"))fprintf(stderr,"[binCC] smpl=%u ch15 cc=%d val=%d\n",state.cursmpl,cn+1,state.chan[ch].ctl[cn].lastcc);
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
            if(ch==15&&getenv("C2_NTAP"))fprintf(stderr,"[binN] smpl=%u ch15 note=%d vel=%d\n",state.cursmpl,state.chan[ch].lastnte,state.chan[ch].lastvel);
            state.chan[ch].notenr++; state.chan[ch].noteptr++;
            UPDATENT2(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum); }
        UPDATENT3(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum);
        if(g_solo>=0 && ch!=g_solo){ mptr=solo_mptr0; laststat=solo_laststat0; } }
    *mptr++=0xfd;
    synthProcessMIDI(midibuf);
    pollSteal("MIDI");
    if(getenv("C2_VTAP")){
        static u32 pf[32]={0};
        for(int v=0;v<32;v++){
            u32 base=0x4c4be0u+(u32)v*0x228u;
            u32 freq=*(u32*)(uintptr_t)(base+0x3c);
            u32 cnt =*(u32*)(uintptr_t)(base+0x38);
            if(freq!=pf[v]){ fprintf(stderr,"[bin] t=%u v=%d freq=%u cnt=%u\n",state.time,v,freq,cnt); pf[v]=freq; }
        }
    }
    if(state.nexttime==(u32)-1) state.running=0;
}
static void ssRender(float* outbuf, u32 len){
    if(state.running && !state.silent){
        while(len){ u32 torender=(len>state.smpldelta)?state.smpldelta:len;
            if(torender) synthRender(outbuf,torender);
            pollSteal("REND");
            if(getenv("C2_RTAP")){ // ronan workspace tap: a_voicing/a_bypass + f1 coeffs
                u32 ws=*(u32*)(uintptr_t)0x6a88f8u;
                if(ws){ float av=*(float*)(uintptr_t)(ws+0x54), ab=*(float*)(uintptr_t)(ws+0x60);
                    float f1a=*(float*)(uintptr_t)(ws+0x0c),f1b=*(float*)(uintptr_t)(ws+0x10),f1c=*(float*)(uintptr_t)(ws+0x14);
                    int w0=*(int*)(uintptr_t)(ws+0x150),w1=*(int*)(uintptr_t)(ws+0x154);
                    fprintf(stderr,"[binR] smpl=%u av=%.7g ab=%.7g w150=%d w154=%d f1=%.6g,%.6g,%.6g\n",state.cursmpl,av,ab,w0,w1,f1a,f1b,f1c); } }
            if(getenv("C2_STAP")){ // ronan SEQUENCER state tap
                u32 ws=*(u32*)(uintptr_t)0x6a88f8u;
                if(ws){ int fc=*(int*)(uintptr_t)(ws+0x128),sp=*(int*)(uintptr_t)(ws+0x12c),sc=*(int*)(uintptr_t)(ws+0x130);
                    int cs=*(int*)(uintptr_t)(ws+0x134),p2=*(int*)(uintptr_t)(ws+0x14c),w0=*(int*)(uintptr_t)(ws+0x150),w1=*(int*)(uintptr_t)(ws+0x154);
                    u32 bp=*(u32*)(uintptr_t)(ws+0x140),pt=*(u32*)(uintptr_t)(ws+0x144);
                    fprintf(stderr,"[binSEQ] smpl=%u w4on=%d w4off=%d fc=%d sc=%d sp=%d syl=%d p2=%d ptr=%d\n",state.cursmpl,w0,w1,fc,sc,sp,cs,p2,(int)(pt-bp)); } }
            if(getenv("C2_CTAP")){ u32 b=0x4c4be0u;
                float cv=*(float*)(uintptr_t)(b+0x0c), vr=*(float*)(uintptr_t)(b+0x10);
                u32 cnt=*(u32*)(uintptr_t)(b+0x38);
                fprintf(stderr,"[binC] smpl=%u cnt=%u curvol=%.9g volramp=%.9g\n",state.cursmpl,cnt,cv,vr); }
            outbuf+=2*torender; len-=torender; state.smpldelta-=torender; state.cursmpl+=torender;
            if(!state.smpldelta){ ssTick();
                if(state.running){ u64 prod=(u64)(state.nexttime-state.time)*(u64)state.usecs;
                    u32 q=(u32)(prod/base.timediv2), r=(u32)(prod%base.timediv2);
                    u32 nr=state.smplrem+r, carry=(nr<state.smplrem)?1:0; state.smplrem=nr; state.smpldelta=q+carry; }
                else state.smpldelta=(u32)-1; } }
    } else { memset(outbuf,0,len*2*sizeof(float)); state.cursmpl+=len; }
}

int main(int argc,char**argv){
    const char*img=argc>1?argv[1]:"/tmp/candytron/unpacked.bin";
    const char*v2m=argc>2?argv[2]:"/home/spheenik/projects/scene/fr_public/genthree/data/josie.v2m";
    const char*outp=argc>3?argv[3]:"/tmp/candytron/c2_josie.f32";
    u32 secs=argc>4?atoi(argv[4]):45;
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=segv; sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL); sigaction(SIGFPE,&sa,NULL);
    void*p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(p!=(void*)(uintptr_t)IMG_BASE){fprintf(stderr,"mmap fail\n");return 1;}
    FILE*f=fopen(img,"rb"); if(!f){fprintf(stderr,"no %s\n",img);return 1;}
    fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);
    for(unsigned i=0;i<2;i++){ u8*s=(u8*)(uintptr_t)RDTSC_SITES[i]; if(s[0]==0x0f&&s[1]==0x31){s[0]=0x31;s[1]=0xc0;} }
    // C2_RONAN_NOP: NOP the syRonanProcess call @0x41fac3 (e8 12 fd ff ff) so
    // ch15's RAW voice excitation (chanbuf @0x4c1248) flows through syChanProcess
    // to the mix unmodified -- lets us verify the ronan INPUT vs the portable.
    if(getenv("C2_RONAN_NOP")){ u8*s=(u8*)(uintptr_t)0x41fac3u;
        if(s[0]==0xe8){ for(int k=0;k<5;k++) s[k]=0x90; fprintf(stderr,"[c2] ronan call NOPed\n"); } }
    *(u32*)(uintptr_t)IAT_ALLOC=(u32)(uintptr_t)&stub_alloc;
    *(u32*)(uintptr_t)IAT_ALLOC2=(u32)(uintptr_t)&stub_ident;
    *(u32*)(uintptr_t)IAT_FREE=(u32)(uintptr_t)&stub_free;

    // load the v2m
    FILE*vf=fopen(v2m,"rb"); if(!vf){fprintf(stderr,"no %s\n",v2m);return 1;}
    fseek(vf,0,SEEK_END); long vn=ftell(vf); fseek(vf,0,SEEK_SET);
    u8*vbuf=malloc(vn); fread(vbuf,1,vn,vf); fclose(vf);
    fprintf(stderr,"[c2] v2m %ld bytes\n",vn);

    ssInitBase(vbuf);
    fprintf(stderr,"[c2] timediv=%u maxtime=%u gdnum=%u patchmap=%p globals=%p\n",
        base.timediv,base.maxtime,base.gdnum,(void*)base.patchmap,(void*)base.globals);
    fprintf(stderr,"[c2] ch15 notenum=%u pcnum=%u pbnum=%u ctl[3].ccnum=%u (ctl4=text-select)\n",
        base.chan[15].notenum, base.chan[15].pcnum, base.chan[15].pbnum, base.chan[15].ctl[3].ccnum);
    fprintf(stderr,"[c2] speech texts[0..5]: ");
    for(int i=0;i<6;i++){ const char*s=base.speechptrs[i]; fprintf(stderr,"[%d]=\"%.12s\" ",i,s?s:"(null)"); }
    fprintf(stderr,"\n");
    // [solo-dbg] per-channel note counts + first-note tick
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        u32 firsttick=GETDELTA(bc->noteptr,bc->notenum);
        fprintf(stderr,"[ch%d] notes=%u firsttick=%u (~%.3fs @120bpm-ish)\n",ch,bc->notenum,firsttick,firsttick/480.0*0.5); }
    ssReset();
    state.cursmpl=state.smpldelta=0; state.running=1; state.silent=0; // silent=0 to actually play

    u32 total=secs*44100, done=0; double peak=0;
    u32 CHUNK=getenv("C2_CHUNK")?atoi(getenv("C2_CHUNK")):2048;
    float*buf=malloc(8192*2*sizeof(float));
    FILE*out=fopen(outp,"wb");
    while(done<total){ u32 n=(total-done<CHUNK)?(total-done):CHUNK;
        ssRender(buf,n);
        for(u32 i=0;i<n*2;i++){double a=buf[i];if(a<0)a=-a;if(a>peak)peak=a;}
        fwrite(buf,2*sizeof(float),n,out); done+=n;
        if(!state.running){ fprintf(stderr,"[c2] song ended at %.1fs\n",(double)done/44100); break; } }
    fclose(out);
    fprintf(stderr,"[c2] rendered %.1fs peak=%.4f\n",(double)done/44100,peak);
    return 0;
}
