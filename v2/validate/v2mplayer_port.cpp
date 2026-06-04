/*************************************************************************************/
/*************************************************************************************/
/**                                                                                 **/
/**  V2 module player (.v2m)                                                        **/
/**  (c) Tammo 'kb' Hinrichs 2000-2008                                              **/
/**  This file is under the Artistic License 2.0, see LICENSE.txt for details       **/
/**                                                                                 **/
/*************************************************************************************/
/*************************************************************************************/


#include "v2mplayer.h"
#include "libv2.h"
#include <cstdlib> // getenv (POISON diagnostic)
#include <cstdio>  // bus-tap dump (BUSTAP diagnostic)

// Bus-tap (gated by env BUSTAP=<prefix>): after each synthRender call, append
// the live per-frame bus buffers to <prefix>.aux1/.aux2/.mix. Both cores get
// the identical chunk sequence, so the dumps are call-for-call comparable;
// diffing them bisects channel-chain (aux*) vs global-FX (mix) divergence.
// synthDebugGetBus is provided by whichever synth core is linked (C++ or ASM).
extern "C" void __stdcall synthDebugGetBus(void *, float **, float **, float **, int *);
// Per-stage mix-chain tap: snapshots of mixbuf after each global FX stage
// (reverb, mod-delay, dcf, low-cut/high-cut, sum compressor). Lets the A/B
// harness bisect WHICH global stage introduces the divergence. Provided by both
// cores (C++ under V2_VALIDATE; asm via asm_appendix.asm + redef step).
extern "C" void __stdcall synthDebugGetMixTap(void *, float **, float **, float **,
                                              float **, float **, int *);
// Per-voice-substage tap (mono vcebuf snapshots after osc/filter/dist/dcf): one
// level deeper than the mix tap, to localize which voice block first diverges.
extern "C" void __stdcall synthDebugGetVceTap(void *, float **, float **, float **,
                                              float **, int *);
extern "C" void __stdcall synthDebugGetChanTap(void *, float **, int *);
// Dry mix (mixbuf before any global FX): isolates dry-mix vs reverb as the source.
extern "C" void __stdcall synthDebugGetPreMix(void *, float **, int *);
// Per-channel-chain sub-stage tap (stereo chanbuf snapshots after each channel-FX
// block: dcf1/comp/boost/dist/chorus/dcf2): one level below the chan tap, to pin
// WHICH channel-FX block first diverges. Provided by both cores.
extern "C" void __stdcall synthDebugGetChainTap(void *, float **, float **, float **,
                                                float **, float **, float **, int *);
