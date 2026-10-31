# DSP review evidence

These checks use original synthetic signals and installed tools as black-box
references. They establish bounded software behaviour, not handset calibration
or perceptual equivalence. No external decoder implementation or music was used.

## FM waveform-reference disagreement and capture coverage (r59)

Published SiON and fmfm.core models disagree on WS7 and WS8 curvature. SiON's
WS8 idealization phase-warps sine with eighth-cycle plateaus; fmfm clips sine
at a sqrt2 gain. The current1.7 clipping differs from both. WS7 ranges from
current signed cubic to SiON's partly silent curve and fmfm exponential tails.
The published YMF825 board chart is qualitative; it cannot select a complete
formula. No DSP change is justified from this evidence alone.

Add original WS7–13 carrier probes, expanding FM coverage19→26 while pinning
every earlier MMF hash. All26 pass eight generator checks with native and
independent go-smaf parsers enabled, including CRC, nested structure, exact
events/voice fields and audible native binding. All19 earlier candidate WAVs
remain byte-identical. The full comparison pack has115 inputs; every earlier
108 input and candidate WAV retains its bytes. App/DSP, APK and cache remain
r59; no full native/Android rebuild is claimed for this tooling-only change.

The fresh independent parser uses commit545503dc3bc1fa7253b17c9739b154053b8cfd82,
Go1.27.3 from a release-manifest SHA256-verified SDK, and verified module sums.
Temporary parser source/SDK are not app dependencies or vendored product code.
The self-authored curve figure and CSV show continuous analytic models rather
than sampled/log-quantized reference tables or hardware audio. See
[the investigation](fm-waveform-investigation.md) for pinned source links,
formulas, limits and the new original inputs. No vendor table/chart is packaged.

## Follow-up state audit and rejected WT LFO shortcut (r59)

A separate original ASan/UBSan probe compares32 family/rate pairs: FM, Mobile
recorded Audio, MA7 recorded Audio and ATR, at8/44.1kHz, across previous/current
families. Partial-play reinit, full seek and partial-play seek match a fresh
player's float PCM exactly with blocks1/31/257/1024. No new state bug was found.
This bounded result does not establish every possible reuse combination.

The next experiment kept WT LFO phase advancing but skipped sine-table lookup
while effective vibrato and AM depths were both zero. Two guard orderings were
tested only in frozen prototypes. For each ordering, GCC14.2 and NDK Clang18
targeting x86_64 Linux each pass240 scenarios and6,242,160 bitwise sample/state
checks, including repeated OFF/ON periods, phase wraps, AM and negative zero.
The first ordering also renders120 complete MMFs byte-identically, including
the previous108 and12 new RAM-WT modulation controls. The second ordering's
evidence is the sample/state prototype, not a separate full-MMF comparison.

Both variants are rejected on warm paired host timings. The first guard slows
Clang unity-rate AM by6.38% (8/9 pairs); checking AM first still slows GCC
unity-rate EVB by5.70% and Clang fractional EVB by10.26%. These are bounded
observations, not universal slowdowns: control variation also exposes host and
code-placement noise. Nine alternating before/after pairs use32 WT voices,
48,000 frames per measurement, pinned to an allowed CPU. Dormant-path gains
do not meet the no-active-regression acceptance condition.

The Clang cold measurement moves approximately53us of table initialization
from the first OFF sample to the first ON sample; samples and phase match.
Cold single-call overhead is measured separately from warm throughput. No
Android execution, speed/power or hardware-calibration claim is made.
Production code, permanent tests, APK and render-cache revision remain r59.
Raw measurements, frozen sources, the32-pair proof and new original controls
are retained in the follow-up audit archive; no optimization is adopted.

## Mobile CC1 source-grammar isolation (r59)

Mobile CC1 was correctly audited as Unapplied but still scheduled as a generic
Modulation event. With distinct Mobile MTR0 and MA7 MTR1 sharing channel0, MA7
initialization could leave `squaredControllers=true`. The Mobile event then
borrowed MA7's1/2/4/8 vibrato law. A200ms instruction changed9,644 float samples
despite the Unapplied audit. The same Mobile sequence alone did not change.

The routing table now emits a timed ScoreNop for Mobile CC1. It retains the
same audit and timestamp, including final control-only rests. MA7 CC1 and HPS
octave markers retain their existing routing. No new Mobile modulation curve
is inferred; shared-channel initialization policy is otherwise unchanged.
Cache r59 invalidates affected conversions.

The original18-case Astra proof covers pure/mixed tracks in both orders,
same/other channels and before/live/EOF controls. The permanent regression
`tools/mobile-modulation-isolation-regression.h` adds embedded WT and compressed
Mobile format1, giving72 combinations with blocks1/257/1024, seek, complete
decode/audit checks and intrinsic EVB protection. It fails against r58 objects
and passes with the fix under ASan/UBSan.

All82 earlier candidate WAVs remain byte-identical.26 new complete MMFs have
independent bitwise CRC and chunk-boundary checks, including mixed format1/2,
track order/channel/endpoint controls and genuine MA7 FM/WT depth controls.
Of108 total inputs, only8 change; all8 corrected outputs match their timed
NOP controls.11 total control pairs match, and100 candidates are byte-identical.
The held-note cases preserve every PCM16 sample before the authored200ms event.
Valid MA7 FM and WT CC1 still produce distinct OFF/max-depth outputs.

All ten native ASan/UBSan suites pass against the rebuilt player object; three
unchanged r58 core objects are reused after source/header identity checks.
The earlier audio attempt was deliberately interrupted to strengthen physicalEOF
coverage. Nine completed suites remain valid; audio was recompiled and passed
with the final fixture. The diagnostic CLI8+4 checks and self-test pass.
As in r58, LeakSanitizer is disabled because of sandbox tracing; no leak-clean
claim is made. Android lint/Debug/Release pass for all three ABIs (0 errors,
4 existing lint warnings), and debug APKv2 verifies. No new device execution,
reference listening, GitHub Actions run or publication was performed.

