## 1. Build the toolkit (no extraction dir touched yet)

- [x] 1.1 Create `v2/v2m/toolkit/` package skeleton + `README.md` (purpose, the `era` CLI, import convention)
- [x] 1.2 `packers.py`: `detect_packer(exe)` by PE section signature (`aplib`/`kkrunchy`/`ruletool`/`none`); unit-check against all 13 recon binaries
- [x] 1.3 `packers.py`: `unpack(exe)` dispatch — aPLib + kkrunchy via one Unicorn stub-emulation route, `none` (identity pass-through), `ruletool` (raise named NotImplemented)
- [x] 1.4 `carve.py`: lift `findv2m3` as the canonical v2m-span parser (multi-song, non-overlapping)
- [x] 1.5 `eras.py`: lift the `erascan` const/opcode assay as one module
- [x] 1.6 `disasm.py`: lift the flybye CLI disassembler form (VA-based, base `0x400000`)
- [x] 1.7 `era` CLI: front `detect|unpack|carve|assay|disasm` over the modules
- [x] 1.8 `oracle.h`: shared C scaffold — `oracle_map_image`, fault reporter, rdtsc seed-pin helper, f32 stereo writer; per-binary VAs/sites/driving stay in the harness
- [x] 1.9 Commit the toolkit (inert until imported)
- [x] 1.10 `bufcmp.py` + `era compare`: numpy-accelerated oracle-contract primitive (max|d|/rms/divergence), superseding inline diffs and the pure-Python `v2/validate/compare.py`
- [x] 1.11 `oracle.h` tap facility: typed live reads (`oracle_u32`/`_f32_at`/`oracle_buf`/`oracle_field_*`) + env-gated tap sinks (`oracle_tap_*`) for tapping a loaded oracle at chosen positions; replaces per-harness `rd32`/casts/`getenv`+`fwrite` boilerplate
- [x] 1.12 `tap.py` + `era tap`: static typed read at a VA from an unpacked image (data-side of `era disasm`); documents the static-vs-live boundary (runtime taps stay in `oracle.h`)
- [x] 1.13 `oracle.h` signal-chain tap table: `oracle_chan_tap` + `oracle_taps_open/reset/dump/close` + `oracle_tap_add` — the open/reset/dump/close lifecycle shared, only the accumulate point per-binary (collapses the ~9× fopen/memset/fwrite/fclose in `c1_solo_probe.c`)

## 2. Migrate flybye end-to-end (cleanest; in-image player)

- [x] 2.1 Repoint flybye unpack/carve/assay/disasm to the toolkit; delete the forked scripts
- [x] 2.2 Verify: re-run unpack → byte-compare flat image to the captured `/tmp/fr013/unpacked.bin`
- [x] 2.3 Verify: re-run carve → byte-identical to committed `flybye_embedded.v2m` (and `fr022_embedded.v2m`)
- [x] 2.4 Verify: re-run assay → same const/opcode rows as `NOTES.md`
- [x] 2.5 Refit `c1_flybye_harness.c` (and `c1_flybye_solo.c`) onto `oracle.h`; recompile
- [x] 2.6 Verify: re-render → `max|d| = 0` vs the committed flybye reference `.f32`
- [x] 2.7 Commit flybye migration (own commit = own rollback point)

## 3. Migrate candytron (ported player + synth-only ABI)

- [x] 3.1 Repoint candytron unpack/carve/assay to the toolkit; delete forks (c2_unpack.c)
- [x] 3.2 Verify unpack (kkrunchy route) → byte-compare to `/tmp/candytron/unpacked.bin`
- [x] 3.3 Verify carve → byte-identical to the candytron josie carve (`/tmp`; none committed in-dir)
- [x] 3.4 Refit `c2_oracle.c`, `c2_oracle_solo.c`, `c2_render.c` onto `oracle.h`; recompile
- [x] 3.5 Verify re-render → `max|d| = 0` vs the committed candytron C2 reference `.f32`
- [x] 3.6 Commit candytron migration

## 4. Migrate fr08 (harness lives in v2/validate/)

- [ ] 4.1 Repoint fr08-extraction unpack/carve/assay/disasm to the toolkit; delete forks (note: `findv2m`, `findv2m2`, `findv2m3` all removed)
- [ ] 4.2 Verify unpack → byte-compare to the captured fr08 `unpacked.bin`
- [ ] 4.3 Verify carve → byte-identical to committed `fr08.v2m`
- [ ] 4.4 Refit `v2/validate/c1_fr08_harness.c` onto `oracle.h`; recompile
- [ ] 4.5 Verify re-render → `max|d| = 0` vs the committed fr08 663 s reference `.f32`
- [ ] 4.6 Commit fr08 migration

## 5. Close-out

- [ ] 5.1 Confirm no forked copies of `unpack`/`findv2m`/`disasm`/`erascan` remain outside `toolkit/`
- [ ] 5.2 Update `era-hunt-manifest.md` sweep plan to reference the `era` CLI instead of per-dir scripts
- [ ] 5.3 Smoke-test the full pipeline on one *unmigrated* queued binary (e.g. fr-022 party): `era detect` + `era unpack` + `era carve` + `v2dump`/`v2load` version readout — proving the toolkit handles a new binary without a dir fork
