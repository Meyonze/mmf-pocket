# Private corpus compatibility test

The compatibility corpus is private and is not part of this repository. Only aggregate results are recorded here; file names and audio are intentionally omitted.

Results are regenerated with `tools/corpus-test.cpp` after decoder changes.

| Date | App version | Files | Passed | Failed | Notes |
|---|---|---:|---:|---:|---|
| 2026-09-25 | 1.4.0-beta.2 | 382 | 321 | 61 | All MA-1/2/3/5 files parsed and initialized; 61 MA-7 Format 3 files were correctly rejected as unsupported |
| 2026-09-25 | 1.4.0-beta.3 | 382 | 321 | 61 | All 321 supported files produced finite, audible output during a 10-second probe; 61 MA-7 files remain unsupported |
| 2026-09-25 | 1.4.0-beta.6 | 382 | 321 | 61 | Audio-tail, ROM-PCM fallback level and speaker-filter changes passed a 10-second finite/audible probe for every supported file; the same 61 MA-7 files remain unsupported |
| 2026-09-26 | 1.4.0-beta.10 | 382 | 382 | 0 | Format 3 event decoding and MA-7 FM/WT folding enabled; all 61 MA-7 files and all 321 earlier-format files produced finite, audible output in a 10-second probe |
| 2026-09-28 | 1.5.2 | 5 | 5 | All available MA-1/2 files were checked on a physical device after the VMA release and HandyPhone retrigger corrections |

beta.3 also rendered the three privately attached examples to their natural end
(approximately 194 seconds total scheduled duration). This tests conversion and
termination, not similarity to a handset recording. Synthetic regression checks
cover sustain, zero rates, key-off/XOF, exponential release, neutral detune,
timebases, restart and bounded song termination.

UI review: selection is retained across stop and speaker-mode toggles; Play
re-renders the retained source using the current switch setting. Old asynchronous
conversion results are invalidated by the generation counter. Batch conversion
disables both Play and the mode switch. Android Lint/build are run; touch behaviour
has not been verified on a connected device (none available).

The corpus contains 81 tracks using the 10/20 ms timebase range that previously played 10x too fast. A two-second audio probe is rendered for each supported file; a silent intro is not counted as a decoder failure. Full-file conversion was also exercised before switching to the faster regression probe, which exposed and led to a fix for unbounded release tails. File names and audio remain private.