## Exact-zero FM output at EOF (r58)

An XOF carrier can remain active while its existing Nyquist guard returns zero
or waveform15/23/31 selects unbound user SRAM. Unlike stopped attacks, these
cases were not recognized by the EOF query and retained the ten-second safety
tail. Original complete MMFs reproduce10.4 seconds for a400ms silent score.

`FmOperator::muted()` now reports exact zero output with current controls;
`FmVoice::muted()` combines only its output carriers. The envelope-only
`silentForCurrentNote()` contract is preserved. The new query changes no phase,
envelope, feedback, slot occupancy or in-score processing. It is used only at
EOF, after every authored event and score endpoint; the four shared LP histories
still drain under the existing rule. Cache r58 invalidates affected conversions.

This relies on the current finite parsed-player state. Pitch setters can clear
Nyquist suppression, so it is not a permanent per-note certificate. Vibrato
changes local advancement but not the existing guard. Modern WS30 remains
audible; masked15/23/31 remain unconditional zero. This does not implement SRAM
binding or change the frequency guard, and makes no hardware fidelity claim.

The permanent `tools/fm-zero-output-regression.h` fails against saved r57 core
objects and passes with the change under ASan/UBSan. It tests all eight valid
topologies, both profiles, all256 selectors, isolated operators against actual
generated output, every live carrier, tiny gains, feedback, pitch revival,
vibrato, later events, rests, seek and render blocks1/257/1024. A final bend
into suppression matches an independently authored All Sound Off score through
the shared filter drain. The pre-existing inconsistent public patch case
`fourOp=false, algorithm>=2` is outside the parser-supported topology contract.

Full validation rebuilt all four sanitized core objects and freshly linked and
ran all ten native suites. The diagnostic CLI8+4 checks and self-test passed.
ASan/UBSan remained enabled; LeakSanitizer was disabled because the sandbox's
tracing prevents its thread inspection. No leak-clean result is claimed.
Android lint/Debug/Release passed for all three ABIs, with0 errors and4 existing
lint warnings; debug APKv2 signature verified. Gradle's first offline attempt
was blocked by sandbox-local sockets; approved execution outside the sandbox
succeeded, still offline. No new device execution or timing claim is made.

All63 earlier MMF candidate WAVs remain byte-identical. Nineteen new complete
wire inputs have verified chunk lengths, CRCs and hashes. Of82 total, only13
shorten, retaining every PCM16 sample and removing only zero PCM16; all13 equal
independently authored controls. The other69 are byte-identical, including
two pitch-bend/controller-reset revival controls. Native route reports agree
before/after:26 bound-FM routes, no fallback or overflow across19 summaries.

## MA7 incomplete operator images (r57)

MA7 FM images selected their supplied operator count by exact image length,
while the algorithm independently selected how many operators the core executes.
Short FM (24-byte register image) and FM+AL (40-byte image) contain two operator
records. Algorithms2..7 require four, but these short images were accepted and
the missing operators inherited default patches. Such an initial definition
could mask a later complete setup voice; an inline one could replace a working
voice. The defect alters timbre and release rather than causing an out-of-bounds
read.

Reject only when required operators exceed supplied records. Complete larger
images with unused registers (algorithms0/1 in a four-record body) keep their
previous acceptance. MA3/MA5/VMA, PCM and Analog Lite DSP are unchanged. Cache
r57 invalidates existing conversions of affected malformed inputs.

The Astra reproducer covers algorithms2..7 with/without AL: all12 incomplete
images were accepted before the fix and changed output relative to a complete
voice, including setup and inline masking. At8kHz the original inline fixture
grew from2676 to5901 frames with peak sample differences0.0606..0.1223; these
are bounded synthetic changes, not an audibility or hardware-quality score.
`tools/ma7-incomplete-fm-regression.h` fails against r56 and passes the corrected
ASan/UBSan parser. It covers setup order, inline definitions, standalone fallback,
all required algorithms, complete two/four-record compatibility, block and seek.

Nine complete original MMFs add six malformed-definition cases and three
controls: plain/AL missing-only, malformed-before-valid, and inline replacement.
All63 comparison MMFs render with default GCC O3. Only six affected WAVs change;
the other57 retain their bytes, including every earlier54 candidate. Each fixed
malformed case equals its independently authored control with no malformed
definition. This demonstrates software rejection/voice preservation without
claiming reference-player acceptance or new MA7 synthesis support.

## Recorded-Audio start order (r56)

The separate two-slot recorded-Audio pool chooses the oldest source when full.
It previously used only the sample cursor as its age. Four successful starts at
one timestamp repeatedly replaced slot0, retaining the second and fourth source
instead of the third and fourth. Even three simultaneous starts can leave the
wrong age ordering for the next later trigger.

Use a successful-start serial, reset on init and seek. The decoded-event ceiling
bounds the serial, and ignored missing/muted triggers never allocate. No
per-sample work or additional sample storage is introduced. This corrects the
existing oldest-first policy; hardware allocation semantics remain unmeasured.
The two slots, source-track binding, gate identities, gain, resampling and voice
recipes are unchanged. Cache r56 invalidates old rendered output.

An Astra review supplied an original reproducer: all16 same-timestamp cases
(Mobile/MA7,4/5/6/8 triggers, single/multiple MTRs) fail before the fix and
match the independently authored last-two score afterward, including block and
seek replay. Baseline output for four triggers matches the wrong second/fourth
pair exactly; peak before/reference differences range0.0105..0.0174 full scale
for these low-level synthetic inputs. This is not a perceptual quality metric.

`tools/recorded-audio-fifo-regression.h` adds permanent waveform checks for
simultaneous starts, playback order differing from track decode order, stale
gates, free-slot reuse, ordinary ascending starts, muted/missing input, init,
seek and render blocks. Explicit retained-source spans are independently mixed
into one integer-PCM recording for the ordinary/free-slot prefix controls,
bypassing pool allocation. Its targeted ASan/UBSan harness passes.

