# Extreme-rate PCM cutoff interpolation

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