static void bustap_dump(void *synth)
{
  static FILE *fa1 = 0, *fa2 = 0, *fmx = 0;
  static FILE *fpr = 0, *fpd = 0, *fpf = 0, *fpl = 0, *fpc = 0; // per-stage taps
  static FILE *fvo = 0, *fvf = 0, *fvd = 0, *fvc = 0;           // per-voice taps
  static FILE *fch = 0;                                         // channel sum tap
  static FILE *fc1 = 0, *fcm = 0, *fcb = 0, *fcd = 0, *fcc = 0, *fc2 = 0; // chain sub-stage
  static FILE *fpm = 0;                                         // dry mix (pre-reverb)
  static int armed = -1;
  if (armed < 0) {
    const char *pfx = getenv("BUSTAP");
    armed = pfx ? 1 : 0;
    if (armed) {
      char p[600];
      snprintf(p, sizeof p, "%s.aux1", pfx); fa1 = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.aux2", pfx); fa2 = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.mix",  pfx); fmx = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.post_reverb", pfx); fpr = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.post_delay",  pfx); fpd = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.post_dcf",    pfx); fpf = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.post_lchc",   pfx); fpl = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.post_compr",  pfx); fpc = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.vce_osc",  pfx); fvo = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.vce_flt",  pfx); fvf = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.vce_dist", pfx); fvd = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.vce_dcf",  pfx); fvc = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.chan",     pfx); fch = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_dcf1",  pfx); fc1 = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_comp",  pfx); fcm = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_boost", pfx); fcb = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_dist",  pfx); fcd = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_chorus",pfx); fcc = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.ch_dcf2",  pfx); fc2 = fopen(p, "wb");
      snprintf(p, sizeof p, "%s.premix",   pfx); fpm = fopen(p, "wb");
    }
  }
  if (!armed) return;
  float *a1, *a2, *mx; int n = 0;
  synthDebugGetBus(synth, &a1, &a2, &mx, &n);
  if (fa1) fwrite(a1, sizeof(float), n,   fa1);
  if (fa2) fwrite(a2, sizeof(float), n,   fa2);
  if (fmx) fwrite(mx, sizeof(float), 2*n, fmx);
  float *pr, *pd, *pf, *pl, *pc; int n2 = 0;
  synthDebugGetMixTap(synth, &pr, &pd, &pf, &pl, &pc, &n2);
  if (fpr) fwrite(pr, sizeof(float), 2*n2, fpr);
  if (fpd) fwrite(pd, sizeof(float), 2*n2, fpd);
  if (fpf) fwrite(pf, sizeof(float), 2*n2, fpf);
  if (fpl) fwrite(pl, sizeof(float), 2*n2, fpl);
  if (fpc) fwrite(pc, sizeof(float), 2*n2, fpc);
  float *vo, *vf, *vd, *vc; int n3 = 0;
  synthDebugGetVceTap(synth, &vo, &vf, &vd, &vc, &n3); // mono streams
  if (fvo) fwrite(vo, sizeof(float), n3, fvo);
  if (fvf) fwrite(vf, sizeof(float), n3, fvf);
  if (fvd) fwrite(vd, sizeof(float), n3, fvd);
  if (fvc) fwrite(vc, sizeof(float), n3, fvc);
  float *cb; int n4 = 0;
  synthDebugGetChanTap(synth, &cb, &n4);   // stereo
  if (fch) fwrite(cb, sizeof(float), 2*n4, fch);
  float *c1, *cm, *cbo, *cd, *cc, *c2; int n5 = 0;
  synthDebugGetChainTap(synth, &c1, &cm, &cbo, &cd, &cc, &c2, &n5); // stereo
  if (fc1) fwrite(c1,  sizeof(float), 2*n5, fc1);
  if (fcm) fwrite(cm,  sizeof(float), 2*n5, fcm);
  if (fcb) fwrite(cbo, sizeof(float), 2*n5, fcb);
  if (fcd) fwrite(cd,  sizeof(float), 2*n5, fcd);
  if (fcc) fwrite(cc,  sizeof(float), 2*n5, fcc);
  if (fc2) fwrite(c2,  sizeof(float), 2*n5, fc2);
  float *pm; int n6 = 0;
  synthDebugGetPreMix(synth, &pm, &n6);    // stereo dry mix (pre-reverb)
  if (fpm) fwrite(pm, sizeof(float), 2*n6, fpm);
}

// Event trace (gated by env EVTRACE=1): decode the per-Tick MIDI buffer and print
// the current sample position + note/CC/PB/PC events to stderr. Lets us correlate
// a divergence sample (e.g. ~4563) with the musical event that triggers it.
static void evtrace(unsigned cursmpl, unsigned ticktime, const unsigned char *buf)
{
  static int armed = -1;
  if (armed < 0) armed = getenv("EVTRACE") ? 1 : 0;
  if (!armed) return;
  char line[1024]; int o = 0;
  o += snprintf(line+o, sizeof line-o, "smpl=%-7u tick=%-6u |", cursmpl, ticktime);
  const unsigned char *p = buf; unsigned char st = 0; int any = 0;
  while (*p != 0xfd && o < (int)sizeof line - 64) {
    if (*p >= 0x80) st = *p++;
    unsigned ch = st & 0x0f, cmd = st & 0xf0;
    if (cmd == 0x90)      { int n=p[0],v=p[1]; p+=2; o+=snprintf(line+o,sizeof line-o," ch%u %s n%d v%d", ch, v?"NOTEON":"noteoff", n, v); any=1; }
    else if (cmd == 0xb0) { int c=p[0],v=p[1]; p+=2; o+=snprintf(line+o,sizeof line-o," ch%u CC%d=%d", ch, c, v); any=1; }
    else if (cmd == 0xc0) { int v=p[0]; p+=1;       o+=snprintf(line+o,sizeof line-o," ch%u PGM=%d", ch, v); any=1; }
    else if (cmd == 0xe0) { int a=p[0],b=p[1]; p+=2; o+=snprintf(line+o,sizeof line-o," ch%u PB=%d,%d", ch, a, b); any=1; }
    else break;
  }
  if (any) fprintf(stderr, "%s\n", line);
}