Twelve complete original MMFs add six overlap/last-two pairs (Mobile channel0,
MA7 channel16;4/5/8 starts), with original unsigned PCM8 waves, lengths and CRCs.
Default GCC O3 rendering of all54 original comparison inputs changes only the
six overlap inputs; the other48 WAVs are byte-identical to r55. All six corrected
outputs equal their last-two control WAV byte-for-byte. The earlier42 candidate
WAV bytes are preserved. No handset/reference recording or Android timing/power
claim follows from these software tests.

## Exact half/quarter-period FM folding (r54 follow-up)

Wave selectors 19/22/26/27/29 used floating remainder with period 0.5 or 0.25
after phase normalization. Within a half/quarter interval, subtraction by that
dyadic period is exact. The half-period helper now returns the first interval,
subtracts in the second, and keeps `fmod` for the rounded endpoint 1 and NaN.
The quarter-period waveform only needs the first half of the cycle; the rest
is still literal zero. Selector aliases, modern WS30 and other shapes stay intact.

Exact positive-zero endpoints use equality checks. GCC can remove `fabs` or
`copysign` here from range information under production flags, changing zero
signs with downward rounding; an explicit half/quarter equality preserves them.
The permanent phase suite checks 2,880 exact dyadic outputs and 960 nonfinite
classifications across raw aliases, both profiles and four rounding modes.
Integer-normalization zero signs under downward rounding are compiler-dependent
in the baseline and are not prescribed by that table; quarter/half endpoint
signs are checked. Baseline and final suites pass with GCC O1/O3, GCC ASan/UBSan
and Clang O1/O3. The raw-subtraction and range-eliminated `fabs` candidates fail
the endpoint checks and are not adopted. Android code generation for all three
ABIs retains the explicit endpoint selection and rare half-period fallback.

A separate normalized-phase audit covers 1,024,599 original bit-pattern and
boundary inputs per rounding mode. Its 16,393,584 helper comparisons across
GCC/Clang used rounding-aware compilation; actual production-flag caller checks
are also required, as the eliminated-`fabs` case demonstrates. This establishes
sample/sign behavior within the tested contract, not arbitrary `errno`, NaN
payload or floating-exception-environment equivalence.

Four host compiler configurations (GCC/Clang O3 and GCC/Clang with AVX2/FMA,
Clang using `-ffp-contract=fast`) directly compare before/after binary streams.
Each retains all 4,298,240 operator samples and 6,144,000 voice samples plus
recent-level/active/released/muted state byte-for-byte. Compiler configurations
can have different fingerprints from one another; equality is within each pair.
Clang18 wrapped-call instrumentation checks 512 streams of 8,192 samples: each
affected selector/alias/profile goes from 8,192 `fmod` calls to zero during
normal positive phase progression, with matching output hashes. Other streams
retain zero calls. This is call counting, not Android timing.
GCC instead inlines the finite remainder as x87 `fprem`, so wrapped library
call counting does not measure its normal remainder work.

A CPU-pinned, single-process Clang18 host benchmark uses 32 actual FM voices,
72 conditions, 512 warmup frames and 4,096 measured frames, with five ABBA groups
per condition. All 1,440 sample-bit hashes match. Median baseline/final ratios
aggregated by waveform are 1.24/1.13/1.21/1.24/1.46 for 19/22/26/27/29;
the sine control is 0.99. Some condition medians and individual runs regress
amid substantial host noise. These are synthesis measurements, with no Android
underrun, latency, power or hardware-timbre claim. Render cache remains r54.

The separate one-wrap PCM shortcut was not adopted. Although its 946,176-record
traces match under three compiler profiles, paired timing gains are comparable
to control noise and one multiple-wrap fallback condition regresses about 9%.
The validated PCM loop path is retained.

## Long-filter coefficient preparation (r54)

Only the 64/128-tap kernels now prepare interpolated weights in a separate
stack array before the original ordered source sum. The formula, tables,
transition blends, source index resolution and voice state are retained.
Each invocation uses at most 512 extra stack bytes, with no heap allocation
or persistent per-voice cache. The existing 32-tap path stays together: splitting
that path can scalarize accumulation in the Android compiler.

Separate loops can change compiler FMA contraction. An Astra audit found that
retaining source order alone does not guarantee bit-exact results; r54 therefore
refreshes render caches and makes no general exact-output claim.

Two compiler configurations each exercise 600,000 direct outputs per bank
(1.2 million per compiler). Original inputs cover full-scale constants,
alternating signs, impulses, tiny samples, deterministic random values,
coefficient-sign adversaries, cancellation, phase/cutoff endpoints and valid
loop histories. GCC O3 with AVX2/FMA differs by at most 3.5762786865e-7 full
scale, or 0.01171875 of a normalized PCM16 step. Clang18 host O3 with FMA and
`-ffp-contract=fast` matches in these direct-function contexts.

Against long-double bilinear evaluation of the stored rows, maximum errors
are 8.5094e-7/9.5921e-7 before and 7.3173e-7/9.5231e-7 after for 64/128 taps
under GCC/FMA. These observed maxima are not a universal error bound or a
claim that the new calculation always rounds more accurately.

A conservative calculation using measured host row L1 norms and ordinary
float error bounds gives pair differences below 1.312e-5/2.513e-5 full scale
(0.430/0.824 PCM16 step). It assumes ordered accumulation, finite normalized
input and no fast-math reassociation, and concerns a single direct FIR output.
It excludes later crossfades, mixing and PCM16 quantization. The much smaller
observed 0.012-step difference must not be presented as the universal maximum.

An independent continuous normalized sinc/Blackman long-double oracle checks
6,186 adversarial cases per bank. Before/after maximum errors are equal:
0.01298696 for 64 taps and 0.003488767 for 128 taps. These larger numbers are
existing table approximation on full-scale inputs, not the added rounding
difference; they also have a broader signal scope than the earlier sine tests.

