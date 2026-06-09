# v2m/embedded — genuine songs carved from the demo binaries

The GROUND TRUTH corpus: each file is the V2M **embedded in its demo binary**,
carved with `toolkit/era carve <unpacked.bin> --extract 0`, at its NATIVE format
version. These replace the removed v6-converted corpus (a converted V2M is never
a basis for any conclusion — USER RULING). The packed release binaries stay out
of the repo (copyright); these are reproducible from `~/downloads/<zip>` via
`toolkit/era unpack` + `era carve`.

| file | era | demo | carve | source release (sha256) | v2m sha256 |
| --- | --- | --- | --- | --- | --- |
| `fr08.v2m` | v0 | fr-08 .the .product (final) | 0 | `fr08_final.zip` `12a3a2099555…` | `36e0546d709cca86…` |
| `flybye.v2m` | v1 | fr-013 flybye (unofficial) | 0 | `fr013_unoff.zip` `60b203067426…` | `1ebaf51d6e71ad44…` |
| `fr014.v2m` | v3 | fr-014 mark&sweep / garbage collection | 0 | `fr014.zip` `ea7f2d3a506e…` | `d505ce35fd29528f…` |
| `fr019.v2m` | v4 | fr-019 poem to a horse (ms2002 party) | 0 | `fr019_party.zip` `1fa7e27a8989…` | `9c7e7e07bead2067…` |
| `candytron.v2m` | v5 | fr-030 candytron (final) | 0 | `fr-030_candytron_final.zip` `3e1b7ae4577c…` | `5bf17fb8d58d608c…` |