#define GETDELTA(p, w) ((p)[0]+((p)[w]<<8)+((p)[2*w]<<16))
#define UPDATENT(n, v, p, w) if ((n)<(w)) { (v)=m_state.time+GETDELTA((p), (w)); if ((v)<m_state.nexttime) m_state.nexttime=(v); }
#define UPDATENT2(n, v, p, w) if ((n)<(w) && GETDELTA((p), (w))) { (v)=m_state.time+GETDELTA((p), (w)); }
#define UPDATENT3(n, v, p, w) if ((n)<(w) && (v)<m_state.nexttime) m_state.nexttime=(v); 
#define PUTSTAT(s) { sU8 bla=(s); if (laststat!=bla) { laststat=bla; *mptr++=(sU8)laststat; }};



namespace
{
	void UpdateSampleDelta(sU32 nexttime, sU32 time, sU32 usecs, sU32 td2, sU32 *smplrem, sU32 *smpldelta)
  //////////////////////////////////////////////////////////////////////////////////////////////////////
	{
		// performs 64bit (nexttime-time)*usecs/td2 and a 32.32bit addition to smpldelta:smplrem
		// [validation port] portable equivalent of the original x86 mul/div/adc.
		// Original: edx:eax = (nexttime-time)*usecs (unsigned 64b); div td2 ->
		// quotient eax, remainder edx; *smplrem += edx (carry -> eax); *smpldelta = eax.
		sU64 prod = (sU64)(nexttime - time) * usecs;
		sU32 quot = (sU32)(prod / td2);
		sU32 rem  = (sU32)(prod % td2);
		sU32 newrem = *smplrem + rem;
		sU32 carry  = (newrem < *smplrem) ? 1 : 0; // unsigned overflow == x86 carry
		*smplrem   = newrem;
		*smpldelta = quot + carry;
	}
}



sBool V2MPlayer::InitBase(const void *a_v2m)
///////////////////////////////////////
{
	const sU8 *d=(const sU8*)a_v2m;
	m_base.timediv=(*((sU32*)(d)));
	m_base.timediv2=10000*m_base.timediv;
	m_base.maxtime=*((sU32*)(d+4));
	m_base.gdnum=*((sU32*)(d+8));
	d+=12;
	m_base.gptr=d;
  d+=10*m_base.gdnum;
	for (sInt ch=0; ch<16; ch++)
	{
		V2MBase::Channel &c=m_base.chan[ch];
		c.notenum=*((sU32*)d);
		d+=4;
		if (c.notenum)
		{
			c.noteptr=d;
			d+=5*c.notenum;
			c.pcnum=*((sU32*)d);
			d+=4;
			c.pcptr=d;
			d+=4*c.pcnum;
			c.pbnum=*((sU32*)d);
			d+=4;
			c.pbptr=d;
			d+=5*c.pbnum;
			for (sInt cn=0; cn<7; cn++)
			{
				V2MBase::Channel::CC &cc=c.ctl[cn];
				cc.ccnum=*((sU32*)d);
				d+=4;
				cc.ccptr=d;
				d+=4*cc.ccnum;
			}						
		}		
	}
	sInt size=*((sU32*)d);
	if (size>16384 || size<0) return sFALSE;
	d+=4;
	m_base.globals=d;
	d+=size;
	size=*((sU32*)d);
	if (size>1048576 || size<0) return sFALSE;
	d+=4;
	m_base.patchmap=d;
	d+=size;

  sU32 spsize=*((sU32*)d);
	d+=4;
	if (!spsize || spsize>=8192)
	{
		for (sU32 i=0; i<256; i++)
			m_base.speechptrs[i]=" ";
	}
	else
	{
		m_base.speechdata=(const char *)d;
		d+=spsize;
		const sU32 *p32=(const sU32*)m_base.speechdata;
		sU32 n=*(p32++);
		for (sU32 i=0; i<n; i++)
		{
			m_base.speechptrs[i]=m_base.speechdata+*(p32++);
		}
	}

  return sTRUE;
}