Actual `PcmVoice` traces cover 946,176 frames per compiler profile. GCC and
Clang default O3 before/after traces match byte-for-byte. GCC AVX2/FMA changes
97,086 sample values by at most 1.0430813e-7 (about 0.00342 PCM16 step), while
recent level, position, LFO phase, loop travel, active and fade state remain
byte-identical. This preserves the measured allocation/control state without
assuming sample equality under every compiler or ABI.

A single-process Clang18 host O3 benchmark pins one allowed CPU and uses 32
actual PCM voices, 512 warmup frames and 8,192 measured frames per run. Twelve
conditions each have five ABBA groups (240 runs). Fixed-rate 6/8/9/16 interior
median baseline/candidate ratios are 2.22/1.84/2.00/2.22, with short-loop ratios
1.38/1.22/1.39/1.24. Dynamic cases are 1.39 interior and 1.11 short-loop.
The unchanged 4:1 controls also vary, showing substantial host timing noise;
some individual runs regress. No Android performance or battery claim follows.

Default GCC O3 CLI rendering of 22 real MMF containers matches the previous
engine byte-for-byte: fourteen original FM scores, five audio-track inputs,
and three new original MA5 RAM-WT workloads. The WT cases exercise advances
about 5.80, 17.39 and a controller sweep from 2.90 through 11.61 at 44.1 kHz.
Native inspection verifies one PCM instrument, no FM fallback, intended looping
and quality interpolation, finite audio and natural completion. The independent
go-smaf parser checks container CRC/events but does not implement these PCM
voice/bulk formats; it is not a waveform or voice-field oracle for these cases.

GCC AVX2/FMA CLI rendering demonstrates why the cache revision matters: the
high128 and changing-rate WT files each change four of 264,600 interleaved PCM16
sample values by one integer unit; the high64 file stays exact. All three keep
their frame counts. Small float differences can cross a quantization boundary,
so a sub-step measured float error does not imply identical PCM16 output.

## Portable device synthesis workload

`tools/synth-benchmark.cpp` exercises production `FmVoice::tick()` and
`MaPlayer::PcmVoice::tick()` with original generated inputs. Its 22 cases cover
32 simultaneous voices at the app's 44,100 Hz, FM selectors 0/6/16/24,
algorithms 0/7, AM/VIB off/both, and PCM ratios 4/8/16 with linear/quality
interpolation. Algorithm 0 consumes two operators; algorithm 7 consumes four.
The PCM workload deliberately loops synthetic recorded-voice cursors to sustain
the load; it does not model the number of recorded-Audio hardware slots.

Each case warms 512 frames, then measures 98,304 frames (4,096 with `--quick`)
in blocks of 1,024. Setup time includes voice initialization and warmup, including
first-use shared table work. Steady wall time, maximum block time, block budget
overruns and an output checksum are emitted as JSON. The timing includes output
validation/checksum overhead and excludes parsing, player mix/output filters,
PCM16, WAV I/O, AudioTrack and UI. The checksum is not a timbre metric or an
ABI-independent oracle. Scheduling delays can affect wall and block timings.

Host quick/default runs and a quick ASan/UBSan run pass all 22 cases. Repeated
host quick runs produce identical output checksums. Malformed/duplicate CLI
options and output disk-full errors fail explicitly. The build helper produces
Android API 29 executables for arm64-v8a, armeabi-v7a and x86_64, using the app's
NDK 27.2.12479018 and statically linked C++ runtime. ELF inspection confirms
there is no dependency on a separately supplied `libc++_shared.so`.
No Android device execution, audio underrun, latency or power measurement has
been performed. See [device instructions](device-audio-benchmark.md).

## Additional pitch and controller audit

A bounded original probe compares changing bend sensitivity against a separately
authored fixed-sensitivity score with equivalent raw bend positions. Under
ASan/UBSan, all 18 FM/WT cases match exactly: 506,880 interleaved L/R sample
values, including positive/negative bends, CC121, Hold/release tails and unrelated
channel controls through global channel 127. Blocks 1/257/1024 and seek replay
also match. Existing focused MA7 range, Mobile RPN and controller-reset checks
pass. This found no additional defect and made no production change. It does
not resolve portamento support or reference-dependent FM envelope rate latching.
The additional probe is retained with the local validation evidence.

## Full-byte HPS/VMA banks

HPS Bank events already admit all 256 byte values. The VMA definition parser
and event application now retain bit 7 when constructing and selecting a voice
key, preventing bank 0 and bank 128 from colliding. The existing drum flag is
still derived from bit 7; no routing precedence changed. Original fixtures in
`tools/vma-bank-regression-test.cpp` compare bank selection against independently
assigned programs and reversed definition order. They cover all bank bytes,
multiple program numbers, wrong-bank fallback, live switches, inline isolation,
seek and block sizes. Invalid high-bit Mobile/MA7 controllers remain rejected.
The independently pinned go-smaf parser also reports the original full bank
bytes in definitions and HPS events; it is structural evidence, not audio output.

## Provably silent attack tails

Original embedded-FM fixtures with every output carrier stopped at zero attack
and XOF set render 81,600 frames at 8 kHz before the correction: the 1,600-frame
authored score plus the 80,000-frame safety allowance, all samples exactly zero.
The same fixtures with the existing zero-volume EOF contract render just the
1,600 authored frames. A later audible note remains audible but previously also
inherited the unnecessary safety tail from its silent companion.

The follow-up extends the EOF-only mute query to envelope states that cannot
produce later output for the current note: finished envelopes, or configured
attack step and current level both exactly zero. It uses configured steps,
including MA7 low bits and KSR, rather than raw AR or measured sample levels.
MA7 AR0 with the low attack bit set is nonzero and must remain live. Feedback
changes phase only, and note-on resets its history; no voice-local output filter
adds a separate tail. The shared player LP history still drains normally.

`tools/silent-attack-regression.h` covers all eight carrier topologies, feedback,
mixed live carriers, silent modulators, configured rate bits, KSR, slow attacks,
arbitrarily small nonzero gain, later events, final rests, audible release and
filter drain, retrigger, seek, block sizes and retained slot-allocation policy.
Targeted and full native ASan/UBSan checks pass. The instrument-WT regression
also covers MA7's nonzero odd attack, tiny nonzero audio, future notes and rests,
render blocks and seek; full existing ATR/recorded-Audio suites pass unchanged.

