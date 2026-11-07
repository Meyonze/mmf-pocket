# Certified zero PCM reconstruction

The native keyed-PCM renderer now avoids quality reconstruction only when the
existing allocation-time `zeroSource` certificate proves that **every decoded
sample in the exclusive playback range** is exactly zero. The certificate uses
the immutable decoded source and clamped playback endpoint. Nonzero data beyond
that endpoint is irrelevant to reconstruction.

The normal linear sample lookup, source position, loop history, vibrato and
amplitude LFO, envelope/Hold/release, controller gain, and `recentLevel` update
still run. The linear and FIR paths both yield positive zero for this certified
source, so final float sample bits and voice timing are retained. This is not a
volume threshold or a new silence/voice-reclamation policy. A zero loop after a
nonzero attack is ineligible; so is any single nonzero PCM sample, even±1.
Recorded Audio and independent ATR retain their existing paths, which do not
set this certificate. Existing filter-table preparation remains unchanged.

`tools/pcm-zero-source-test.cpp` compares the same voice with and without its
certificate.528conditions/405,504sample pairs cover legacy/modern profiles,
short/long sources, loops, fractional positions,11rate choices from0.25to12,
bend and gain changes, LFO, Hold/release, and slot reuse. Samples compare by bits;
source/loop/LFO clocks, envelope state and allocation-level history compare
exactly. Negative controls have a±1attack or loop sample. Nonzero decoded data
outside the exclusive endpoint is also covered. `--bench` is a host-only cost
probe, not an Android performance measurement.

All2,121 existing encoded-input WAVs remain byte-identical. ASan/UBSan checks
include the new differential test, silent voice allocation, high-rate PCM,
recorded Audio, ATR, PCM16 bridge, player input protection, and native/experimental
channel contribution diagnostics. No cache revision or application version bump
is required because rendered samples are unchanged.

In nine alternating before/after host trials (32 looping voices,8,192frames),
the certified2x case median fell from23.12ms to3.27ms, and12x from50.51ms to
3.71ms. The uncertified controls retained their full reconstruction path;
measured ratios varied from0.91to1.03 on the shared host. These are synthetic
all-zero workloads, not whole-song savings or Android measurements. Android
Debug/Release builds for all configured ABIs and lint also pass locally.