void V2MPlayer::Reset()
////////////////////////
{
	m_state.time=0;
	m_state.nexttime=(sU32)-1;

	m_state.gptr=m_base.gptr;
	m_state.gnr=0;
	UPDATENT(m_state.gnr, m_state.gnt, m_state.gptr, m_base.gdnum);
	for (sInt ch=0; ch<16; ch++)
	{
		V2MBase::Channel &bc=m_base.chan[ch];
		PlayerState::Channel &sc=m_state.chan[ch];

		if (!bc.notenum) continue;
		sc.noteptr=bc.noteptr;
		sc.notenr=sc.lastnte=sc.lastvel=0;
		UPDATENT(sc.notenr,sc.notent, sc.noteptr, bc.notenum);
		sc.pcptr=bc.pcptr;
		sc.pcnr=sc.lastpc=0;
		UPDATENT(sc.pcnr,sc.pcnt, sc.pcptr, bc.pcnum);
		sc.pbptr=bc.pbptr;
		sc.pbnr=sc.lastpb0=sc.lastpb1=0;
		UPDATENT(sc.pbnr,sc.pbnt, sc.pbptr, bc.pcnum);
		for (sInt cn=0; cn<7; cn++)
		{
			V2MBase::Channel::CC &bcc=bc.ctl[cn];
			PlayerState::Channel::CC &scc=sc.ctl[cn];
			scc.ccptr=bcc.ccptr;
			scc.ccnr=scc.lastcc=0;
			UPDATENT(scc.ccnr,scc.ccnt,scc.ccptr,bcc.ccnum);
		}
	}
	m_state.usecs=5000*m_samplerate;
	m_state.num=4;
	m_state.den=4;
	m_state.tpq=8;
	m_state.bar=0;
	m_state.beat=0;
	m_state.tick=0;
	m_state.smplrem=0;


	if (m_samplerate)
	{
		// DIAGNOSTIC (gated by env POISON=1): fill the synth instance with a
		// non-zero byte pattern before synthInit. The ASM core zeroes the whole
		// instance itself (rep stosb SYN.size) so it is unaffected; a C++ core
		// that relies on caller-zeroed memory will diverge. Used to prove the
		// sizeof(this)->sizeof(*this) init-zeroing fix. No-op unless POISON set.
		if (getenv("POISON"))
			memset(m_synth, 0xCC, sizeof(m_synth));
		synthInit(m_synth,(void*)m_base.patchmap,m_samplerate);
		synthSetGlobals(m_synth,(void*)m_base.globals);
		synthSetLyrics(m_synth,m_base.speechptrs);
	}
}