An instrument-WT probe confirms the same issue with a nonzero looping waveform
behind a stopped envelope: only the looped/XOF/nonzero-volume combination adds
the 80,000-frame silent allowance. EOF therefore also checks the configured
envelope for instrument WT (`keyNote >= 0` and `!streamAudio`). Recorded Audio
and ATR do not share that instrument-envelope contract and remain excluded.

## Naturally finished Hold carriers

An additive FM voice can contain a Hold-deferred carrier that finishes naturally
and another carrier still releasing. Previously the finished carrier retained
its deferred flag, leaving the voice classified as held. In a full pool this
could cause an unrelated held note to be stolen instead of the release tail.
Clearing deferred state on envelope completion and refreshing release state
for still-deferred voices fixes classification without changing sample math.

`tools/finished-hold-regression.h`, included by the audio regression suite,
compares paired voices with identical samples and only the completed carrier's
Hold response differing. It covers all eight topologies, natural completion
from sustain/release, remaining live held carriers, pedal release and retrigger.
End-to-end fixtures compare full and non-full pools, three render block sizes
and seek-to-start. The full-pool fixture requires zero stolen held voices and
sample equality with its independently non-deferred reference.

## ADPCM through an actual SMAF container

The installed FFmpeg 7.1.5 MMF muxer identifies its default audio codec as
`adpcm_yamaha`. The original 8,192-byte xorshift vector used by
`tools/yamaha-adpcm-test.cpp` was copied from its Yamaha ADPCM WAV wrapper into
an MMF container with `ffmpeg -i random_xorshift.wav -c:a copy random.mmf`.
FFprobe reported Yamaha ADPCM, mono, 8 kHz. Decoding that MMF with FFmpeg produced
16,384 PCM samples byte-identical to the WAV reference.

The same MMF was then read through the project's `SmafFile` parser and decoded
with `YamahaAdpcm`, using the established low-nibble-first order. All 16,384
samples matched the FFmpeg MMF output under ASan/UBSan. This supports applying
the single-division magnitude reconstruction to SMAF, beyond testing a WAV
wrapper alone. Both FFmpeg routes use the same codec family; they are not two
independent decoder implementations. The comparison does not establish every
Yamaha chip's ADPCM variant or independently validate the container's CRC.

## Changing PCM rates and loop history

`tools/pcm-dynamic-regression.h`, called by the high-rate resampling test,
compares actual `PcmVoice` output against an explicitly expanded source
timeline. The expected path shares the interpolation kernel but never wraps
or resolves periodic history. It checks loop state rather than independently
verifying FIR coefficients.

The bounded test covers 54,000 samples across source lengths 1, 3, 9, 31, 79
and 257, with loops beginning at the first, middle and final sample. Rates
change from 0.3 to 16.01 to 0.5, then repeatedly cross the 4..5 and 8..10
filter transitions.
Twelve cases explicitly widen the filter after short-loop history reaches
its cap, retaining the 64-sample reach required by the 128-tap follow-up.
The final ASan/UBSan run's maximum absolute difference was 1.19e-7.

## Initial 64-tap filter limits

A separate direct-double FIR probe compared the table-based filter against
its intended normalized Blackman/sinc response. For near-full-scale synthetic
sines at source advances 15.99, 16 and 16.01, maximum absolute sample errors
were approximately 5.28e-5, 2.32e-7 and 6.08e-5. There was no discontinuity at
16:1. Off-grid ratios, including 6, 12 and 18, showed errors up to approximately
0.0068 full scale due to interpolation between cutoff rows.

A tone at 1.4 times output Nyquist retained amplitude gains of approximately
0.2014 at 16:1 and 0.4671 at 32:1, relative to the input sine amplitude. These
are about -13.9 dB and -6.6 dB: a fixed 64-tap filter offers increasingly weak
separation at extreme reductions. The implementation improves tested high-rate
cases but does not provide uniform stopband rejection at arbitrary ratios.

## Bounded 128-tap follow-up

The follow-up keeps the established filter through 8:1 and smoothstep-blends
to 128 taps over 8..10. Above 10:1 it uses 128 taps. Its table has eight
fractional-phase steps and 32 cutoff steps covering bandwidths 0..1/8; the
lower bandwidth allows a smaller phase table. The new table occupies 152,064
bytes. Both high-rate tables are prepared during voice allocation.

The previous 64-tap table allocated 65 cutoff rows although rates above 4:1
only reach row 16. Row 17 is still retained as an interpolation guard: a rate
just above four can round its cutoff to exactly 16 in float. Keeping that row
preserves the original endpoint arithmetic. The reduced table occupies 594,432
bytes. The two high-rate banks together use 746,496 bytes, saving 1,400,064
bytes versus the previous 2,146,560-byte bank. The existing 32-tap bank is
unchanged. A frozen-header comparison under ASan/UBSan matched all 148,608
retained coefficient values and 70,686 output samples through 8:1 bit for bit,
including rates immediately adjacent to four and eight.

Initially the 128-tap boundary path alone resolved the first source index and walked
successive indices, extending the start or wrapping at loop end as needed.
This avoids a signed modulo for every tap while preserving summation order.
The expanded-source regression and independent FIR boundary tests cover this
path. Source advancement, authored gates and EOF remain unchanged.

At advances 16/32/64, the original synthetic tone at 1.4 times output Nyquist
shows alias-energy reductions of 14.45/7.31/4.59 dB versus the 64-tap direct
FIR. Tests also check the exact 128-tap response at cutoff/phase grid points,
a low-passband amplitude floor, rounded table endpoints and nonfinite-rate
fallback. Near-full-scale sines away from the table grid have maximum absolute
error 0.00171 against the independently constructed continuous 128-tap FIR in
the tested cases. All these checks pass under ASan/UBSan.

