// Stubs linked ONLY into harness_asm.
//
// The shared player calls synthSetLyrics() unconditionally (Reset()). The C++
// core defines it as a no-op (synth_core.cpp), so harness_cpp resolves it
// there. The RONAN-disabled asm core does not export it, so we supply the same
// no-op here. Result: lyrics are a no-op on BOTH sides — identical behavior,
// correct for the RONAN-off / non-speech baseline.
extern "C" void __attribute__((stdcall)) synthSetLyrics(void *, const char **) {}