void V2MPlayer::Tick()
///////////////////////
{
	if (m_state.state != PlayerState::PLAYING)
		return;

	m_state.tick+=m_state.nexttime-m_state.time;
	while (m_state.tick>=m_base.timediv)
	{
		m_state.tick-=m_base.timediv;
		m_state.beat++;
	}
	sU32 qpb=(m_state.num*4/m_state.den);
	while (m_state.beat>=qpb)
	{
		m_state.beat-=qpb;
		m_state.bar++;
	}


	m_state.time=m_state.nexttime;
	m_state.nexttime=(sU32)-1;
	sU8 *mptr=m_midibuf;
	sU32 laststat=-1;

	if (m_state.gnr<m_base.gdnum && m_state.time==m_state.gnt) // neues global-event?
	{
		m_state.usecs=(*(sU32 *)(m_state.gptr+3*m_base.gdnum+4*m_state.gnr))*(m_samplerate/100);
		m_state.num=m_state.gptr[7*m_base.gdnum+m_state.gnr];
		m_state.den=m_state.gptr[8*m_base.gdnum+m_state.gnr];
		m_state.tpq=m_state.gptr[9*m_base.gdnum+m_state.gnr];
		m_state.gnr++;
		UPDATENT2(m_state.gnr, m_state.gnt, m_state.gptr+m_state.gnr, m_base.gdnum);
	}
	UPDATENT3(m_state.gnr, m_state.gnt, m_state.gptr+m_state.gnr, m_base.gdnum);

	// [validation] CHANSOLO=<n>: keep only channel n's MIDI (mute the rest). All
	// event-pointer advancement and scheduling stay identical (we discard the
	// emitted BYTES, not the bookkeeping), so timing is bit-for-bit the full song
	// with the other channels silenced. Lets the A/B harness isolate one channel's
	// full contribution (voice + its channel FX) in both cores.
	static int chansolo = -2;
	if (chansolo == -2) { const char *e = getenv("CHANSOLO"); chansolo = e ? atoi(e) : -1; }

	for (sInt ch=0; ch<16; ch++)
	{
		V2MBase::Channel &bc=m_base.chan[ch];
		PlayerState::Channel &sc=m_state.chan[ch];
		if (!bc.notenum)
			continue;
		sU8 *solo_mptr0 = mptr; sU32 solo_laststat0 = laststat; // for CHANSOLO discard
		// 1. process pgm change events
		if (sc.pcnr<bc.pcnum && m_state.time==sc.pcnt)
		{
			PUTSTAT(0xc0|ch)
			*mptr++=(sc.lastpc+=sc.pcptr[3*bc.pcnum]);
			sc.pcnr++;
			sc.pcptr++;
			UPDATENT2(sc.pcnr,sc.pcnt,sc.pcptr,bc.pcnum);
		}
		UPDATENT3(sc.pcnr,sc.pcnt,sc.pcptr,bc.pcnum);

		// 2. process control changes
 		for (sInt cn=0; cn<7; cn++)
		{
				V2MBase::Channel::CC &bcc=bc.ctl[cn];
				PlayerState::Channel::CC &scc=sc.ctl[cn];
				if (scc.ccnr<bcc.ccnum && m_state.time==scc.ccnt)
				{
					PUTSTAT(0xb0|ch)
					*mptr++=cn+1;
					*mptr++=(scc.lastcc+=scc.ccptr[3*bcc.ccnum]);
					scc.ccnr++;
					scc.ccptr++;
					UPDATENT2(scc.ccnr,scc.ccnt,scc.ccptr,bcc.ccnum);
				}
				UPDATENT3(scc.ccnr,scc.ccnt,scc.ccptr,bcc.ccnum);
		}

		// 3. process pitch bends
		if (sc.pbnr<bc.pbnum && m_state.time==sc.pbnt)
		{
			PUTSTAT(0xe0|ch)
			*mptr++=(sc.lastpb0+=sc.pbptr[3*bc.pcnum]);
			*mptr++=(sc.lastpb1+=sc.pbptr[4*bc.pcnum]);
			sc.pbnr++;
			sc.pbptr++;
			UPDATENT2(sc.pbnr,sc.pbnt,sc.pbptr,bc.pbnum);
		}
		UPDATENT3(sc.pbnr,sc.pbnt,sc.pbptr,bc.pbnum);

		// 4. process notes
		while (sc.notenr<bc.notenum && m_state.time==sc.notent)
		{
			PUTSTAT(0x90|ch)
			*mptr++=(sc.lastnte+=sc.noteptr[3*bc.notenum]);
			*mptr++=(sc.lastvel+=sc.noteptr[4*bc.notenum]);
			sc.notenr++;
			sc.noteptr++;
			UPDATENT2(sc.notenr,sc.notent,sc.noteptr,bc.notenum);
		}
		UPDATENT3(sc.notenr,sc.notent,sc.noteptr,bc.notenum);

		// CHANSOLO: discard this channel's emitted MIDI bytes if it's muted.
		if (chansolo >= 0 && ch != chansolo) { mptr = solo_mptr0; laststat = solo_laststat0; }
	}

	*mptr++=0xfd;

	evtrace(m_state.cursmpl, m_state.time, m_midibuf);
	synthProcessMIDI(m_synth,m_midibuf);
	
	if (m_state.nexttime==(sU32)-1) m_state.state=PlayerState::STOPPED;
}