A host-only release-build benchmark called actual `PcmVoice::tick()` for 32
simultaneous voices over 44,100 frames. Long-loop cases at 16/32/64 took about
0.37..0.60 CPU seconds; short-loop cases took 0.39..0.46 seconds. The 9:1
blend cost about 0.57..0.84 seconds for long loops and 0.74..0.79 for short loops,
versus roughly 0.38..0.54 seconds for the previous filter in those cases.
These are isolated host CPU measurements, excluding the rest of the mixer;
timings varied between runs during other build work. They do not establish
Android throughput or battery impact. Larger 256/512-tap
prototypes were left out because their additional CPU cost was not justified
without device/polyphony measurements.

The fixed 128-tap limit still weakens at extreme reductions. Longer filtering
also changes the passband/transition response and transient ringing. Improved
synthetic stopband measurements do not establish a perceptual improvement for
every sound.

## Boundary cursor follow-up

The 16-, 32- and 64-tap boundary branches now use a small stack cursor to
resolve the first source index once and advance through subsequent taps.
Negative source time continues to repeat sample zero; non-loop EOF repeats
the final sample; loops restart at their authored start. Coefficients,
floating-point operations and accumulation order are unchanged. The interior
fast-path source and the existing 128-tap boundary body are unchanged.

An ASan/UBSan comparison against a frozen pre-cursor header matched 198,288
output samples bit for bit across all four tap counts, synthetic randomized
PCM and phases, endpoints, loop starts, removed histories and nonfinite-rate
fallbacks. The frozen implementation is not shipped as a test dependency.
`tools/pcm-boundary-cursor-regression.h` instead builds source-index timelines
by appending complete attack/loop ranges, then checks 608,256 cursor indices.
The high-rate suite also retains the 54,000 changing-rate voice checks and
explicit fractional endpoints for both upsampling and downsampling.

Interleaved release-build host benchmarks showed short-loop speedups of about
1.2..2 times for the shorter filters and about 1.2..1.3 times for the 9:1 blend.
Interior timings varied in both directions, generally by a few percent;
some tiny cases changed by roughly 2 ns per sample. Those measurements do not
establish zero performance regression, and are not Android or battery tests.
The useful evidence is the consistent short-loop gain with bit-exact output,
not a universal throughput claim. No cache revision is needed for this
optimization because it preserves the decoded output.

## FM phase continuity

Advancing the oscillator while its output is suppressed preserves elapsed
source phase when pitch returns below Nyquist. The focused FM test checks that
renderer property, including vibrato, retrigger and release. It does not prove
hardware phase-accumulator behaviour. The existing mute decision still uses
the unmodulated operator frequency; vibrato, waveform harmonics and modulation
sidebands remain separate anti-alias limitations.

## Unused FM LFO follow-up

The adopted optimization sets the note's LFO increment to zero only when no
operator consumed by the selected algorithm has configured AM/VIB depth. The
shared tick loop is unchanged, and evaluates the exact zero-input sine case.
Note-on restores both rate and phase; all live controls leave configured depths
unchanged. The query uses the actual operator instances, preserving even retained
operators when the public C++ API receives an inconsistent operator-count flag.
Minimum DVB/DAM codes still have nonzero enabled depths and remain active.

An Astra review checked every consumer and public controller. Frozen r53 versus
candidate traces compare 6,144,000 outputs, recent-level values and public
active/released/muted flags directly: both 73,728,000-byte streams have SHA-256
`eef2478525bfb0ca0fe67862c55bcd873a3c842316575d8d42d52b35bcf51658`.
The eleven real generated SMAFs also retain byte-identical PCM16 WAVs.
The independent operator oracle in `tools/fm-phase-test.cpp` covers 376,832
voice frames across both profiles, two rates, all four LFO codes, flag changes
on retrigger, controller changes and retained operators. It passes against
both the original and optimized engines under ASan/UBSan.

A single-process paired O3 benchmark uses 32 sustained voices, all eight
algorithms, both profiles, none/VIB/AM/both modes and 16 alternating-order pairs
per scenario, pinned to one allowed host CPU. The unused-LFO median speedup
across 16 scenarios is 1.055; individual medians span 0.819..1.279. Enabled-LFO
control medians are approximately 1.039/1.004/1.021 and also vary. Both selected
host objects have byte-identical 1,268-byte `FmVoice::tick()` instruction bodies.
These noisy host measurements cannot demonstrate uniform gains, absence of
regressions, or Android performance/battery improvements. Device measurements
remain necessary. Raw timings and comparison sources are retained with the
optimized build's validation evidence.

## Waveform-local sine work

The waveform helper formerly evaluated sine before dispatch, including for
square, triangle, saw and unbound user-wave selectors. The adopted change moves
the same expression into cases 0/1/2/8/9/10 and keeps a local double in case 11.
Gated sine expressions and every other formula stay unchanged. Normalization
still permits exactly x=1 for a tiny negative phase; no endpoint is remapped.
The raw modern selector 30 remains distinct from its masked aliases.

GCC and NDK Clang each compare 4,298,240 direct operator samples and 6,144,000
voice samples/recent levels/public flags byte-for-byte against the previous
engine, with no differences. An independent Astra audit checks 207,872 float-bit
results over all 256 selectors, 203 boundary/extreme finite phases and four
rounding modes. Fixed signal tables add 1,440 endpoint/alias/signed-zero checks
to the phase suite; both old and new cores pass under ASan/UBSan.

The following call counts are for 8,192 standalone modern-operator samples.
Timing medians come from a separate pinned, paired host test of 32 sustained
voices, four algorithms, both profiles, two LFO modes and eight alternating
pairs per scenario (80 scenarios). They exclude parsing, WAV I/O and Android
audio scheduling.

| Wave | Previous sine calls | Current sine calls | Median host speedup | Scenario range |
| --- | ---: | ---: | ---: | ---: |
| Sine WS0 | 8,192 | 8,192 | 1.035 | 0.776..1.486 |
| Square WS6 | 8,192 | 0 | 1.752 | 1.280..2.120 |
| Half-square WS14 | 8,192 | 0 | 1.895 | 1.259..2.693 |
| Triangle WS16 | 8,192 | 0 | 1.570 | 1.227..2.132 |
| Saw WS24 | 8,192 | 0 | 1.650 | 1.143..1.926 |

