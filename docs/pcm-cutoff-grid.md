# Extreme-rate PCM cutoff interpolation

## Next revision: 1/1024 cutoff grid (unreleased)

The extreme-rate bank now uses 129 cutoff rows instead of 65, retaining the
128 taps, eight fractional phases, window and 8..10 source-advance transition.
Only the reconstruction path above 8:1 can change. Source timing, loop history,
envelopes, gains and the conventional/legacy paths retain their existing rules.

The same 240 off-grid cases described below have maximum absolute sample error
0.000129370, versus 0.000429378 for the released 1/512 grid (about 70% lower).
The acceptance bound is tightened from 0.0005 to 0.00015; the released grid
fails that bound. This is an error against the selected independent FIR model,
not a measured improvement in handset fidelity or perceived sound quality.

Bank preparation is now rate-aware: `prepareDownsamplingKernels(advance)` builds
the high-rate bank only if twice the voice's source advance exceeds 4 and the
extreme-rate bank only if it exceeds 8; the unconditional form remains available.
The banks are lazy function-local statics, so a bank not prepared early is built
on first use and output is unchanged (a cold-versus-prepared sweep is compared
bit for bit in `tools/high-rate-resample-test.cpp`). Songs whose PCM never needs
the larger banks avoid about 1.2 MB and, by the host figures above, roughly
8 ms; device effect is unmeasured.

Coverage caveat: those 240 cases use only 0.35, 0.5 and 0.7 cycles per output
sample (the last exceeds the output Nyquist). A wider host sweep (7 frequencies
up to 0.5, 14 rates, extra phases and offsets, amplitude 30000) finds the worst
error near 0.42-0.45 cycles per output sample at mid-row rates: 0.000220 with the
1/1024 grid versus 0.000539 with the released 1/512 grid (about 59% lower).
The test now also gates this sweep at 0.00025; the released grid fails it. An
independent review reported a supremum of about 2.3e-4 over all sine inputs for
rates 10..128. The 0.00015 bound and the ~70% figure describe the 240 sampled
cases only. Errors below 10:1 (64-tap and 32-tap banks) were not changed by this
revision and are about 7e-3 against the same kind of model in that review, which
is a reported, not independently re-measured, figure.

Shared immutable bank storage grows from 299520 to 594432 bytes (+294912,
288 KiB). There are no additional convolution taps, per-voice buffers or
per-sample allocations. More table rows must be initialized.

Host measurement (x86-64, GCC 13, -O3, shared machine, so noisy): constructing
the extreme-rate bank took about 2-3 ms before and 4-6 ms after (five runs each).
`tools/synth-benchmark.cpp` condition `pcm_ratio16_quality` (32 voices, the quick condition above 8:1;
the 8:1 condition was not separately attributed) had a best-of-five realtime ratio of 5.698
before and 5.666 after, a difference inside run-to-run variation. These are
host figures only; Android startup, CPU and battery effects have not been measured.

The complete high-rate resampling suite passes with ASan/UBSan, including the
existing dynamic-loop, boundary cursor, silent-loop, native-rate and near-native
checks. Android builds and physical-device listening for this revision remain
unverified. The app cache becomes `r91-pcm-grid128-v1` so existing conversions
cannot hide the new reconstruction. Public version metadata is unchanged.

## Released 1.6.8 baseline: 1/512 cutoff grid

This change refines renderer reconstruction, not handset calibration. Above
8:1 source advance the existing 128-tap PCM filter uses a cutoff table. Its
bandwidth grid was 1/256; it is now 1/512. Eight fractional phases, the 128 taps,
window, cutoff target, source positions, loop history, envelopes and gains stay
the same. The existing 8..10 transition still blends the 64- and 128-tap paths.
No AL DSP or per-song/file processing is added.

The shared extreme-rate bank grows from 152064 to 299520 bytes (+147456,
144 KiB). This is shared immutable storage, not a per-voice allocation.
Per-output convolution tap counts are unchanged. Initial table construction
has more rows; no Android startup, memory or CPU benchmark is claimed.

## Verification

The existing `tools/high-rate-resample-test.cpp` compares against an independent
double-precision normalized 128-tap FIR assembled directly from sinc/window
formulae. Its 240 off-grid cases cover eight source advances (10 through 100),
three frequencies, two waveform phases and five fractional source positions.
Maximum absolute error against that reference changed from 0.00170884 to
0.000429378. The bound is tightened from 0.003 to 0.0005; the old implementation
fails the tighter bound. These figures measure reconstruction error against
the chosen filter, not perceived quality, alias attenuation or handset fidelity.
A separate low-frequency probe at 10:1 changed RMS error from 1.03e-6 to 1.44e-6;
not every individual input has a lower error because phase interpolation and
float rounding remain. No claim of universal improvement is made.

A before/after synthetic comparison of 278640 float samples is bit-identical
for source advances .25/.8/.99/1/1.01/1.05/1.1/2/4/4.5/5/8/16/32/64,
legacy and quality modes, looped/nonlooped sources, four lengths (1/7/129/1024)
and four fractional positions. The higher exact-cutoff controls keep the old
rows; off-grid extreme advances intentionally change. No full song corpus or
handset recording was available in this environment.

The application cache uses `r91-pcm-grid64-v1` to regenerate affected WAVs.
This identifies the recovered r91 baseline plus this patch, not the missing
r97 build or recovery of its lost commits. Release candidate metadata is 1.6.8/80;
publication is tracked separately from source preparation. Existing AL diagnostics are
independent of this audio change.

The complete high-rate resampling executable passes with ASan/UBSan, including
72000 dynamic-loop samples, 608256 expanded-timeline boundary indices,
27909 silent-loop samples, 24576 native-rate/legacy samples and 48 near-native
anti-alias cases. The printed 64-vs-128-tap alias figures in that executable
compare filter lengths; they are not before/after measurements of this grid
change. Android builds and physical-device listening have not been run for
this patch.