sBool V2MPlayer::Open(const void *a_v2mptr, sU32 a_samplerate)
///////////////////////////////////////////////////////////////
{
	if (m_base.valid) Close();
	
	m_samplerate=a_samplerate;

	if (!InitBase(a_v2mptr)) return sFALSE;

	Reset();

	return m_base.valid=sTRUE;
}



void V2MPlayer::Close()
////////////////////////
{
	if (!m_base.valid) return;
	if (m_state.state!=PlayerState::OFF) Stop();

	m_base.valid=0;
}



void V2MPlayer::Play(sU32 a_time)
//////////////////////////////////
{
	if (!m_base.valid || !m_samplerate) return;

	Stop();
	Reset();

	m_base.valid=sFALSE;
	sU32 destsmpl, cursmpl=0;
	// [validation port] signed a_time*m_samplerate/m_tpc (orig: imul/idiv).
	destsmpl = (sU32)(((sS64)(sInt)a_time * (sInt)m_samplerate) / (sInt)m_tpc);

	m_state.state=PlayerState::PLAYING;
	m_state.smpldelta=0;
	m_state.smplrem=0;
	while ((cursmpl+m_state.smpldelta)<destsmpl && m_state.state==PlayerState::PLAYING)
	{
		cursmpl+=m_state.smpldelta;
		Tick();
		if (m_state.state==PlayerState::PLAYING)
		{
			UpdateSampleDelta(m_state.nexttime,m_state.time,m_state.usecs,m_base.timediv2,&m_state.smplrem,&m_state.smpldelta);
		}
		else
			m_state.smpldelta=-1;
	}
	m_state.smpldelta-=(destsmpl-cursmpl);
	m_timeoffset=cursmpl-m_state.cursmpl;
	m_fadeval=1.0f;
	m_fadedelta=0.0f;
	m_base.valid=sTRUE;
}



void V2MPlayer::Stop(sU32 a_fadetime)
//////////////////////////////////////
{
	if (!m_base.valid) return;

	if (a_fadetime)
	{
		sU32 ftsmpls;
		// [validation port] signed a_fadetime*m_samplerate/m_tpc (orig: imul/idiv).
		ftsmpls = (sU32)(((sS64)(sInt)a_fadetime * (sInt)m_samplerate) / (sInt)m_tpc);
		m_fadedelta=m_fadeval/ftsmpls;
	}
	else
		m_state.state=PlayerState::OFF;
}





