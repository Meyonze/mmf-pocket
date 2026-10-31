# FM waveforms 7–13: reference disagreement

The current renderer, two published software models and a YMF825 board chart
do not provide a single established waveform formula. The renderer remains at
cache r59; this investigation adds comparison inputs and records the uncertainty.
It does not select a new synthesis curve.

## Sources

| Reference | Pinned source | What it establishes |
| --- | --- | --- |
| YMF825 board documentation | [junk16/ymf825board, 3154d5b](https://github.com/junk16/ymf825board/blob/3154d5b043940a29af8f2389912cc9c30f0a76d6/manual/fbd_spec3.md#ws-fb) | Qualitative shape chart and waveform selectors; not numerical curvature |
| SiON | [SiOPMTable.as, 1e6d6cd](https://github.com/keim/SiON/blob/1e6d6cd20bbc0379f5a81f607ac87a105163648f/src/org/si/sion/module/SiOPMTable.as) | A software table construction with logarithmic quantization and half-bin sine sampling |
| fmfm.core | [data.go, dc06f90](https://github.com/but80/fmfm.core/blob/dc06f908f796ee07e5716a2dea41aede8a63508e/ymf/ymfdata/data.go) | A different software model, described by its author as work in progress and mostly based on doomjs/opl3 |

These are implementation references, not recordings from a MA-series handset.
The chart is public board documentation; it does not establish measured samples
or a mathematical manufacturer specification. External source code, tables and
the board chart are not copied into the app or comparison pack.

## Waveform 8

Let `x` be normalized phase in `[0,1)`.

| Model | Continuous description | Value at `x=1/16` |
| --- | --- | ---: |
| Current | `clamp(1.7 sin(2πx), -1, 1)` | 0.650562 |
| fmfm | `clamp(√2 sin(2πx), -1, 1)` | 0.541196 |
| Idealized SiON | `sin(2πq(x))`, with the phase mapping below | 0.707107 |

For the idealized SiON mapping, `q=2x` below1/8, `q=1/4` from1/8 to3/8,
`q=2x−1/2` from3/8 to5/8, `q=3/4` from5/8 to7/8, and `q=2x−1` thereafter.
This is a continuous interpretation, not an exact copy of its sampled/log table.

The latter two models place plateaus at1/8–3/8 and5/8–7/8, but disagree on
their transition curves and slopes. The current1.7 clipping gives wider
plateaus. A qualitative chart cannot determine which complete model to adopt.
Waveforms9–13 apply related gating/repetition operations, so the uncertainty
affects that family too.

## Waveform 7

The current curve is a signed cubic. SiON's table has an exactly silent middle
half-cycle, with idealized `1−sin(2πx)` and `−1−sin(2πx)` edge lobes. fmfm
instead uses exponential tails and has no exact silent interval. On its1024-bin
edge grid the positive exponent is `−n/16`, equivalent to `−64x`; the negative
tail is `−2^(−64(1−x))`. This is not an exponent of16 per cycle. The board chart
cannot establish exact central silence or choose between these formulas.

## Added comparison inputs

`tools/generate-fm-probes.py` now creates26 original FM files. The new
`waveform-7.mmf` through `waveform-13.mmf` use a silent AR0 modulator and one
carrier, changing only its selector from the existing sine fixture. Timing,
gain, bank/program, note and gate stay the same. The earlier19 inputs retain
their exact bytes; do not replace their captured recordings.

All26 pass eight generator checks with both the native parser/render checker
and pinned independent go-smaf parser enabled. Validation covers fields,
events, timing, complete container lengths/CRC and native audible binding.
It does not validate hardware waveform accuracy or acceptance by Galamelo.

The investigation package includes a self-authored PNG/SVG curve comparison,
CSV values and its plotting script. They show continuous analytic models, not
measured audio or exact quantized tables. The expanded full comparison pack
contains115 original MMFs with r59 candidate WAVs. Earlier108 inputs and their
WAVs are preserved; no new APK or cache revision is needed.

An eventual reference comparison should use these same input files and retain
the raw recording and manifest. Compare a stable held-note region as well as
the onset, and keep timing/level differences visible. A curve change needs
better primary documentation or a discriminating reference capture; software
agreement alone does not establish handset fidelity.