The actual Android ARM64 waveform function also loses its pre-dispatch sine
call; its selected debug/O3 build shrinks from 1,716 to 1,684 bytes. These are
code-generation observations and noisy host timings, not device speed or
battery measurements. All fourteen real generated MMFs render to the same
PCM16 WAV bytes as the previous engine, including the three new waveform probes.

Equivalence is over audio and renderer state. Removing an unused sine can
change sticky floating-point exception flags; the renderer does not consume
those flags. The audit does not assert identical floating-point environments
or trap behavior.

## Next measurements

The next timbre changes need reference captures rather than parameter guesses.
Prioritize the original custom-FM probes on a reference player or handset,
matching playback rate, gain and start time with the capture protocol. Then
compare envelope timing, feedback and vibrato separately before changing them.

One focused open question is pitch bend and envelope key-rate scaling. The
current renderer selects the rate offset at note-on using the bent frequency;
later pitch changes update oscillator frequency without changing envelope rates.
Thus a bend immediately before or after a note can select different envelope
timing despite identical final pitch. A reference needs to establish whether
the hardware latches or updates this offset. The paired F4 KSR probes cross the
current offset boundary from 8 to 9 and swap Bend/NoteOn at the same tick, with
identical final pitch and voice fields. Their local PCM differs while duration
stays 132,300 frames; all original nine candidate WAVs remain byte-identical.
No rate or event-order correction is justified by that software difference alone.

Dynamic harmonic/sideband aliasing and dense high-rate PCM CPU cost also remain
open. Measure Android polyphony/latency before increasing filter lengths or
changing waveform suppression. The current guard and bounded filters do not
establish hardware behaviour, and host timings do not predict battery cost.


## r60: scoped square/saw carrier bandlimiting

Broad per-operator PolyBLEP was rejected after independent composed-FM
references found regressions:11 of18 shaped-modulator cases and4 of18
phase-modulated shaped-carrier cases grew in MSE. One shaped-modulator case
changed from approximately1.10e-5 to4.59e-2 MSE. Standalone sinusoidal-PM
comparisons alone had missed these compositions. This is why the adopted
scope inspects the algorithm graph instead of treating every operator alike.

The production path applies amplitude-compensated PolyBLEP only to modern
WS6/WS24 final carriers without feedback and with every immediate upstream
operator provably silent. The note-on decision uses actual envelope state,
including MA7 low attack bits; an instantaneous zero, Nyquist suppression,
quiet level or absent output is insufficient. Incomplete public-C++ operator
configurations disable all eligibility, including retained operators. Raw
FmOperator configuration stays unfiltered unless explicitly enabled.

A2049-entry double table stores the inverse sinc-squared gain on steps0..0.5.
It is approximately16KiB, shared and initialized once. A1,000,001-point check
observed maximum relative interpolation error2.07e-7; this is not a formal
all-input bound. The permanent native test independently checks exact sinc
gains at8191 interior points per waveform. Constant-pitch gain/reciprocal
are cached; live pitch recomputes them; vibrato uses the current step, capped
at0.5. Subnormal steps bypass reciprocal calculation. The existing nominal
Nyquist guard and integrated phase remain in place.

Twelve stationary combinations (two rates, two waveforms, three coherent
frequencies) pass independent finite Fourier-series references with at least
75% lower squared error and fundamental amplitude within0.1%. The final
filter also preserves the prior spectrum tool's unfiltered outputs exactly.
For example at approximately2735Hz, nonharmonic AC fractions change from
5.040% to0.133% for square and7.143% to0.149% for saw. These exclude aliased
energy colliding with permitted harmonic bins and count numerical residuals;
they are not listening, total-alias-power or handset-fidelity scores.

Policy, feedback/PM isolation, odd attacks, incomplete retained operators,
retrigger/fresh-state equivalence, live pitch, zero/subnormal frequency and
vibrato crossing instantaneous Nyquist have permanent native coverage.
All36 composed-FM reference metrics remain identical under the scoped path.
Eight original wire-format scores bring the FM suite to34; all26 prior input
hashes are pinned. Native and independent go-smaf validation pass all8 checks.
Of123 actual MMF renders, six intended candidates change and117 WAVs remain
byte-identical. Every format and frame count is retained; six new nonlinear
controls remain exact, and the two new additive outputs change.

Eleven native suites pass ASan/UBSan with leak detection disabled in this
sandbox. The large audio suite was built from an isolated byte-identical
copy of all engine sources; every other suite links four freshly compiled
production objects, necessary because the operator layout changed. The final
bandlimit suite was rebuilt after adding retrigger checks. Diagnostic tools
pass12 CLI checks plus their tone self-test. Android lint, Debug and Release
builds pass for all three ABIs, with0 errors and4 existing lint warnings;
the debug APK verifies with v2 signing. No new Java coordination tests or
Android rebuild after tool/document-only additions were needed.

Seven alternating-order paired GCC O3 host runs use32 full FmVoices over24
cases (isolated, serial, mixed and feedback; sine/square/saw; AM/VIB off/on).
For affected isolated square/saw cases median wall ratios span0.74..1.30;
mixed cases span0.89..1.12. Controls also vary substantially. No uniform
performance improvement or absence of regressions is established. The slowest
observed host case has about2.65 times CPU realtime headroom; that number
excludes the player, output filters, AudioTrack and Android. Device performance,
power, listening acceptance and handset/reference captures remain unmeasured.
The converted-audio cache advances to r60 because rendered samples change.


## r61: complete ignored-instruction clocks