void V2MPlayer::Render(sF32 *a_buffer, sU32 a_len, sBool a_add)
/////////////////////////////////////////////////////////////////
{
	if (!a_buffer) return;

	if (m_base.valid && m_state.state==PlayerState::PLAYING)
	{
		sU32 todo=a_len;
		while (todo)
		{
			sInt torender=(todo>m_state.smpldelta)?m_state.smpldelta:todo;
			if (torender)
			{
				synthRender(m_synth,a_buffer,torender,0,a_add);
				bustap_dump(m_synth);
				a_buffer+=2*torender;
				todo-=torender;
				m_state.smpldelta-=torender;
				m_state.cursmpl+=torender;
			}
			if (!m_state.smpldelta)
			{
				Tick();
				if (m_state.state==PlayerState::PLAYING)
					UpdateSampleDelta(m_state.nexttime,m_state.time,m_state.usecs,m_base.timediv2,&m_state.smplrem,&m_state.smpldelta);
				else
					m_state.smpldelta=-1;
			}
		}
	}
	else if (m_state.state==PlayerState::OFF || !m_base.valid)
	{
    if (!a_add)
    {
      // [validation port] zero 2*a_len floats (orig: rep stosd of a_len<<1 dwords).
      memset(a_buffer, 0, (size_t)a_len * 2 * sizeof(sF32));
    }
	}
	else
	{
		synthRender(m_synth,a_buffer,a_len,0,a_add);
		m_state.cursmpl+=a_len;
	}


	if (m_fadedelta)
	{
		for (sU32 i=0; i<a_len; i++)
		{
			a_buffer[2*i]*=m_fadeval;
			a_buffer[2*i+1]*=m_fadeval;
			m_fadeval-=m_fadedelta; if (m_fadeval<0) m_fadeval=0; 
		}
		if (!m_fadeval) Stop();
	}
}


sBool V2MPlayer::IsPlaying()
{
	return m_base.valid && m_state.state==PlayerState::PLAYING;
}


#ifdef V2MPLAYER_SYNC_FUNCTIONS

sU32 V2MPlayer::CalcPositions(sS32 **a_dest)
/////////////////////////////////////////////
{
	if (!a_dest) return 0;
	if (!m_base.valid) 
	{
		*a_dest=0;
		return 0;
	}

	// step 1: ende finden
	sS32 *&dp=*a_dest;
	sU32 gnr=0;
	const sU8* gp=m_base.gptr;
	sU32 curbar=0;
	sU32 cur32th=0;
	sU32 lastevtime=0;
	sU32 pb32=32;
	sU32 usecs=500000;

	sU32 posnum=0;
	sU32 ttime, td, this32;
	sF64 curtimer=0;
	
	while (gnr<m_base.gdnum)
	{
		ttime=lastevtime+(gp[2*m_base.gdnum]<<16)+(gp[m_base.gdnum]<<8)+gp[0];
		td=ttime-lastevtime;
		this32=(td*8/m_base.timediv);
		posnum+=this32;
		lastevtime=ttime;
		pb32=gp[7*m_base.gdnum]*32/gp[8*m_base.gdnum];
		gnr++;
		gp++;
	}
	td=m_base.maxtime-lastevtime;
	this32=(td*8/m_base.timediv);
	posnum+=this32+1;
	dp=new sS32[2*posnum];
	gnr=0;
	gp=m_base.gptr;
	lastevtime=0;
	pb32=32;
  sU32 pn;
	for (pn=0; pn<posnum; pn++)
	{
		sU32 curtime=pn*m_base.timediv/8;
		if (gnr<m_base.gdnum)
		{
			ttime=lastevtime+(gp[2*m_base.gdnum+gnr]<<16)+(gp[m_base.gdnum+gnr]<<8)+gp[gnr];
			if (curtime>=ttime)
			{
				pb32=gp[7*m_base.gdnum+gnr]*32/gp[8*m_base.gdnum+gnr];
				usecs=*(sU32 *)(gp+3*m_base.gdnum+4*gnr);
				gnr++;
				lastevtime=ttime;
			}
		}
		dp[2*pn]=(sU32)curtimer;
		dp[2*pn+1]=(curbar<<16)|(cur32th<<8)|(pb32);

		cur32th++;
		if (cur32th==pb32)
		{
			cur32th=0;
			curbar++;
		}
		curtimer+=m_tpc*usecs/8000000.0;
	}
	return pn;
}

#endif