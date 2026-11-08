# Rendered channel contributions

`tools/render-contribution-audit.cpp` is a host-only, read-only diagnostic for
locating software mix balance differences. It does not adjust instrument gain,
normalize output, or change application playback defaults.

Build against the native library, then run:

```sh
c++ -std=c++20 -O2 -Ithird_party/yamaha-smaf-player/src \
  tools/render-contribution-audit.cpp build/native-render/libsmaf.a \
  -o render-contribution-audit
./render-contribution-audit input.mmf 10 > contributions.json
./render-contribution-audit input.mmf 10 \
  --experimental-sine-factor=8 --experimental-sine-math=hermite \
  > contributions-8x.json
```

The default is the native source policy. Explicit factors are1,2,4,8,16,32;
math is `exact` or `hermite` and requires a factor. Factor1 gives delayed native
output. Selected policies have72frames of physical latency at44,100Hz. The
optional time cap counts physical output, including latency and tails; default
600seconds, maximum600seconds plus selected latency. `finished` and
`cap_reached` distinguish full playback from a bounded observation. Input must
be12bytes to16MiB. JSON does not export input paths, titles or authors.

## Reading the report

Score parts group by global channel0–127. Different MTR tracks or program changes
using the same channel are aggregated. Independent ATR playback has a separate
`atr` domain grouped by ATR track/channel, even if its virtual channel number
matches a score channel. These buckets are not individual instrument identities.

Each part contains three stereo measurements:

- `before_output_filter`: input to the existing output low-pass. With an explicit
  quality policy this is **after** that part's FIR/native FIFO alignment.
- `after_output_filter`: that part's contribution through the two-pole low-pass.
- `with_shared_output_gain`: the filtered contribution multiplied by the actual
  full mix limiter gain and hard-end fade at each physical frame.

Each stage reports peak, RMS, AC RMS and left/right DC. RMS uses the entire
observed physical interval, including initial silence and selected latency.
This is conditional contribution under the real mix gain, not a hypothetical
solo playback. Phase cancellation and reinforcement mean component powers do
not add to mix power. A quiet mix may contain loud opposing parts. It is not
perceptual loudness, LUFS, or an Android speaker/phone-EQ measurement.

`voice_frames` counts actual native source ticks by FM, keyed PCM, recorded
Audio, and independent ATR route. It counts active synthesis, including exact
zero gain; it is not audible polyphony or hardware voice usage. FIR drain frames
do not create source ticks. `sine_source_voice_frames` is a **subset** of FM
voice frames routed through the experimental subframe API, not an additional
voice category. `peak_native_sources` is also measured before quality tails.

`trigger_diagnostics` attributes existing engine counter increments to the
requesting bucket: FM, PCM, fallback, Audio, missing Audio, and explicitly
suppressed silent notes. These retain the engine's overlapping definitions:
Audio also increments `pcm_notes`, and `fallback_notes` does not describe every
missing-ROM or wavetable substitution. They are clues, not a complete inventory
of original versus substituted timbres. Authored note/ATR trigger counts remain
separate from successful synthesis counts.

## Correctness and limits

The diagnostic copies active voices and ticks the copies. An independent shadow
sum preserves the renderer's exact original source summation order and checks
filter state and shared gain/fade bit-for-bit. Every observed output sample is
also checked against a separate uninstrumented player rendered in blocks.
`reference_pcm_exact` is emitted only after these checks succeed. The per-part
sum has normal floating-point grouping error, reported separately as
`component_sum_error_peak` and `component_sum_error_rms` (guarded below2e-5).

This duplicates synthesis and maintains per-part filters; it is not a runtime
CPU or battery benchmark. It uses the software player with
`suppressHpsSlapbackDuplicates=false`, without app-specific phone processing.
A change in contribution does not by itself establish hardware fidelity or
justify compensating gain. Compare the same physical interval and source policy
before interpreting changes; inspect source route and trigger diagnostics too.

Validation includes analytical1:2 DC sources, coherent cancellation,32-source
shared limiting, independent ATR/score channel collisions, native block sizes
1/17/512/1024, hard boundaries, future event exclusion, missing Audio/fallback/
immutable silence attribution, late-created parts, and final-frame tiny tails.
512 FM graph/wave/profile cases and16 mixed quality cases pass Release,
ASan/UBSan and NDK Clang targeting the Linux host (not Android execution).
Six CLI checks cover valid native/quality fixtures, metadata canaries, unchanged
input, caps and invalid arguments. Across2,121 encoded native inputs, output
matches the uninstrumented renderer exactly, all six per-part trigger counts
sum to engine totals, and maximum component residual is1.49011611938e-8.
An additional28 encoded-input runs cover seven observed source combinations
(FM, PCM, Audio, ATR, ATR+FM, ATR+PCM, and silence), each with factor1/exact,
2/exact,8/Hermite and32/Hermite. Reference samples and attributed trigger totals
match in every run; component sums are exact for this representative subset.

## Adaptive app policy

Use `--app-quality` to measure the current8x Hermite policy with selective16x
voices, including pitch changes and retained8x/16x filter tails:

```sh
./render-contribution-audit input.mmf 10 --app-quality > app-contributions.json
```

This flag is mutually exclusive with explicit factor/math flags. `sine_factor`
is the baseline8, and `boost_high_notes` is true. Per-part
`sine_voice_frames_by_factor` contains disjoint native voice-frame counts for
2/4/8/16/32. Their sum equals `sine_source_voice_frames`.
`boosted_sine_voice_frames` counts only selectively raised16x frames. A fixed16x
report therefore has16x voice frames but zero boosted frames. Filter drains do
not add source counts. Zero-gain live voices can count; these are not audible
voice counts or CPU measurements.

Each part now carries both aligned FIR histories. The global shadow retains the
original source summation order; actual output still matches an independent
uninstrumented player bit-for-bit. Late parts have an implicit zero prefix.
Mixing8x and16x within one channel, raising pitch and lowering it again, new
notes, source stops, tiny final-frame tails and shared limiting are tested.
56 adaptive mixed cases cover graphs0/5/7, the stopped-attack reduced forms
of graphs3/4/6 and r90 weak VM35 nested6, Exact/Hermite and four block sizes.
Fixed-density mixed coverage also includes56 cases. See [reduced-graph eligibility](fm-reduced-graphs.md)
and [weak nested policy](fm-weak-nested-r90.md). The historical `boost_high_notes`
field keeps its name; the app policy also selects16x at every pitch for weak nested6.
Six CLI checks include metadata exclusion and a high-note match to fixed16x.
Twelve legacy controls preserve all old report fields and values exactly;
additional metadata is additive under schema1.

`--app-quality` matches the synthesis density policy. The existing
`suppressHpsSlapbackDuplicates=false` setting and absence of app phone EQ still
apply. This is not a complete app post-processing or handset-fidelity study.
See [adaptive policy and validation](fm-adaptive-density.md).