Fully consumed but ignored controller/reserved messages could disappear from
the event timeline. With no EOS and an earlier gate, a one-second final command
left a30ms score endpoint. Mobile (including compressed transport) and MA7
now retain a timed ScoreNop for non-routed CC and complete reserved A0/D0 or
20/50 messages. Payload-width validation remains mandatory; truncated messages
and unknown-width recovery retain their earlier behavior and diagnostics.
No control/pressure implementation is inferred. Cache version advances to r61.

The focused native suite checks two rates, both MA7 channel banks, compressed
Mobile, unchanged audit categories, all-silent command-only scores, block/seek
invariance, later EOS and partial payloads. A sustained RR0 fixture stays
audible past the former10.03s safety endpoint and matches the independent
FF00 NOP oracle. The existing ten-second safety extension itself is unchanged.
An initial final-partial-block assertion in that new test was corrected to
check individual sample positions; the final focused suite passed after rebuild.

Eighteen original container/CRC-checked wire fixtures include12 EOF-recovery
targets and6 NOP/later-EOS controls. EOS is deliberately absent in target
sequences: these are prefix-recovery probes, not strict format-conformance
claims. All141 MMFs were rendered against r60:12 target durations change and
129 WAVs stay exact, including all123 prior inputs. Target PCM prefixes stay
exact and appended PCM16 is entirely zero;12 NOP-control comparisons match.
The18 score/tone inventory reports also remain byte-identical.

The relevant audio-regression, security-smoke and focused clock suites pass
ASan/UBSan with leak detection disabled. MaPlayer was rebuilt; the other three
objects were reused only after confirming their sources and every engine
header match the r60 build. Diagnostic CLI12 checks and tone self-test pass.
Android lint/Debug/Release pass all three ABIs with0 errors and4 existing
warnings, and the APK verifies v2 signing. Unaffected operator/resampler/Java
suites were not repeated. No handset, device-performance or listening claim.

## r62: displaced ATR source bounds

A long ATR source displaced from the shared PCM pool retained its original
source length in the safety endpoint. A held FM/WT voice could therefore
continue for about26s instead of the surviving-source control's10.01s.
After the final authored event and the last live ATR source, the player now
reduces that bound using completed source EOFs/gates and the authored endpoint.
Muted live recordings, later score instructions and the existing ten-second
safety tail remain intact. Seek/init restore the conservative initial bound.
Cache version advances to r62.

Completion uses each source's original ceil(length*outputRate/sourceRate)+1
rounding. An initial cursor-based implementation differed from a gated-source
oracle by one frame at44.1kHz; a new permanent test caught this. The final
implementation and every affected native caller were rebuilt and revalidated.

Seventeen original CRC/chunk-checked fixtures cover16/32-slot ATR steals,
held FM/WT, later triggers/controllers, live/muted long recordings, and WT
replacing a partially played ATR. Across158 MMFs, seven corrected tails match
independent surviving-source/gate controls exactly;151 outputs stay exact,
including all141 prior inputs. Prefixes remain exact until the corrected
existing20ms safety fade. Prior fixtures use a12s render cap; new fixtures
use35s and complete before that cap, covering the entire old/new tails.
Score/tone inventories of all17 new inputs also stay byte-identical.

Audio-regression, security-smoke, score-ignored-clock-test and the new
atr-runtime-end-test pass ASan/UBSan with leak detection disabled. MaPlayer
and all native test/diagnostic callers were rebuilt. Three other objects were
reused only after matching their unchanged source/header dependencies against
the frozen r60 sources and confirming none depends on ma_player.h; the player
layout itself changed. The focused suite additionally covers8kHz/44.1kHz,
block-size/seek/reinit invariance and an audible surviving long recording.
Diagnostic CLI12 checks and the tone self-test pass. Android lint/Debug/Release
pass all three ABIs with0 errors and4 existing warnings; APK v2 signing passes.
Unaffected operator/resampler/Java suites were not repeated. No handset,
device-performance, reference-app or listening acceptance claim is made.

## r63: Mobile recorded Audio source namespace

An EOS-only MA7 MTR could overwrite the channel's streamTrack and redirect
an earlier Mobile MTR's recorded Audio to its own same-numbered Mwa. A
negative decoy replaced a positive source; absent decoys muted the note.
One focused Astra audit supplied that reproduction. Mobile NoteOn now carries
its originating MTR, including packed transport and remembered velocity.
Runtime and tone diagnostics prefer the event source, with the prior channel
fallback for untagged legacy/internal notes. Shared-channel profiles and the
MA7 per-wave pan policy are unchanged. Cache version advances to r63.

The new native suite checks96 mixed cases across plain/packed Mobile,
MTR0/1 (shared channels0/16), wave IDs1/14/32, explicit/running velocity,
decoy presence/order, missing own sources, seek/init/block sizes and44.1kHz.
Its first oracle used a standalone Mobile source and failed on the existing
mixed-profile pan gain difference, despite resolving the correct source.
The corrected oracle holds channel settings fixed and duplicates the original
source into the irrelevant bank, then varies only that bank in the target.
The final test fails against r62 and passes against r63.

Thirty-two independently CRC/chunk-checked original wire scores bring the
comparison pack to190. Twelve intended candidates change;178 outputs stay
byte-identical, including all158 prior inputs. Twenty source-control pairs
match exactly. The new cases complete before their2s render cap; old cases
retain their original12s/35s caps. All32 score audits remain exact. Tone
routes now name the correct MTR/wave/reason in all32 cases;16 detail rows
change, including four waveform-duplicated controls whose audio was exact.

The six relevant native suites are recorded-source-binding-test,
recorded-resample-test, atr-runtime-end-test, score-ignored-clock-test,
security-smoke and audio-regression. They use ASan/UBSan with leak detection
disabled in this sandbox. MaPlayer and native callers are rebuilt; three
objects are reused after verifying unchanged source/header dependencies
that do not include ma_player.h. Diagnostic CLI13 checks and tone self-test
pass; the new source diagnostic test also fails against r62. Android
lint/Debug/Release pass all three ABIs with0 errors and4 existing warnings,
and the APK verifies v2 signing. Unaffected operator/resampler/Java suites
are not repeated. No handset calibration, listening or performance claim.
