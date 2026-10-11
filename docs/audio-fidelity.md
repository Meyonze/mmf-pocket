# Audio fidelity work

Current release preparation: stable1.6.3/code69/cache r47 contains the same
validated audio/playback code as DEV beta.10. It combines the historical
source-only batches below with progressive-playback coordination repairs.
See "Consolidated DEV beta.10" at the end for scope and verification limits.
Earlier "pending" notes describe the state when each batch was completed.

## Pending progressive batch: retain quiet nonzero PCM caches (2026-10-12)

ProgressivePlayback has a separate Java WAV/cache writer. It still rejected
PCM peak1..3 after JNI had produced valid samples, independent of the native
WAV writer's earlier quiet-output fix. Change only its final silence predicate
from peak<4 to peak==0. Empty/all-zero output still rejects; never normalize
volume or change source samples, timing, threads, seek or AudioTrack behavior.

Unmodified frozen Java renderLoop fails the quiet-cache assertion. Host test
compiles that actual class with a synthetic PCM producer and compile-only
Android media stubs; every device operation throws if called. No playbackLoop,
JNI/MMF decoding, simultaneous threads or Android audio are exercised here.
Candidate60 PCM cases pass with whole-file independent PCM/RIFF oracle, blocks
1/31/1024 and phone flag passthrough:30 newly accepted tiny nonzero cases,
18 ordinary output and12 empty/zero cases. The30 legacy-compatible cases have
exact old/new statuses, frame counts and complete WAV SHA256. Four session-
creation/render error, cancellation and actual move-error cleanup cases pass.
Tests include negative PCM extrema, leading rests and later quiet/loud output.

Actual production Java/NativeMmfRenderer classes also compile against Android
API35 without host stubs. Existing explicit-close try-resource warning remains
in both profiles; close-before-move ordering is retained. Not a Gradle/APK/lint
or device check. Existing native/bridge/helper sources exact; no private corpus
rerender, sound quality or listening claim. Add host Java regression CI step
locally only; GitHub CI not run. UI/version/cache unchanged this batch.

Source only alongside four previous pending batches; DEV beta.9 excludes all
five. Next combined APK beta.10/code68/cache47 needs packaging/device checks.
No APK build/Git publication. Proprietary/reference-dependent gaps remain.

## Pending output batch: fail safely on WAV storage errors (2026-10-12)

Check initial header and each PCM-block write immediately. Check final seek,
header rewrite, explicit flush and close before reporting success. A failed
header rewrite/close could previously report successful conversion; a failed
block write could wastefully render the rest of the song. Existing Java error
handling removes the partial file instead of moving it into the WAV cache.
No synthesis, gain, timing, PCM16, quiet-output or JNI behavior changes.

Extract the actual writer into an ostream helper and wrap its close check,
allowing bounded host fault injection without filling a disk or adding
production test flags. The baseline adapter extracts the old writer body
unchanged; scope reconstruction verifies that exact transformation. Its final
seek failure reproduces false success. Candidate tests pass for initial
header, first/second PCM block, final seek/header/flush/close faults, including
immediate rendering abort, healthy file-byte identity and uncreatable path.
These are injected host stream failures, not real Android/disk-full tests or
power-loss durability guarantees; flush is not an fsync guarantee.

Original real-file writer, baseline stream adapter and candidate each render
70 original synthetic cases: all complete WAVs and statuses are SHA256 exact
on healthy storage. Prior normal/quiet/nonfinite WAV/JNI tests still pass.
All12 core files and PCM16 helper exact; Android3ABI normal/audit object
compilation passes, not linked APK/device validation. No private corpus
rerender, GitHub CI/sanitizer runtime, timbre or listening improvement claim.
Existing bridge test expanded locally; Java/UI/version/cache/workflow unchanged.

Source only alongside the three prior pending batches. DEV beta.9 excludes
all four; next combined APK beta.10/code68/cache47 needs packaging validation.
No APK build or Git publication. Reference-dependent synthesis gaps remain.

## Pending output batch: preserve representable quiet WAVs (2026-10-12)

The actual WAV writer rejected float peaks below0.0001, although such samples
can encode to nonzero PCM16 and stream successfully through JNI. Use actual
emitted nonzero PCM16 instead of that arbitrary amplitude threshold. Keep
all-zero quantized output/zero-frame failure, and prior nonfinite rejection.
No gain normalization, sample/timing change, new dither or timbre adjustment.

Frozen old bridge independently fails the quiet-output assertion. Expanded
host tests cover64 Mobile F2/FM, MA7 F3/FM and ATR PCM8/ADPCM volume/phone cases:
20 representable below the old threshold,13 quantized-silent,31 ordinary.
WAV and JNI blocks1/31/1024 match independent PCM oracle exactly. Host callback
tests, not Java VM/device. Existing normal/nonfinite bridge tests pass too.
Fresh old/new comparison70 synthetic cases confirms every emitted PCM byte
and duration unchanged:20 formerly false failures now finalize successfully,
50 complete output files/statuses SHA256 exact. All12 core/helper files remain
exact; Android3ABI normal/audit object compilation passes, not a linked APK.
No private corpus rerender, reference-device listening or perceptual improvement
claim. Existing CI test expanded locally; GitHub/sanitizer runtime not run.

Source only, alongside muted-tail/PCM16 pending changes. No APK/version/cache/
Java/UI/workflow change or Git publication. Latest DEV beta.9 excludes them;
next combined APK needs beta.10/code68/cache47 and packaging validation.

## Pending output batch: nearest PCM16 conversion (2026-10-12)

Use one finite-checked PCM16 helper in both the actual WAV writer and JNI
streaming output. Preserve the established symmetric32767 scale and clipping;
round to nearest integer (half away from zero), rather than truncate toward
zero. No dither, added silence noise, timbre/gain/modulation retuning or core
renderer changes. Reject NaN/infinity before integer conversion or JNI writes.

Independent numeric oracle covers every half-step's float neighbors, random
values, signed zero, saturation, subnormal values and host rounding modes.
Maximum quantization error is half a PCM step; tested changes from the previous
output are at most one step. Random-test quantization RMS falls from0.575319
to0.288756 LSB. This is about6dB for that error component ONLY, not a measured
music SNR or an assertion of a large audible/hardware-fidelity improvement.

Actual bridge host fixtures compare WAV bytes with an independent oracle and
JNI output with WAV: FM, ATR PCM8/ADPCM, phone mode off/on,1/31/1024-frame blocks,
odd array capacity, repeated EOF and nonfinite rejection pass. Minimal host JNI
callbacks are used, not a Java VM or Android device. Original bridge fails the
nearest-PCM oracle as expected. All12 core files are unchanged from this batch's
baseline (which already includes the pending muted-tail fix below).
Android NDK API29 object compilation passes for arm64, armv7 and x86_64, normal
and audit modes. This does not establish linked APK or device behavior.
CI sanitizer steps added locally; GitHub CI/sanitizers not run in this batch.

Source only: installed DEV beta.9/code67/cache46 does not contain this or the
muted-tail change. Next combined APK needs beta.10/code68/cache47 and Android
packaging validation. No APK build or Git publication in this batch. Physical
ROM/AL/EG/LFO calibration and unsupported formats remain separate open gaps.

## Pending native batch: provably muted score-end tails (2026-10-12)

Investigated38 previously bounded score-end tails. Every original's full
stereo-float fingerprint/peak/duration matches the previous beta.8 result.
Runtime envelope categories explain the retained voices: Hold3files, XOF14,
exact zero release5, slow nonzero release21 (overlapping categories). All
surviving FM/WT voices had received keyoff; no recorded Audio/ATR remained.
These are current renderer states, not calibrated hardware envelope laws.

Four cases have exactly zero remaining voice gain, with no later authored
events. Add an EOF-only output-gain check: keep ordinary active-voice EOF;
otherwise require the score/source endpoint, consumed authored events, every
live FM/WT/Audio/ATR gain exactly zero, and the existing1e-7 filter drain.
No amplitude/RMS heuristic, early voice retirement, EG retiming, per-song
rule or change to oscillator, sample, limiter, volume or allocation behavior.
Future gain/controller events still run and may revive the existing voices;
nonzero quiet tails retain the usual safety allowance. Still-live muted ATR
sources retain their estimated source/gate endpoint. Initial broad EOF guard
failed existing ATR duration regression; endpoint guard added and rerun.

Original synthetic FM/WT Volume/Expression fixtures test audible-before-mute,
later unmute, quiet nonzero, final rest, seek, block1/1024 and stable EOF.
ATR fixtures ensure zero gain cannot discard source time or future gain events.
This is pending native source, NOT installed beta.9; next batched integration
must beta.10/code68/cache47. No Android build or Git publication in this batch.
Actual original-output impact and delivery proofs are recorded separately in
tail-cause-next, not implied by this source-only note.
Final host impact validation passed:32 full original outputs retain exact
hash/peak/duration. Four shorter outputs are bit-exact prefixes of fresh old
outputs; removed samples below1e-7, combined omitted tail34.900sec. This is
output duration saved, not a CPU-time benchmark or audible timbre improvement.
Final audio/security/Recorded/ATR host suites pass; Android integration pending.

## DEV integration: 1.6.3-beta.9 - bounded waveform/container reads

Batch two structurally reproduced parser corrections, without synthesis or
instrument retuning. Within Mtsp, skip MspI/unknown/wrong-family children by
their bounded chunk length instead of searching their opaque payloads for Mwa
signatures. Otherwise metadata can manufacture/replace a wave or exhaust the
8192-child scan budget before the following real wave. Invalid-header byte
recovery, valid duplicate-wave ordering and existing codec acceptance remain.
At the outer MMMD boundary, clamp each child to the declared parent region,
not to the physical input buffer including CRC/unowned suffix. This does not
add any guessed codec, stereo layout or sample-byte law.

Original independent fixtures reproduce both failures against immutable beta.8
sources. Candidate boundary, existing audio and security suites pass. Tests
cover fake nested waves before/after a real wave, large/empty metadata, wrong
wave family, invalid-prefix recovery, oversized/partial children, parent bounds,
render blocks and seek. The first test wave mistakenly declared 20-bit instead
of PCM8; corrected before the successful old-source failure reproduction.

All382 originals' complete parsed file/track/header/clock/setup/sequence/wave
fields compare directly and exactly, including raw wave bytes and metadata;
all original SHA256 identities and other11 native source files remain exact.
This is parsed-input identity with unchanged rendering code, not a new full
audio render or hardware/listening calibration. No audible quality increase
is asserted for these unchanged originals. VersionCode67/cache r46; debug-only
batch, same DEV identity. Android packaging/delivery verified separately.

Remaining-gap review: the one missing-RAM original carries MA7 wave command
0x23 codec1 for the requested ID0; unsupported encoding, not absent input.
The one missing-recorded original requests an Mwa absent from its MTR despite
an equal-numbered instrument-RAM wave; these namespaces must not be aliased.
No decoder/sample-law inference follows from either observation. Proprietary
ROM, AL filter, fixed frequency/raw detune, user FM wave and physical envelope
calibration remain separate source/reference-dependent gaps. Do not invent
per-song balance/vibrato corrections or call them completed fidelity features.

## DEV integration: 1.6.3-beta.8 (2026-10-11)

Batch the previously verified ATR resampling source change into the Android
DEV app. No additional FM/WT/recorded-Mwa timbre, balance or modulation tuning.
VersionCode66/render-cache r45 request fresh conversions. Same orange DEV
resources, app package and debug certificate; no release signing/publication.
Prior native source and original-render evidence are retained separately.
Scheduled-tone inventory remains host-only and does not change app sound.
User-SRAM transfer candidate still lacks established sample law/endian;
no guessed decoder, waveform substitution or per-song change is included.
Offline assembleDebug/lintDebug pass (0errors/4existing warnings), three ABI
native libraries match fresh stripped outputs and differ from beta.7. Same
debug signer, orange DEV resources, package/version and compiled cache verified.
All40Android source/resource files preserve beta.7 except cache; all12native
sources match the validated ATR batch. Prior ATR/recorded/audio/security host
binaries rerun successfully; no fresh host compilation/original-corpus renders
or device listening in this integration. OneDrive APK/source/report copies
are checked separately. Public stable1.6.2 and Git publication unchanged.

## Scheduled tone-path inventory (2026-10-11, host-only)

Add tools/tone-usage-audit.cpp to distinguish defined-but-unused voices from
scheduled instrument requests and actual FM/WT/recorded/ATR allocation paths.
Replay the decoded original event order and timed activations; identity is the
voice-table element, not merely a bank/program key. Recording routing wins even
when its wave is absent. Separate ROM/missing-RAM source fallback from generic
fallback, and intentional EP0 from missing samples. Permanently silent MA7
notes still fire but do not count as feature use; current channel-volume zero
is not immutable silence. Count only used operators of two-/four-op voices.
Reject empty inventories. Original classification fixtures are standalone and
connected to local sanitizer CI; injected metadata tests are not new protocol
evidence. No playback/native/Android/version/cache changes in this batch.

382 private originals parse and scan completely, with no events beyond the
600-second safety bound. Of5577 definition elements,5223 are requested and3838
bound, across1135883 scheduled NoteOn events (not unique sounding notes).
255 files request missing ROM PCM voices (190841 fallback NoteOn instances);
one file requests missing RAM (51), and one has22 missing recording requests.
Actual bound definitions use AL in58 files/187476 scheduled selections,
fixed-frequency in10/2153, raw nonzero MA7 DT in38/43745, and user-SRAM waveform
numbers15/23/31 in3/982. Categories overlap: this is not a fidelity score.
AL presence, including zero bytes, is not perceived impact; raw DT is partially
represented/uncalibrated, not proof that every nonzero setting sounds wrong.
The observer does not render or model lifetime/steals/audibility; artificial
pool saturation is possible. Rejected definitions outside the voice table are
outside this inventory; score-audit remains necessary. Never infer that a
sample is absent from its name or assume all fallback is ROM. These findings
prioritize source-backed work without arbitrary song-specific retuning.
All12 frozen production source files remain byte-exact. No new APK or Git
publication; pending ATR changes remain for next beta.8/code66/cache45.

## Pending native batch: ATR one-shot resampling quality

Source-only after DEV1.6.3-beta.7/code65/cache44. ATR WaveOn now selects the
existing non-loop16-tap upsampling/32-tap anti-alias resampler, with necessary
shared-table preparation on allocation. No new numeric kernel, decoding,
source pitch/position/EOF, authored gain/pan/gates/EOS, voice limits or slot
allocation law. Unity-rate output exact; no added reconstruction tail. Shared
output-filter drain, transient overshoot/pre-ringing and limiter peaks may
change with the waveform. This is renderer quality, not calibrated handset DSP.
ATR Pitch Bend remains unapplied: a score bend range is not evidence for ATR.

New original standalone tests independently construct direct-double FIR,
short-source edge extension/EOF, actual ATR signed8 and decoded ADPCM paths,
pass/stopband, unity, authored gain/pan, reinit/seek/block and slot reuse.
ADPCM coverage checks interpolation of the decoded ATR signal, not an
independent ADPCM decoder. An independent50ms/60ms clock plus post-mix LP
oracle checks hard mute directly, so Gate/EOS cannot pass only by agreeing on
the same wrong time. Existing nonnative Gate/EOS comparisons now use the same
complete recording instead of differently truncated inputs with different FIR
edge taps. WT exclusions hash535ae50ff0d9110d exact frozen/current; earlier
Mobile/MA7 recorded and HPS tests continue passing. Linear recentLevel remains
exact, preserving the source-level voice-steal detector.

Frozen ATR suite fails expected quality selection; candidate standalone,
complete audio regression and security smoke pass. Existing Astra bounded
design/final review found no major issue; standalone suite is connected to the
local sanitizer workflow, not pushed/run. PortableLinux x86_64-musl cross-build
passes, not executed on Linux.32-voice warm-table host microbenchmark16000frames:
linear/quality17.922/34.766ms at0.5;19.426/85.190ms at2. This uses actual ATR
voice copies, not an Android runtime/power test or pool-allocation benchmark.
Recordings can occupy16/32shared PCM slots, unlike Mwa's separate2voice pool.
Fresh382 originals old/new first10s complete: only the one ATR-bearing file
changes waveform hash/peak; other381 complete reports exact. All382 whole-score
events/clocks/origin provenance exact. That original's full42.993s render ends
naturally in both versions, with identical duration/diagnostics/peak0.609 and
only waveform hash changed. Six previous problem originals full reports exact;
all5 HPS originals full reports preserve every field except that same ATR file's
waveform. Full groups overlap, not12unique inputs. Others not fully rendered.
OneDrive source/report delivery records are in atr-resample-next/HANDOFF.md
and separate manifest/verification files. No new APK/version/cache or
Git commit/push/tag/release; next batched DEV must beta.8/code66/cache45.

## DEV integration: 1.6.3-beta.7 (2026-10-11)

Batch the HPS zero-gate recovery, nominal feedback regression tests/comment,
and recorded Mwa Audio resampling source changes below. No new timbre/gain/
vibrato retuning is introduced during packaging. VersionCode65 and render
cache r44 force fresh conversion instead of reusing beta.6/cache43 results.
Offline assembleDebug and lintDebug pass (0 errors,4 existing warnings).
All three packaged ABI libraries match the fresh build and differ from beta.6.
Compiled cache prefix, package/version, orange DEV resources and unchanged
debug certificate are verified against beta.6. No release key used; public
stable1.6.2 unchanged. No new hardware listening/timing or sanitizer CI run.
Source and earlier regression evidence are backed up separately from the APK.
Git commit/push/tag/public release are not part of this DEV delivery.

## Pending native batch: recorded Mwa Audio resampling quality

Source-only after DEV1.6.3-beta.6/code64/cache43. Mobile1/2 and MA7 recorded
Mwa Audio now opt into the existing16-tap upsampling/32-tap rate-dependent
anti-alias kernels. These are renderer-quality choices, not calibrated handset
DSP. Originating note grammar, not shared channel setup, selects the profile:
HPS notes remain linear even with stale modern channel state. ATR explicitly
remains linear; WT uses exactly its previous quality policy. Unity-rate output
is exact. Necessary shared tables are prepared when the Audio voice starts.
Source data, source pitch/advance, gates/2ms gate fade, velocity/pan/gain,
two-slot oldest-first allocation, decoding and event timing are unchanged.
No guessed instrument retuning, per-song gain or blanket accompaniment change.
Symmetric FIR may pre-ring or overshoot; no added FIR playback tail/delay or
independent clipping. Changed samples can affect the shared limiter and mixed
output peaks: unchanged authored gain is not a claim of identical mix amplitude.

Original independent direct-double FIR fixtures cover source lengths1/2/3/7/
16/32/79, exact phase/cutoff rows, edge extension, native EOF and gate fade.
Separate synthetic pass/stop-band tests exercise ratios1.5/2/4/6; the0.45-cycle/
source stop-band tone loses more than20dB energy relative to linear in these
fixtures, not at every frequency near the finite filter transition. The initial
0.375-cycle test at1.5 fell in that transition and failed; retained initial log,
threshold not relaxed. At0.5 upsampling, sine RMS error is0.0000583431 versus
0.0349011 linear. These are test-signal measures, not per-song perceptual claims.
Integration covers compressed/uncompressed Mobile and MA7, actual HPS decoded
origin flags, stale channel state, unity, source rates4000..48000, block/seek,
reinitialization. Actual ATR triggering and WT allocation/render outputs retain
the frozen float hash746fc64f20bee869. The original short WT exclusion fixture
ended before512 frames at high source rate; its input was lengthened, without
production changes. The old simple-decimation Audio equality assertion was
replaced; independent band tests now guard the intended conversion behavior.
Dedicated candidate/full audio/security pass; frozen dedicated quality fails
as expected. Existing Astra design/final reviews found no blocker. Portable
Linux main cross-build passes; sanitizer CI is connected locally but NOT run.

Fresh382 originals: all first10s render reports pass;23 waveform hashes and9
reported peaks change only in recorded-Audio-bearing files. All remaining
fields, whole-score musical/NOP/EOS clocks and origin provenance are exact.
Routing inventory identifies28 recorded-Audio-bearing originals; all28 full
renders pass and change waveform hashes,8 reported peaks. Every other field,
including actual rendered duration, diagnostics and endpoint, stays exact.
Natural endings24/28 unchanged; the same4 pre-existing safety-tail endings
remain unresolved. Previously reported6 problem originals and5 HPS originals
have complete full-render reports exact and natural endings; groups overlap,
not39 unique originals. No new device listening or reference-accuracy claim.
All12 native files pass exact reverse-patch scope verification: quality opt-in,
startAudio argument, shared tick exclusion removal and one interpolation comment
only. Underlying FM/WT/interpolation numeric code and decoder fields unchanged.

Host-only warm-table microbenchmark for4s/two recorded voices: linear/quality
8.636/18.493ms at0.5,7.298/42.507ms at2. This excludes kernel creation and is
not Android CPU/power/conversion latency. OneDrive source/report destination
MMF Player/recorded-resample-next; no private media/vendorPDF/ROM/keys/binaries
included in source archive. No APK/version/cache/commit/push/tag/release;
next batchedDEV must increment beta.7/code65/cache44.

## Native validation batch: MA5 nominal FM feedback evidence

Tests/documentation only after HPS zero-gate recovery. Existing playback
numerics, profiles, waveforms, envelopes, gains and scheduling are unchanged;
no new APK/cache/version or publication. PDF skill extraction and full visual
inspection of MA5 UserManual1.33 pp62-63 corroborate the nominal feedback
amounts directly:0,pi/16,pi/8,pi/4,pi/2,pi,2pi,4pi radians correspond to the
existing modern cycles0,1/32,1/16,1/8,1/4,1/2,1,2. Two-sample averaging and
physical signal normalization remain uncalibrated. Legacy/MA7/fallback profile
policies are not upgraded to hardware fidelity from this MA5 table.

The diagrams retain direct-output feedback in additive algorithms. A blanket
output-carrier exclusion would wrongly remove those loops. Arbitrary nonzero
FB fields on other operators remain unverified; no new eligibility mask is
imposed. New original fixtures independently assemble FmOperator instances,
feedback history and all8 routes, deriving nominal cycles from radians rather
than sharing FmVoice's table.8algorithms x8FBsettings x4existing profiles x3
rates=768fixtures,2048samples each, checked sample-exactly. Carrier-mask
counterexamples and retrigger/history/profile reset are also covered. This
checks current numeric implementation, not independently emulated chip DSP.
An initially insensitive alg1/FB3 reuse fixture was strengthened to alg0/FB5
and explicitly observed profile/depth differences following Astra review.

Only one native comment changes; stripping that exact comment makes all12
native files match the frozen baseline. No new real-corpus render, audible
improvement or hardware recording is claimed. Repeating382 unchanged-source
audio renders would add cost without testing a new synthesis path. Dedicated
baseline/current and full audio/security suites pass instead. Source/test
records destination OneDrive/MMF Player/fm-feedback-proof-next; no private
music,manufacturer PDF/ROM,binaries/APK/signing keys are included.

## Pending native batch: HPS zero-gate recovery

Source-only after batched DEV1.6.3-beta.6/code64/cache43; no new APK or Git
publication. Next batched APK must increment all three. SMAF3.05 pp29-31
were fully extracted/visually checked using PDF skill: HPS EOS is four literal
zeros, not permission to extrapolate MA7 timed EOS to delayed HPS triple-zero
compatibility. That existing EOS policy is unchanged. HPS score GateTime0 is
prohibited. For a normally encoded zero gate, only non-ATR HPS scores record
Reserved0x106 and retain Duration as inert ScoreNop. No new voice, retrigger,
note identity or generated gate-off. Reader-malformed lengths stop first,
without a fabricated clock or second diagnostic. Known-width recovery keeps
scanComplete true; gate0 takes diagnostic precedence over a reserved key.
Valid notes, ATR, Mobile/MA7 zero-gate remembered velocity, DSP/timbre/gain/
balance/vibrato remain unchanged. No per-song correction or hardware-match claim.

Original fixtures cover all254 note bytes excluding00/FF x3 voice types,
prefix/suffix and independent tracks, zero/multibyte/late clocks, silent-only
tracks,600s cap,400000-event bound and later-track budget, malformed lengths,
seek/block, modern/ATR exclusions. Parsed XOF and an actual tail-limit hit
verify the safety-fade oracle against independently authored FF00 NOP. A late
clock can move an existing held-voice safety fade, not merely add silence.
Frozen clock probe fails as expected; unchanged exclusions pass. Dedicated,
full audio and security regression tests pass. Existing Astra bounded design/
final source reviews found no serious issue; comparison success/complete
requirements were strengthened following review. No new delegation.

Frozen parser plus aggregate callback scans382 actual originals:5 HPS files,
6468 nonzero gates,0 zero gates. Observer independence and12 frozen native
files verified; only production ma_player.cpp differs. Fresh382 first10s
complete waveform/runtime/audit reports are exact, including estimated bounds.
Whole-score musical fields/sourceTrack/handyPhoneNote/NOP/Mobile+MA7 EOS clocks,
endpoints and provenance are exact. All5 HPS and6 previous problem originals
fully rendered: every report field/hash exact, natural endings. The groups
overlap; not11 unique originals. Strict comparisons require all expected inputs
and success/completeness, without field exemptions. Other corpus inputs were
not full rendered. No actual-corpus audible improvement is claimed: the bug is
reproduced in synthetic data and absent from this private corpus. Source/reports
destination OneDrive/MMF Player/hps-zero-gate-next. No private media,manufacturer
PDF/ROM,APK,binaries or signing keys packaged; no Android/device test this batch.

## DEV beta.6 batch: accepted MA7 terminal clock

After hps-note-data-next, only the already accepted MA7 FF2F00 success branch
adds a timed inert ScoreNop(a0x2f,b3). The distinct marker keeps existing Mobile
EOS inventories separate. MA7 guideline1.60 p34 (8.6.4 Track End Point) was
extracted and fully visually inspected using PDF skill. Existing compact SEQU
already validates this terminator and reads its preceding Duration, but did
not preserve that time in the event horizon. Do not import new wire syntax.
Missing/nonzero terminators gain no clock; suffix rejection and audit remain.
Natural gates/releases/ATR and600s safety policy remain; this is not immediate
sequence hard mute. No DSP/timbre/gain/balance/vibrato/per-song adjustments.
CC121 remains Unsupported: authoring SMF reset Table8 on p32 does not prove
the compact converter's transport behaviour. Existing Astra bounded design
and final source reviews found no blocker for clock-only scope.

Original tests cover GM/customFM/WT,low/high channel banks,zero/multibyte/late
clocks,independent MTR,gate beyond EOS,recorded Audio,ATR,XOF safety,terminal-
only silence,600s cap,exact event-resource bound,seek/block and all malformed
terminators. Frozen clock FAIL expected,exclusions PASS. Candidate dedicated,
full audio and security PASS. Old score-NOP/length/muted-note assertions assumed
MA7 EOS was untimed; replace their expected endpoint with authored timing,
preserving waveform,gate and malformed assertions. Initial exclusion grouping
also called these new-policy tests; isolate unchanged VLQ compatibility checks.
All intermediate failed reports are preserved, not omitted.

Fresh382 first10s actual samples,lengths,diagnostics and ending flags unchanged;
one estimated-seconds report changes. All existing musical event fields,
including sourceTrack and handyPhoneNote,regularNOP and MobileEOS clocks exact.
61 accepted MA7 clocks extend6 precise endpoints. Whole-score provenance valid
(6468HPS/331466MA7 notes,hostEv32bytes). All61 MA7 originals fully rendered old/
new: only3 waveform hashes/frame lengths change,all diagnostics/peaks unchanged.
One authored ending adds1.06s;two other endings add one sample each. Natural
endings remain50/61;the same11 existing safety-limited endings remain. Six prior
problem originals have all complete waveform reports unchanged,natural endings.

FINAL INDEPENDENT ORACLE CHECK PASS: all61 full outputs match every compared
field of the frozen renderer with only accepted-boundary observation,
replacing FF2F00 by authored same-Duration FF00+EOF in memory. No new EOS
scheduling branch is used by the oracle. The six prior problem originals also
match the oracle; observer-only marker counts are independently checked.
Observer/source independence was checked; all12 frozen files retained,only
production ma_player.cpp differs. Source cpp SHA256
B441F6CA9C6DF322EE084E4997CB8C5518BB94D05EB4B8D58A636B60737E73B7.

User approved a single batched DEV APK containing all pending native repairs.
Source1.6.3-beta.6/code64/cache43; assembleDebug and lintDebug PASS. APK identity,
fresh r43 cache literal,three native ABI libraries and beta.5 debug-certificate
compatibility verified. Release signing disabled; no commit/push/tag/release.
OneDrive destination: MMF Player/MMF-Pocket-1.6.3-beta.6-debug.apk, with source
and reports under MMF Player/ma7-eos-clock-next. No Android runtime/device listening
claim,private media/vendorPDF/ROM/binaries/keys in source package. Late authored
endpoints can extend a rest or move the existing XOF safety fade; independent
NOP oracle validates semantics,not perceptual/hardware-equivalent timbre.

## Pending native batch: HPS NoteNumber validation

Source-only after HPS reserved FF boundaries. No APK/publication; installable
remains1.6.3-beta.5/code63/cache42. Increment all3 before the next batched APK.
PDF skill extraction and full-page visual inspection of SMAF3.05 p31 confirms
HPS NoteNumber0/13/14/15 are prohibited;1..12 are legal, C uses12 in the preceding
octave. Existing decoder mapped prohibited values into pitches. Only this HPS
branch now records Reserved0x105 and a timed ScoreNop after consuming the known-
width gate; no voice/retrigger/generated gate-off. Preserve Duration, resume
valid following events and preserve independent tracks. ATR six-bit WaveIDs
are excluded before this check. Existing zero-gate skip and00/FF grammars are
unchanged; this is not complete zero-gate spec validation. No DSP/timbre/gain/
balance/vibrato/controller/voice allocation retuning or per-song correction.

Original synthetic tests cover62 prohibited bytes excluding00/FF x4 gates x3
voice types (GM/customFM/WT), silent reserved-only timelines, independent HPS
tracks, seek/block invariance, all192 legal key encodings x3 voices, ATR WaveID
exclusions and previous HPS-meta exclusions. The old score-NOP fixture used
prohibited0x20 for C60; replace with legal0x1c, preserving exactly MIDI60.
Frozen rejected probe fails as expected and unchanged probe passes after this
fixture correction. Candidate dedicated/full audio/security tests pass. All12
native baseline files retained; only ma_player.cpp differs. Host aggregate
trace is the frozen parser plus callback only, no renderer/DSP changes.

Raw aggregate scan of382 originals:5 HPS files,6468 sounding HPS notes,0
prohibited values. This does not audit gate-zero notes or raw complete syntax.
Fresh382 whole-score event fingerprints/sourceTrack/clocks/bounds match the
frozen baseline; all provenance valid (6468HPS/331466MA7 notes,hostEv32bytes).
All5 affected-grammar HPS originals and6 prior problem originals fully rendered:
every report field and stereo-float hash matches; all finish naturally. There
is no field whitelist. No fresh382-file audio render this batch; prior batch's
382 first10s results are historical, not claimed as new waveform verification.
No real-corpus audible improvement or hardware timbre claim. This fixes invalid
note robustness, not the accepted valid sound or proprietary instrument laws.
Source/reports saved to OneDrive/MMF Player/hps-note-data-next; no private music,
manufacturer PDF, binaries, APK or signing keys included. Main6.1sol handled
this bounded parser work without new delegation.

## Pending native batch: HPS reserved FF boundaries

Source-only after Mobile EOS clocks; no APK/publication. Installable remains
1.6.3-beta.5/code63/cache42; increment all3 before next batched APK.
PDF skill extraction and full visual inspection of SMAF3.05 p38 confirms HPS
defines only FF00 NOP and FFF0 exclusive, with other FF subtypes Reserved and
no specified payload length. Remove the guessed one-byte MIDI-meta framing:
record Reserved/incomplete and stop only that HPS stream, preserving earlier
gates/releases and independent tracks. Missing subtype is still genuine
truncation; exact-end reserved subtype does not invent a missing length byte.
Defined HPS messages/EOS, ATR, Mobile1/2 and MA7 rules unchanged. No DSP/gain/
timbre/balance/vibrato/controller/note/voice-allocation retuning or per-song rule.
This does not implement unknown vendor meta or recommended sequence EOS mute.

Original synthetic hps-meta-regression covers all254 reserved subtypes, GM/
customFM/WT, fake note/volume suffixes, reserved-only false initialization,
single-byte/VLQ-looking payloads, exact-end/missing-subtype audits, independent
HPS/modern streams and seek/block invariance. Defined HPS NOP/exclusive/EOS and
existing Mobile/MA7/ATR exclusion tests pass. Frozen source fails suffix/audit
oracles and passes exclusions. An old ATR exclusion fixture incorrectly used
HPS FF2F00 MIDI-meta framing; its marker now uses defined FF00 NOP while
retaining the resume/timing assertion. The254 reserved cases are covered by
the new prefix/stop oracle, not removed. Final dedicated/full audio/security
tests pass. Only native ma_player.cpp differs from the saved baseline; no new
ABI/event layout or per-frame allocation change. Main6.1sol handled this bounded
parser work without new delegation.

All382 first10s complete reports exact, including every stereo-float hash,
estimated/probed duration, diagnostics and audit counts. Entire-score event
counts/fingerprints/sourceTrack/NOP/EOS clocks/bounds/coverage/reset counts
exact. All source provenance valid,6468HPS/331466MA7notes,hostEv32bytes. Prior16
full confirmation inputs and all5 HPS scores fully rendered: complete reports
and entire-score inventories exact. Groups can overlap; not21 unique songs.
All six strict datasets pass with no report-field whitelist. Other corpus not
fully rendered; no real-corpus audible or reference-chip timbre gain claimed.
Source/reports destination OneDrive/MMF Player/hps-meta-next, no private media,
manufacturer PDF, binaries or signing keys. Compatibility risk: undocumented
vendor HPS FF now stops its stream instead of interpreting a guessed suffix;
the supplied real corpus has no affected outputs/inventories.

## Pending native batch: Mobile EOS clock preservation

Source-only after HPS note-local FM allocation; no APK or publication.
Installable stays1.6.3-beta.5/code63/cache42; increment all3 before batching APK.
Manufacturer SMAF3.05 p45 was extracted and visually inspected using the PDF
skill: Mobile Standard EOS FF2F00 is a timed event, unlike HPS. The old parser
validated its terminator but discarded its Duration from the scheduled score.
Only valid Mobile format1/2 EOS now schedules an inert ScoreNop with marker
a0x2f. Missing/nonzero terminators still preserve their prefix without adding
a clock or accepting a fake suffix. HPS/MA7, gates, source ownership, FM/WT/
Audio/ATR releases and all DSP/timbre/gain/balance/vibrato policies unchanged.
No per-song corrections. This retains EOS time, NOT recommended sequence mute;
late gates may still define the ending. Held/XOF safety horizon uses the same
existing endpoint+10s and600s cap as an explicitly authored NOP.

Dedicated synthetic regression covers GM/customFM/WT, multi-byte/zero/late EOS,
packed Mobile, independent tracks, gate after EOS, held/XOF, suffix rejection,
seek/block, EOS-only delay/cap and all255 nonzero/missing terminators. Frozen
source fails the clock oracle and passes malformed/HPS/MA7 exclusions. Full
audio regression and security smoke pass. Astra bounded design review supports
the clock-only scope; final additional review could not run due usage limits.
Main6.1sol completed differential source and test review.

All382 first10s actual samples/lengths/diagnostics unchanged. Musical event
order/values/source/reset/audit data exact.316 accepted EOS clocks;46 whole-score
endpoints/precise total bounds grow, only12 rounded estimated-time reports
change.34 endpoint changes are one sample, not600s-cap exclusions. Previous16
problem/reset originals fully rendered: all hashes/lengths unchanged, natural
endings. Additional46 endpoint-changing originals fully rendered: eleven exact
frame counts/hashes change, nine rounded rendered-time reports change;41->42
natural endings and four remaining safety endings. The initial old-prefix
guard correctly failed at a moved safety fade; its partial report is preserved.
An independent frozen renderer with only host EOS tracing converts accepted
EOS to same-Duration explicit FF00 NOP+EOF in private in-memory copies. All46
full outputs match that reference bit exactly, including natural/safety flags;
an independent rerender also matches. All six strict report/event/provenance
datasets pass, hostEv remains32bytes. Other corpus not fully rendered.

Risk/next device check: authored late EOS can extend silent rest, release or
held safety tails by tens of seconds in some files. This is not an audible
timbre improvement or a guarantee of unchanged continuous-play end latency.
Recommended per-sequence EOS mute remains pending origin-safe voice ownership;
channel-wide mute would harm independent tracks. No guessed per-song gap trim.
Source/reports destination OneDrive/MMF Player/mobile-eos-clock-next; private
music, manufacturer PDF, binaries and signing keys are not packaged.

## Pending native batch: HPS note-local FM allocation

Source-only after MA7 recorded-Audio source ownership; no APK or publication.
Installable stays1.6.3-beta.5/code63/cache42; increment all3 before batching APK.
An inert HPS MTR could set Chan::monophonic on shared Mobile/MA7 channels,
forcing overlapping modern FM notes to replace each other. A trailing default-
false Ev::handyPhoneNote is set only on HPS score NoteOn. The existing mono FM
slot reuse now follows this note flag, not a global channel initialization flag.
HPS retrigger still reuses any active same-channel FM slot, including foreign
format voices. WT/Audio pools, ATR WaveOn, NoteOff, controllers/patch tables,
DSP/timbre/gain/balance/vibrato/gates/UI unchanged; no per-song corrections and
no claim of full mixed-format channel namespace or hardware isolation.

Distinct MTR1(HPS)/MTR0(modern) synthetic overlapping notes on channels4..7
cover GM/custom FM, same/different keys, both storage orders, packed Mobile,
seek/blocks, one sounding note and WT exclusions. Frozen renderer fails audible
polyphony oracle and passes exclusions; final dedicated/full audio/security
tests pass. HPS48 retrigger and modern18 standalone outputs at8/44.1/48kHz
are bit exact versus frozen. Android NDK armv7a/aarch64/x86_64 syntax-only
assertions confirm Ev32bytes before/after: bool fits existing padding. No APK
build/device test. Existing Astra read-only bounded review no serious issue.

All382 first10s complete reports exact, including estimates/diagnostics/audits;
entire-score pre-existing event fields including sourceTrack, non-NOP hashes,
NOP clocks/endpoints/reset counts exact. New bool excluded from fingerprint;
separate tag consistency and HPS-only complete-tagging checks pass382 inventories,
6468 HPS and331466 MA7 source notes. Previous6 problem originals+ATR harvest/
Stab+8 reset-bearing originals fully rendered:16 exact reports/inventories,
16 natural endings. All5 dataset strict comparisons pass without whitelist.
Other corpus files NOT full rendered. This guards a synthetic reproduced audio-
loss condition; no real-corpus audible timbre improvement is claimed.
Source/reports destination OneDrive/MMF Player/hps-note-policy-next.

## Pending native batch: MA7 recorded-Audio originating MTR

Source-only after Mobile unknown-meta boundaries. Installable DEV remains
1.6.3-beta.5/code63/cache42; increment all3 before the next batched APK.
The parser and wave bank already store Mwa samples per originating MTR, while
MA7 channel state is shared. A later MTR's initialization overwrote the source
channel marker and earlier MTR notes could select that later MTR's same-ID
recording. Each decoded MA7 NoteOn now retains its own sourceTrack. Recorded
Audio uses that event marker for wave lookup and its existing native-pan
namespace. Missing own-source wave remains missing, not a cross-MTR fallback.
Legacy Audio keeps Chan::streamTrack. Controllers, FM/WT patch selection,
voice allocation, DSP/timbre/gain/balance and timing/gate policies unchanged.
Shared MA7 channel/controller/voice-table isolation is NOT implemented here;
this is not proof of fully hardware-faithful multi-MTR playback.

Manufacturer SMAF3.05 pp46/122/123 were extracted and visually inspected in
full using the PDF skill. Their score/stream-wave nesting supports preserving
parsed ownership; they do not define MA7 native channel-sharing laws. No
proprietary ROM/PDF is packaged. PEG/KSR/KSL/fixed-frequency extraction was
also explored, but supplied no new calibration law; DSP is not guessed.

New original synthetic two-MTR fixtures use same Wave ID but distinct samples,
against an independently authored single-MTR/two-distinct-ID oracle. Cover
same/different channels, both compact channel banks, track IDs0/1/201/255,
reversed storage order, missing own wave, future/live native pan, seek/blocks,
and live pan after a slot was stolen by another MTR. Old pan fixture's explicit
last-MTR-wins assumption was replaced by its proper source-scoped expectation.
Final dedicated/full audio/security tests pass; frozen source independently
fails source/missing/pan probes and passes single-source/exclusion suites.
Bounded Astra review found no serious issue. Trailing event int can increase
host event size24 to32 bytes; about3.2MB per400000 logical entries, excluding
vector spare capacity. The event cap remains and no per-frame allocation was
added. Host provenance inventory confirms event32 bytes. All382 first10s
complete reports are exact, including estimates/audits; existing entire-score
event fingerprints/counts/NOP clocks/endpoints/coverage/reset counts are exact.
The fingerprint deliberately covers pre-existing fields, not the new source
tag; source tags are independently inventoried. All382 provenance reports
valid,331466 MA7 notes correctly tagged. Their61 MA7 files each have one MTR;
none has multiple MA7 MTRs, so synthetic fixtures demonstrate the repaired bug,
not a real-corpus audible improvement. Previous6 problem originals, ATR harvest,
Stab and8 reset-bearing originals full-rendered:16 complete reports/inventories
exact,16 natural endings. Full-target provenance confirms8151 MA7 note tags;
other full-original groups have no MA7 notes. Strict5-dataset comparisons pass
every existing report field without whitelist; all provenance checks pass.
Other corpus files NOT full rendered. Source/reports ready for separately
verified OneDrive delivery. No Android build or Git publication.

## Pending native batch: Mobile unknown FF subtype boundaries

Source-only after remembered-velocity reset; no APK or Git publication.
Installable DEV remains1.6.3-beta.5/code63/cache r42; increment all3 together
before the next batched APK. SMAF3.05 pp42/45 were extracted and visually
checked in full: Mobile Standard specifies FF00 NOP and FF2F00 EOS, not
generic MIDI meta length framing. An unknown FF subtype now emits one
Unsupported sequence diagnostic, marks coverage incomplete and stops only
that stream. It no longer guesses a byte-length payload and resumes at an
invented boundary. Prefix notes, gates, live voice decay and independent
tracks are retained. FF00/FF2F00, MA7 VLQ meta compatibility, HPS and ATR are
unchanged; no DSP/timbre/gain/balance or per-song rules were changed.

Original synthetic fixtures cover all254 unknown subtype values, exact-end
subtypes versus truly truncated FF, fake byte/VLQ lengths and note/controller
suffixes, GM/custom FM/WT prefixes, packed Mobile, seek/block invariance and
independent tracks. Valid NOP/EOS/reserved statuses and other-format suites
remain included. The truncation audit fixture now uses missing FF2F final00,
a known-width command, rather than guessing unknown FF01 payload shape.
Dedicated candidate tests pass; frozen source fails the separate audible
suffix and unsupported-inventory probes, and passes valid/exclusion tests.
Bounded Astra read-only review found no serious issue or mandatory new test.
Dedicated/full native audio and security tests pass. All382 first10s complete
reports are exact, including estimated bounds/audits. Their whole-score event
counts/fingerprints/NOP clocks/coverage and reset counts are exact. Six previous
problem originals, ATR harvest, Stab and8 reset-bearing originals were fully
rendered: all16 complete reports and whole-score inventories are exact; all16
end naturally. Strict comparison passes every field without a whitelist.
Other corpus files were not full rendered. No audible/reference-chip fidelity
gain is claimed: the synthetic tests prove a formerly unsafe boundary, while
real-file regression checks demonstrate preservation. No new APK. Source and
verification reports are ready for separate OneDrive copy/hash verification.

## Pending native batch: Mobile remembered velocity after Reset All Controllers

Source-only after Mobile7-bit validation, no APK. Installable DEV stays
1.6.3-beta.5/code63/cache r42; increment all3 before the batched APK.
Manufacturer SMAF3.05 p43 was extracted and fully visually checked: Mobile
remembers explicit9n velocity per channel for later8n notes, and Reset All
Controllers clears that memory back to default64. The decoder previously
retained the old value. Only valid Mobile CC121/0 now assigns that channel's
decode-local runVel=64. No dispatcher/voice/DSP/gain/timbre/balance change;
existing sounding-note velocities, channel/track isolation, HPS/ATR and MA7
grammar untouched. Explicit later velocity, including gate0, can overwrite64
normally. Invalid CC121 values remain rejected and cannot reset this memory.

The old reset-test oracle used ordinary controller writes, which do not reset
decode-local velocity. Its one subsequent8n note now uses independent explicit
velocity64 instead. New original fixtures cover GM/custom FM/WT, all16 channels,
previous velocity0/1/32/55/100/127, gate0 remembered mute becoming64, repeated
reset, same-tick before/after ordering, later explicit gate0 override, other
channel/track, packed/seek/blocks, invalid CC121 and both MA7 banks unchanged.
Dedicated/full audio/security suites pass; frozen Mobile-data source separately
fails reset/ordering probes and passes the exclusion/unchanged suite. Bounded
Astra review found no serious issue. This is an authored-control correction,
not per-song gain compensation or a new hardware velocity curve.

382 first10s complete audio/runtime/audit/estimated-bound reports exact;
whole-score event fingerprints/counts/clocks/NOPs/coverage match for382 plus
the previous8 full originals. The corpus has8 inputs with62 routed resets;
their decoded events already remain identical under this corrected rule. They
are additionally full-rendered as reset targets: complete audio/runtime/audit
reports and whole-score inventories are exact, all8 end naturally.
The previous8 full originals have0 routed resets and retain exact complete
reports/natural endings. Unchanged corpus reports do not demonstrate a heard
improvement; synthetic tests directly prove the previously failing condition.
All16 full originals (previous8 plus8 reset-bearing files) end naturally and
retain exact complete reports/inventories; no changed-field whitelist. Other
corpus inputs are not full rendered. Source/test/license snapshot and reports
saved locally to OneDrive; no Android build or Git publication.

## Pending native batch: Mobile7-bit field validation

Source-only work after unknown-width status recovery, no APK. Installable DEV
remains1.6.3-beta.5/code63/cache r42; increment all3 before a batched APK.
Previous pending source repairs retained, no song-specific or DSP retuning.

Mobile note number/velocity, program and pitch-bend bytes must be7-bit data.
Both callers of shared routeController_ (Mobile/MA7 only) require7-bit CC
number/value as well. Previously Mobile fields were masked or partially routed,
which could invent another note, replace an instrument, alter live gain/pan/
pitch or poison the velocity reused by later notes. Invalid fields now audit
Malformed with incomplete coverage, consume their known full widths and
schedule an inert timed ScoreNop; preserve previous live state and other tracks.
Notes first validate their variable gate: an invalid gate retains existing
stop-current-stream behavior, without inserting a rest or committing velocity.
Successfully delimited invalid notes cannot update running velocity, even with
gate0; valid gate0 still updates velocity. Terminal invalid gate0 data is also
audited, not silently accepted. Normal data, source gates/releases/ending law,
FM/WT/mixer/balance/vibrato/filter, HPS/ATR and MA7 grammar stay unchanged.
Manufacturer SMAF3.05 pp43/44 extracted and visually checked to choose scope.
Running-velocity reset for CC121 remains separate compatibility work.

New original fixtures cover every invalid byte128..255 with GM/custom FM/WT,
channels0/15, audible state/time oracles, remembered velocity, zero/terminal
gates, packed transport, independent tracks, seek/blocks, broken/truncated
gates/fields, channel-mode/RPN/NRPN and all valid key/velocity boundaries.
Existing CC120/121/123 audits now distinguish valid0, semantic invalid1
(Unapplied) and malformed128 (Malformed/incomplete); no high-bit semantic
reinterpretation. Dedicated/full audio/security pass. Frozen previous source
fails controls/notes probes and passes valid grammar. Bounded Astra review
found no serious issue. No hardware/YouTube fidelity improvement claim.

Eight full originals retain exact complete reports and natural endings.
Whole-score event inventories for382 corpus inputs plus8 originals are exact
(non-NOP numeric FNV64 fingerprints/counts, NOPs, clocks/bounds, init/complete).
All382 first10s COMPLETE audio/runtime/audit/estimated-bound reports are exact,
with no changed-field whitelist. Native source/test/license snapshot and
reports saved locally to OneDrive; no Android build or Git publication.

## Pending native batch: unknown-width Mobile status fail-safe

Source-only work after explicit NOP/EOS repairs, no APK. Installable DEV stays
1.6.3-beta.5/code63/cache r42; increment all three before the batched APK.
All previous pending bank/pan/length/NOP work is retained.

Mobile Standard status bytes below0x80 are malformed. Reserved F1..FE have
no validated payload width. Previously the decoder tried the following bytes
as another duration/status, which could fabricate suffix notes/controllers.
Stop just that stream after the existing malformed/unsupported audit. Preserve
all previously generated notes, gates, natural release and independent tracks.
No synthetic mute, NOP, horizon change, DSP/timbre/gain/balance or song patch.
A0/D0 have specified widths and retain exact-payload skip/continue behavior.
MA7 has a different low-status/channel-bank grammar and is NOT changed; HPS,
ATR, Mobile F0 and existing FF meta handling are also untouched. Manufacturer
SMAF3.05 p42 was extracted and visually checked before selecting this scope.

Original tests cover all128 invalid low-status bytes and all14 unknown-width
F1..FE values with FM/WT prefix sound, valid-looking false-note/controller
suffixes, terminal status, seek/block replay, representative packed transport
and independent tracks. Valid A0/D0 exact widths across all16 channels and
both MA7 channel banks across32 channels retain their behavior. Dedicated,
full audio and security tests pass. Frozen previous source separately fails
low/reserved false-suffix probes and passes the valid-grammar suite. Bounded
Astra review found no serious issue. Broken files that relied on guessed
resynchronization may lose their suffix; this is intentional fail-safe behavior,
not a claim that every unknown command or broken file is now supported.

382 first10s COMPLETE audio/runtime/audit/estimated-bound reports are exact.
All whole-score event inventories also match (non-NOP ordered numeric FNV64
fingerprints/counts, every exported NOP, endpoint, bound and scan completion).
Six prior problem originals plus ATR harvest and Stab retain identical FULL
reports, event inventories and natural endings. The other corpus songs are
NOT full rendered. Event fingerprints are not reference-chip fidelity scores
or whole-corpus listening comparisons. No perceptual sound-quality gain is
claimed from these unchanged files. Source/test/license snapshot and reports
saved locally to OneDrive; no Android build or Git publication.

## Pending native batch: explicit score NOP time and EOS shape

Source-only work after the length-overflow repair; no APK. Installable DEV
remains1.6.3-beta.5/code63/cache r42. Before the next batched APK increment
all three identifiers; previous pending bank/pan/length work remains included.

HPS score, Mobile (packed/unpacked) and existing MA7 FF00 NOP grammars now
schedule the existing inert ScoreNop at their authored time. Previously the
byte grammar advanced the decode clock but retained no terminal event, so
late rests could vanish from the event horizon or a held/XOF source could
hit the ten-second safety tail before that authored time. NOP does not change
voices, gates, controls, filters or stable same-tick ordering. ATR keeps its
existing source-specific NOP/trigger policy; no double insertion.

Mobile/MA7 FF2F00 requires the final byte to exist and equal zero. Missing
or nonzero bytes are malformed/incomplete and terminate only that stream,
without decoding any suffix as new notes or muting valid prefix sound. Normal
EOS remains the previous compatibility policy: this batch does NOT schedule
EOS time, introduce a score EOS mute or shorten/retune natural release.
Manufacturer SMAF3.05 pp38,45 were extracted and visually checked; exact NOP
and EOS shapes inform this bounded scope, not an invented handset sound law.

Original synthetic fixtures cover three score formats, an independent unchanged
pan-control timeline oracle, late rests after natural WT decay, held/XOF safety
horizon, split durations, simultaneous notes/gates, following note time, seek,
block invariance and packed transport. NOP-only inputs follow the existing
control-only policy, including zero-time empty output and600-second hard cap.
400001 NOPs hit the decoded-event ceiling with an audit; EOS byte1..255 and
missing bytes preserve prefix/other tracks and reject false suffix notes.
Valid EOS and existing HPS/ATR ending policies remain covered. Dedicated/full
audio/security tests pass; frozen length-repair source fails timeline/eos/limits
negative probes. Bounded Astra review found no serious issue.

382 first10s audio fingerprints, runtime counters and audit fields are exact.
102 ESTIMATED upper bounds extend; the other280 complete reports are exact.
Private whole-score inventory independently fingerprints every non-ScoreNop
event, verifies previous inert events survive in order and permits only added
inert NOPs. Per-file exact frame bounds must equal max(previous score endpoint,
latest added NOP)+the existing10s safety allowance, capped at600s. Rounded
report seconds must agree with those frame bounds. This is not a blanket
duration whitelist. Six prior problem originals plus ATR harvest and Stab
retain identical full audio/runtime/audit reports and natural endings; four
of their estimated bounds change by the same validated NOP-only rule.

The corpus was not rendered in full beyond those eight originals; held-source
tails may intentionally last longer when a late NOP extends their old safety
horizon. Unchanged first10s fingerprints do not prove whole-song perceptual
identity. No timbre, mixer, vibrato, filter, UI/mascot or song-specific tuning
changes; no reference-device listening comparison. Native source/test/license
snapshot and reports saved locally to OneDrive. No Android build or git publish.

## Pending native batch: representable score lengths after MA7-pan work

Source-only work, no APK. Installable DEV remains1.6.3-beta.5/code63/cache r42;
the preceding silent-bank/recorded-Audio pan changes are also still unbuilt.
Increment all three identifiers before the next batched DEV APK.

The five-group VLQ reader formerly accumulated35 possible data bits directly
in uint32_t. Terminal values above UINT32_MAX wrapped into small gates,
durations or payload sizes, allowing bogus notes or definitions to be decoded.
Accumulate in uint64_t; narrow only after termination and representability
checks in the shared score-length reader. Unrepresentable lengths follow the
existing malformed/incomplete-coverage, stop-current-stream path BEFORE any
duration addition, note allocation or payload boundary use. Preserve normal
prefix events, previous setup definitions and independent other tracks.

This is a bounded-reader compatibility repair, not a new claim about the
manufacturer's maximum VLQ byte count. All previously accepted32-bit values,
including UINT32_MAX and leading-zero five-byte forms, remain accepted. HPS
uses its existing separate one/two-byte grammar. Normal FM/WT synthesis,
balance, vibrato, filters, UI and score ending/decay policies are unchanged.

Original fixtures cover first-group values16..127 and low wrapped0/1/10/127,
32/35-bit boundaries, Mobile/MA7/packed Mobile, duration/gate/Exclusive/MA7
meta/setup lengths, prior definitions, prefix and other-track recovery, reinit,
seek/block invariance. Legal huge clocks are compared with independent10ms
tick arithmetic at8kHz using only bounded audio prefixes, not long renders.
Frozen post-beta.5 MA7-pan source fails overflow/recovery probes; the same
valid-boundary suite passes before and after the repair. Bounded Astra review
found no serious issue. No private media or proprietary synthesis data added.

Final dedicated/full audio/security suites pass.382 first10s COMPLETE reports
and eight full original reports are exactly unchanged from frozen MA7-pan
source, across every field with no whitelist. The eight originals finish
naturally. This corpus has no audio affected by the repaired overflow path;
no listening or perceptual improvement is inferred from unchanged hashes.
Verified native source/test/license snapshot and reports are saved locally
in OneDrive. No Android build/lint/ABI check or git publication in this batch.

## Pending native batch after beta.5: MA7 silent bank and recorded-Audio pan

Source changes only; no new APK or Android lint/ABI result in this batch.
Installable DEV remains1.6.3-beta.5/code63/cache r42. Before the next APK,
increment the DEV version, versionCode and conversion cache revision together.
Public1.6.2 and all previous local repairs remain unchanged.

Manufacturer MA7 guideline1.60 pp22 and40 were extracted and visually checked.
SP-MIDI bank MSB121/LSB6 is silent for MA7 notes, including embedded FM/WT
and substitutes. Preserve the existing allocation/gate policy; later gain
changes cannot revive these notes, and switching bank cannot silence an
already-held ordinary note. Legacy source events keep their previous behavior.

Exact native Exclusive body `43 79 08 7F 0B ID CL Data` controls recorded
MA7 Audio, not WT instruments, FM, ATR or legacy recorded Audio. ID0..31 maps
to Mwa1..32 within the source MTR. CL0 applies constant-power pan0..127; CL1
clears ALL previous wave overrides in that MTR and restores current channel
pan; CL2 disables pan for the selected wave, giving unity L/R, not a centered
-3dB position. Setup, future notes, live voices and seek are covered. Slots
stolen by legacy sources cannot retain the native-source marker. Rendering
uses preallocated state and performs no new map insertion. Malformed known
messages preserve prior state with a timed inert event; event limits remain
bounded. Do not infer extended payloads or other native wire opcodes.

Original synthetic fixtures cover all32 IDs, all128 pan values, exact endpoints,
unity Panoff, CL1 all-clear, live channel-pan interaction, source scope, same-tick
order, seek/reinit/block invariance, invalid fields/lengths,400001-command
resource flood and legacy slot reuse. Frozen beta.5 fails bank/pan/live/invalid
negative probes. Dedicated/full audio/security suites pass.382 first10s complete
reports and eight complete originals are exact against frozen beta.5; all
eight finish naturally. No perceptual gain is inferred for unaffected files.

Known limitation: multiple MA7 MTRs still share the existing global channel
association (last decoded MTR wins). This batch does not fix that pre-existing
association or claim hardware-faithful multi-MTR playback. Native0x20/0x22
remain unsupported: static converter inspection did not prove their wire
semantics. No proprietary ROM/DLL contents, guessed PEG/KSR/KSL laws,
song-specific balance, timbre, vibrato, mixer, filter or UI changes are added.
Verified source and reports are saved locally in OneDrive; no git publication.

## Local DEV 1.6.3-beta.5: MA7 mute, invalid data and truncated VLQs

Local candidate only: code63/cache r42; public1.6.2 unchanged. Batch these
generic decoding repairs before one APK build. Manufacturer MA7 guideline1.60
pp22,24-27,33 were extracted and fully visually inspected; no ROM content is
bundled and no guessed timbre, KSR/KSL/PEG or fixed-frequency law is introduced.

- MA7 Velocity1 is exact mute, for explicit and remembered velocity. Tag the
  decoded event itself, not the channel: overlapping legacy tracks keep their
  prior squared Velocity1 gain. FM, embedded WT and missing-ROM/FM substitutes
  retain base velocity0, so later live Volume/Expression cannot revive them.
  Retaining the existing allocation/gate policy is a compatibility choice,
  not a claim about hardware resource allocation. Velocity0 handling and the
  ordinary2..127 curve are unchanged; no SMF NoteOff grammar is inferred.
- MA7 known 7-bit note/velocity/program/bend/controller fields128..255 are
  malformed, not wrapped valid values. Consume their validated fixed width,
  audit the raw data, schedule inert timed NOP, preserve prior live state and
  remembered velocity, and continue with following valid events. Invalid gate
  length stops the track before any note/running-state mutation. Keys115..127
  keep compatibility; the authoring tool's normal-key114 ceiling is not guessed
  to be a compact runtime restriction. Legacy controller decoding is unchanged.
- Unterminated Mobile/MA7 Duration/Gate/Exclusive/meta/setup VLQs stop their
  current stream and expose incomplete coverage, rather than synthesizing a
  false note from a partial gate or feeding an empty fabricated Exclusive.
  Preserve preceding normal events and other tracks. Accepted terminal
  five-byte VLQs (including leading zeros) retain their previous meaning.

Original fixtures cover both MA7 channel banks, FM/WT/ROM-fallback mute and
live gain, allocation/gates, legacy/mixed scope, all128 high data bytes for
known fields, inert timing and prior running velocity, audible patch oracle,
partial fields, valid key boundaries, truncated1/5-byte lengths and valid
five-byte equivalents, prefix/other-track recovery, seek and block invariance.
Frozenbeta.4 fails new velocity/data/VLQ modes independently. Dedicated/full
audio/security suites pass. All382 first10s complete reports are exactly
unchanged, including5 HandyPhone,316 Mobile,61 MA7; six prior problem originals
and the ATR and HPS modulation originals retain exact full reports and natural
endings. This corpus has no output affected by these corner cases; no listening
or perceptual gain is inferred from unchanged hashes. Normal playback UI,
mascot and FM core remain unchanged. One offline lint/assembleDebug build;
zero errors/four existing warnings,3ABIs and existing debug v2 signer verified.
Local OneDrive DEV delivery only; no git publish.

## Local DEV 1.6.3-beta.4: HandyPhone reserved controls and known modulation

Local candidate only: code62/cache r41; public1.6.2 unchanged. One APK build
after a bundle of controller repairs and independent synthetic/corpus checks.
Manufacturer SMAF3.05 pp32-37 were extracted and fully visually inspected.

- Standard Program/Bend/Volume/Pan/Expression128..255 are reserved, not masked
  low7-bit values or extended bends. Keep prior settings; do not wrap to mute,
  another patch, another stereo position or out-of-range pitch. Bank retains
  its full byte, including the valid bit7 drum-bank marker.
- Octave Shift0,1..4,0x81..0x84 remain absolute valid shifts; all other values
  retain the previous shift, not an invented reset to zero. Explicit0 still
  resets, repeated valid commands do not add octaves. Pitch base+36 unchanged.
- Short Expression/Bend/Modulation0/15 are reserved. Channel0 short-expression
  zero shares the existing extended/EOS grammar and stays in that path. Valid
  short Expression1..14 and Bend1..14 keep their existing specification tables.
- Known standard HPS Modulation is parsed but physically unapplied, not an
  unsupported byte grammar. Schedule inert ScoreNop to retain its authored time.
  Reserved fixed-width commands retain time the same way. Short Modulation
  also uses this inert event, never MA7's different wheel law on a deliberately
  colliding mixed-format channel. No guessed HPS vibrato curve is introduced.

Original binary score fixtures test every reserved standard byte, every invalid
octave with all8 nonzero valid prior shifts, all256 Bank bytes, all channels /
reserved short values, all14 Expression/Bend table entries, explicit/repeated
octave reset, live commands, EOS ambiguity, source-free notes, block invariance
and seek. Independent terminal-timeline oracle uses a synthetic fast-release
WT ending before a1000ms control; the silent endpoint is retained. Mixed FM/WT
fixtures prove their intrinsic vibrato actually responds to a real MA7 wheel,
then assert that HPS modulation cannot trigger it. Test fixtures themselves use
legal HPS two-byte lengths; no private song/ROM data is included.

Frozenbeta.3 fails new long/octave/short/modulation modes independently. Final
dedicated/full audio/security suites pass. Fresh382 first10s fingerprints,
duration, peaks and all runtime note/fallback/steal/tail metrics are exact.
381 reports are exact across every field. The only differences are two counters
on one HPS score: three standard Modulation instructions move from unsupported
to unapplied. Independent whole-score group audit verifies the same raw values,
times, tracks/channels/counts, with no coverage loss. This is diagnostic clarity,
not proof that their physical vibrato is now emulated. That original's full
audio/timing/note report is also exact and it finishes naturally.

Six prior problem originals and the ATR-bearing original retain exact full
reports/natural endings. No manufacturer listening comparison or perceptual
improvement is claimed for these unchanged files. FM core, UI, mascot and main
playback control hashes remain unchanged; no song-specific gain/vibrato patch,
new mixer law, filter, limiter, pitch range or timbre retuning. Android offline
lint/assembleDebug pass, zero errors/four existing warnings,3ABIs and verified
existing debug v2 signer. Local OneDrive DEV delivery only; no git publish.

## Local DEV 1.6.3-beta.3: ATR Pan and strict HPS lengths

Local candidate only: code61/cache r40; public1.6.2 remains unchanged. Batch
three fixes and their independent regressions before one DEV APK build.
Manufacturer SMAF3.05 pp29,31,51,53,55 were read and visually checked.

- Explicit ATR Pan0..127 uses the recommended constant-power cosine/sine law
  (p53), with exact opposite-side silence at endpoints. Track/local-channel
  coefficients affect both live and future ATR voices, not score/FM/WT or Mwa.
  Pan and the existing Volume/Expression product are independent; repeated
  commands do not compound gain. Reserved128..255 retain the prior Pan.
  Unspecified Pan keeps the prior unity L/R compatibility default, rather than
  silently recalibrating existing recordings. An explicit center value uses
  the documented law without makeup gain, so it differs from that default.
- HPS Duration/GateTime/Exclusive length requires a one-byte value or exactly
  two bytes with the second MSB zero (p29). Missing/invalid continuation stops
  that track and records malformed/incomplete coverage. No guessed Duration,
  GateTime or fake NoteOn; previously valid events and other tracks remain.
  Valid127/128/16511 boundaries and all Mobile decoding are unchanged.
- Only FF00 and FFF0 have defined ATR widths (p55). Other FF subopcodes are
  reserved: record incomplete coverage and stop that sequence, rather than
  guessing a MIDI-meta payload size. This is not EOS; valid prior sources keep
  their authored gates/natural ends. Retain existing HPS score meta compatibility.

Original fixtures cover every Pan value/reserved byte at8/44.1/48kHz, exact
endpoints, quiet independent stereo gain oracles (1e-7 roundoff/bounded filter
drain), live movement and Volume/Expression interaction, repeated commands,
track/channel/WT-slot isolation, block invariance and seek/reinit. Binary ATR
and HPS score fixtures cover wide-length boundaries, malformed gate/duration/
exclusive/setup, valid-prefix preservation and other-track continuation.
Unknown-FF fixtures verify no fake later trigger and no automatic EOS mute.
The frozen beta.2 engine fails each of the three new test modes; the candidate
dedicated/full audio/security suites pass. No private music is embedded.

No timbre, DSP/filter/limiter, FM core, UI, mascot, per-song balance or vibrato
retuning. This remains partial ATR support: pitch bend, modulation, device
effects, other codecs and unverified manufacturer synthesis are not claimed.

Fresh same-compiler44100Hz first10s probes:382/382 pass, all382 reports exact
across every field (HPS5/Mobile316/MA7 61), including stereo float fingerprints.
The six prior reported originals also retain exact full reports and all end
naturally. The ATR-bearing original retains its exact full report (42.993s,
peak0.609, hash4c22f2729a6a72d0). These private files do not exercise a newly
audible Pan change; independent fixtures demonstrate that fix. No listening
comparison or manufacturer-bit-exact sound is claimed.

Offline Android lint/assembleDebug pass: zero errors/four existing warnings,
three ABIs and verified existing debug v2 signer. Normal UI/orange DEV identity
retained. Local OneDrive DEV delivery only, no public source/release mutation.

## Local DEV 1.6.3-beta.2: batched ATR stream/control improvements

Local candidate only: code60/cache r39; public1.6.2 remains unchanged. At the
user's request, batch changes and validation before a single DEV APK build.

- Explicit ATR EOS now mutes that source track at sequence time, as recommended
  (not mandated) by SMAF3.05 p51. It stops all of that track's overlapping local
  channels, not other ATR tracks, score WT/FM, or recorded Mwa Audio. Keep shared
  output filter history. Stop decoding after EOS; absent EOS retains GateTime /
  natural-end policy, not an invented automatic EOF mute. Early EOS also bounds
  that track's safety contribution when an unrelated held voice remains.
- Timed ATR NOPs after a valid trigger retain their authored timeline, even when
  samples end earlier. A stream containing only NOPs/controllers/EOS, or only
  missing wave references, cannot manufacture successful playback.
- Authored ATR Volume and standard/short Expression update both live and future
  voices using the recommended squared gain `(value/127)^2` (pp53-54). Separate
  track/local-channel state prevents control leakage into score channels120-123.
  Recompute the product of gains; repeat commands do not accumulate attenuation.
  Short Expression1 means mute;2-14 map to31,39,...127. Reserved short0/15 and
  standard128-255 retain the previous gain. Initial unity gain remains the prior
  compatibility policy; do not infer an undocumented initial Volume calibration.
- Unsupported ATR pitch/pan/modulation and reserved score-only commands do not
  mutate score state. Audit them accurately and retain time when appropriate.
  ATR exclusive messages are device parameters, not MTR instrument definitions;
  they cannot activate/replace score voices. Device-specific effects remain
  unapplied. No new pan law, pitch range, timbre, mixer/EQ or song-specific patch.

Original stream tests cover exact truncated-source EOS at8/44.1/48kHz, timeline /
track / slot isolation, simultaneous events, later EOS suffix, seek/init reset,
NOP silence, long-source/held-WT horizon, and missing-source rejection. Quiet
sample-domain gain oracles allow only1e-7 floating roundoff and bounded filter
drain; live mute retains exact filter history. Test all short Expression values,
reserved controls, new/repeated/live gains and deliberate score channel120
collision. ATR exclusive isolation uses an independently parsed valid WT patch.
Old header/controller/container fixtures that appended immediate EOS but expected
an audible one-shot now omit explicit EOS; their original functional assertions
are retained. Dedicated tests replace that incorrect EOS-ignoring assumption.

All bundled synthetic/audio/security tests pass. In382 private first10s probes,
every audio fingerprint/probed duration is unchanged and all381 non-ATR reports
are exact. Six prior reported songs have exact full reports/natural endings.
The one ATR-bearing song also has exact full audio (42.993s); its upper-bound
duration estimate changes from52.972s to52.980s because of retained NOP timing,
not added audible output. Those private files do not demonstrate the new gain /
EOS cutoff behavior; the independent original fixtures do. No human listening
or manufacturer-exact synthesis is claimed.

## Local DEV 1.6.3-beta.1: ATR authored GateTime

Local candidate only: code59/cache r38; public1.6.2 is unchanged. ATR stream
Wave messages now stop at GateTime * the track's own G clock (SMAF3.05 p51).
Previously the value was read but discarded, so recordings always ran to EOF.
Each trigger has a separate ATR gate identity: overlapping identical wave IDs,
different ATR banks, stolen/reused slots and score CC120/121/123 cannot stop
the wrong source. Score key-off identity remains separate from ATR identity.

Stop the source at its gate, retaining the existing shared filter/limiter tail;
do not invent a release envelope, tune an instrument, or change song balance.
ATR natural EOF can still finish first. Only a suffix consisting entirely of
generated ATR gates may be ignored once ALL voices and filter history drain.
Later authored triggers/controllers are never skipped. The safety horizon uses
the lesser of gate and decoded source length for ATR, not a distant orphan gate
that could extend an unrelated held FM/WT voice to the600s global ceiling.
The estimated endpoint can still exceed actual playback after slot stealing.

Original synthetic tests use independently truncated recordings as exact
oracles at8/44.1/48kHz, including distinct D/G clocks, two-byte HPS gate lengths,
same-channel overlap, track-local banks, ATR/WT slot reuse, seek, block sizes,
zero/reserved gates, orphan gates with later events, mixed held WT safety and
genuine12s recordings. Old controller-isolation fixtures now explicitly hold
their ATR recording for2s through the controller, rather than relying on an
ignored1ms gate; functional assertions are unchanged.

Candidate gate/audio/security tests and Android lint pass (0 errors/4 warnings).
Compared with frozen public1.6.2, all382 first10s reports are exact, and the six
previously reported songs plus the one ATR-bearing song have exact full-render
reports and natural endings. The existing ATR source uses gates that do not
truncate its samples; the newly corrected cutoff is demonstrated by original
synthetic fixtures, not a claimed audible improvement of those private songs.

FM/WT timbre, pitch, gain, modulation, EQ and player UI are unchanged. This is
not full ATR emulation: EOS muting, ATR-specific controller semantics and other
unsupported codecs remain separate work. No manufacturer-exact listening or
subjective improvement for every song is claimed.

## Public 1.6.2 scope

Version1.6.2 packages the accumulated1.6.2 development corrections through the
ATR metadata and empty-chunk boundary fix, with code58/cache r37. Below are
historical development and listening records; earlier private/beta status does
not describe the current public artifact. Withdrawn DC-conditioning experiments
are not enabled by default. This release retains the accepted generic mix,
with no new song-specific volume/vibrato correction or recovered device ROM.

WAV files now play directly alongside MMFs; only MMFs enter conversion and the
optional handset-output effect. Implemented Mobile controllers, timed voice/
wave definitions, custom FM profiles, sampled interpolation/downsampling and
ATR source identity have additional original synthetic tests. Unsupported
MA-7 AL/PEG/fixed-frequency and ROM/device effects remain incomplete. ATR
metadata/source support is not full gate/EOS/control emulation. Automated
finite/audio regression checks do not certify manufacturer-exact listening.

## Private DEV 1.6.2-beta.15: VM35 phase-modulation scale

This local listening candidate uses code51/cache r30; public1.6.1 is unchanged.
The user's private controlled DQ4 comparison strongly preferred the4-cycle
experiment over the prior0.42-cycle compatibility scale. At the prior setting,
its saw24/modulator ratio2 and square6 carriers were effectively unmodulated.
This listening result does not establish hardware-exact synthesis or validate
the timbre of all other songs.

Modern-profile parsed VM35 FM tones now use4 cycles per unit modulator output.
The independent reference normalization is2^13 peak output *2^15 phase-input
shift /2^26 phase period in SiON; it is not a manufacturer calibration. No
reference implementation code, vendor ROM, private MMF or recording is included.
This corrects the synthesis-family policy, not a title/hash-specific exception.

The depth is recomputed on every note allocation. All active operators must
have the VM35 parser family flag and none may have the expanded-MA7 flag.
Nonmodern/HandyPhone tones, VMA, expanded MA7, original substitutes, and mixed
or conflicting synthetic flags retain0.42. VM35 tones carried by MA7 sequences
still use their VM35 family; sequence format alone is not the instrument format.
Self-feedback scale, envelope rates, waveform math, MULT/BO, pitch bend,
controllers, part gains, output EQ/limiter, PCM processing and playback UI are
unchanged. Mixed FM/PCM output can change through the existing shared limiter.

Synthetic tests verify explicit operator-edge scale and routing for all eight
algorithms, additive invariance, family scope, reused slots, transports, chunks,
seek and a coherent saw/square spectral fixture. Former cross-family timbre
equality fixtures now compare pan/split/fixed-key behavior within each family,
without loosening their functional thresholds. The FM packing test now uses
the actual fixed bit-carrier layout, rather than generic PCM7-bit groups.

Source references read only:
[SiON operator](https://github.com/keim/SiON/blob/1e6d6cd20bbc0379f5a81f607ac87a105163648f/src/org/si/sion/module/channels/SiOPMOperator.as),
[SiON phase/output constants](https://github.com/keim/SiON/blob/1e6d6cd20bbc0379f5a81f607ac87a105163648f/src/org/si/sion/module/SiOPMTable.as).

The reference handsets are J-SH51 and 905SH. No hardware recordings are available;
neither handset is claimed to be emulated exactly. Phone speaker processing is a
separate optional output effect, not a replacement for accurate synthesis.

## Public 1.6.1 scope

Version1.6.1 packages the current1.6.1-beta.9 engine corrections for public
distribution, with render-cache revision r17. The beta sections below are
historical development records: their references to private/DEV builds or
unpublished trials describe the stage when those changes were tested.

Supported embedded-wave decoding, score Audio, drum pitch, controller/bend
handling, extra MA-7 envelope-rate fields and PCM modulation are improved.
Original ROM-free substitute recipes and the modern FM profile remain
approximations. MA-7 signed8 decoding is experimental; its full command-to-
authoring-format linkage remains provisional. AL filter DSP, calibrated
fixed-frequency synthesis, nonneutral pitch envelopes, accurate device ROM
timbres and effects remain incomplete. No complete handset/reference match
or manufacturer certification is claimed. No private recordings, MMFs,
manufacturer binaries or ROM samples are distributed.

## Private 1.6.2-beta.1 development scope

The published 1.6.1 snapshot is unchanged. This development build uses code37
and cache r18, so earlier cached renders cannot mask these corrections.

- FM PE explicitly locks authored pan, including centre; unlocked FM voices
  follow live channel Pan controllers. ROM substitutes preserve authored PE
  without inheriting uncalibrated PCM envelope/TL values.
- Bank125 sampled drum mapping accepts trigger key zero and retains native
  percussion pitch; an unmapped key is not treated as a melodic catch-all.
- Mobile Hold1 (CC64, threshold64) defers gate-off only for SUS-enabled FM/WT
  operators. Pedal-off executes pending gates without releasing key-down notes.
  Mixed operators, XOF, note identities, slot reuse and seek are tested.
  HandyPhone SUS semantics and Audio/ATR playback are preserved. Compact MA7
  CC64 mapping remains unverified and is deliberately not enabled.
- Inline tone definitions activate at their authored times, in stream order
  relative to same-tick notes. Existing notes retain their allocated patches;
  subsequent notes use the newest definition for that exact instrument/split
  slot. Separate keyHigh boundaries coexist; whole split-layout replacement
  and inline waveform mutation are not implemented.
- Parsed custom VM35 tones in the Mobile/MA7 synthesis profile (packed
  MA3/direct and short MA5 forms) use the
  documented MULT11/13/14 aliases to10/12/15. VMA, expanded MA7 and original
  fallback recipes retain their separate previous multiplier interpretation.
- Modern-profile WS30 is a quarter-square rather than a duplicate of WS14.
  This is corroborated by a related Yamaha diagram and independent MA3 model;
  native MA7 waveform calibration is still unverified. Legacy waveform math
  and the other31 waveform selections are unchanged.

Synthetic fixtures cover temporal tone activation, same-tick ordering,
separate splits, held-note PE/SUS isolation, pan, Hold1, multiplier transport
parity and waveform scope. These are structural corrections, not a guarantee
of listening equivalence. No global EQ, gain, limiter or playback-UI changes
are part of this build. Remaining major work includes calibrated AL filters,
Pitch-EG transport/rates, MA7 fractional/fixed-frequency oscillators, exact
envelopes/key-follow and ROM/device effects. No private MMFs or vendor assets
are added to the source distribution.

## Private 1.6.2-beta.2 development scope

Public 1.6.1 remains unchanged. This DEV checkpoint uses code38 and cache r19.

- Expanded native MA7 FM MULT11/13/14 now select the manufacturer's nominal
  labelled ratios1.414/1.498/0.891, not integer11/13/14. The complete table is
  0.5,1,2,3,4,5,6,7,8,9,10,1.414,12,1.498,0.891,15. Static examination of the
  manufacturer's authoring editor linked selection, MULT property, label
  formatter and serialized upper nibble. These are the displayed decimal
  ratios, not a claim of measured chip phase precision. This applies only to
  expanded MA7 tones in the modern profile; VM35, VMA, HandyPhone and original
  substitute recipes retain their distinct semantics. Four raw MA7 DT bits
  are preserved separately, without guessing a new physical detune model.
- Mobile and native MA7 inline sample-wave definitions activate at authored
  event times, rather than preloading future Mobile definitions or ignoring
  native MA7 updates. Waves, tones and notes preserve mutual same-tick stream
  order. Existing notes keep immutable sample snapshots; later notes select
  the active version. This is a safe snapshot policy, not validated hardware
  behavior for a wave RAM write while a note is sounding. Setup waves and
  recorded Mwa/ATR Audio remain separate; seek restores setup state. Extra
  inline decoded memory is capped at16 MiB and512 versions, before allocation.
  Only the existing validated codecs are used; malformed/unsupported updates
  cannot erase a valid wave. No new ROM, manufacturer binary or sample asset
  is redistributed.

Regression checks cover every multiplier, raw DT isolation, profile/fallback
scope, immutable held waves, timed tone/wave/note ordering, namespace isolation,
seek/block consistency, malformed input and aggregate flood guards. No global
gain, EQ, limiter, UI or playback workflow changes are included. Numerical
tests do not prove perceptual equivalence. AL/PEG, fixed-frequency calibration,
physical detune, full ROM timbres and device effects remain incomplete. The
experimental Hold1 end-of-score caveat from beta.1 also remains unresolved;
this checkpoint is not authorized for public release.

Source: Yamaha AT-MA7 authoring package, User's Manual1.60 and its FM editor
selection/property/label/serializer mapping (local read-only static reference).

## Private 1.6.2-beta.3 development scope

Public1.6.1 is unchanged. code39/cache r20 forces fresh conversion.

MA7 channel pan now follows the MA7 SMAF Authoring Guideline1.60 section8.3.5:
left amplitude cos(pi/2 * value/127), right amplitude sin(pi/2 * value/127).
Default64 uses the same curve as an explicit controller64. Endpoints exactly
mute the opposite side. Previously the linear1+/-pan law made stereo extremes
louder and value127 still leaked into the left side. The specified center64
has a small right/left asymmetry; it is not forcibly rounded to equal gains.
This changes MA7 relative part levels and central amplitude, not just stereo
width. Center is around -3dB per channel vs the prior unity gains, while hard
pan uses unity rather than doubled amplitude. No makeup/global gain is added.

Gains are cached when allocating a note or receiving Pan, not recalculated
per sample. Live Pan follows unlocked held FM, WT and recorded Audio. Seek/
fresh init reset allocations. HandyPhone, Mobile MA3/5 and ATR one-shot pan
math remain unchanged; authored5-bit voice PE/pan is a different register
and retains its previous uncalibrated law. Original substitute timbres, note
gates, filters, limiter and player UI are unchanged. This is a documented
channel-balance correction, not proof of better timbre or a handset match.

Synthetic tests independently check all128 controller values for FM/WT/Audio,
exact mute endpoints/default64, both channel banks, live held updates/channel
isolation, locked PE, reinit, seek and block invariance. Existing transport/
pitch/sample equivalence tests now account explicitly for the new MA7 center
gain; quiet filter-drain differences below tolerance are not media comparisons.

Major unresolved work remains AL/PEG/fixed-frequency/physicalDT, exact ROM
timbres, envelopes and device effects; beta.1 Hold1 end-of-score caveat remains.
Mafu audio-reactive behavior is a separate design only; no mascot code changed.

Source: Yamaha AT-MA7_SMAF_GL_160_e.pdf section8.3.5, printed page27.

## Private 1.6.2-beta.4 development scope

Public1.6.1 is unchanged. code40/cache r21 includes the beta.3 channel-pan fix.

MA7 CC1 Modulation Depth was decoded but dropped. It now controls each FM/WT
voice's vibrato with the documented MA7 SMAF guideline8.3.2/Table6 scale:
0 OFF,1..31 x1,32..63 x2,64..95 x4,96..127 x8. The default is0 OFF.
Updates affect held and future notes without retrigger or LFO-phase reset;
pitch bend remains separate. Operators/WT with EVB disabled stay unaffected.
Only pitch modulation is scaled, not EAM/tremolo. Recorded Audio and ATR
one-shots remain unaffected. MA3/5 and HandyPhone keep the previous behavior;
their precise channel-control semantics are not inferred from MA7 documentation.

The intrinsic DVB depths/LFO speeds remain the existing uncalibrated model.
Applying the documented multiplier is not a claim of exact handset vibrato.
Synthetic tests cover independent operator-phase references, OFF and all bucket
boundaries, inactive EVB, AM isolation, held changes, channel isolation, invalid
values, fresh init, seek/block consistency and Audio exclusion. Source snapshots
still omit AL/PEG/fixed-frequency/physicalDT/device ROM/effects. The beta.1 Hold1
end-of-score caveat remains. Mafu audio-reactive behavior remains design only.

Source: Yamaha AT-MA7_SMAF_GL_160_e.pdf section8.3.2/Table6, printed page26.

## Cross-version audio compatibility checks

`corpus-test` now reports an exact ordered-float FNV64 fingerprint in addition
to renderability/peak/voice diagnostics. `tools/compare-audio-regression.ps1`
compares two reports read-only and can require unchanged HandyPhone and/or
Mobile MA3/5 output. It rejects missing, duplicate or failed entries and
different file sets/track descriptions. Format classification matches the
format field explicitly, not any occurrence of the digit3.

Use identical unmodified input files, sample rate, probe interval, compiler
and optimization settings. The fingerprint is neither a cryptographic input
identity check nor a perceptual quality score. Different platforms/libm may
give different float bits. The probe only covers its specified interval;
it is not full-song or device verification. Private reports/inputs stay out
of the repository.

The current382-file ten-second audit compares public1.6.1 source, beta.2 and
beta.4 using the same host tool/compiler. All three render382/382 successfully.
Beta.2 versus beta.4:5 HandyPhone and316 Mobile files have unchanged output;
all61 MA7 files change. Public1.6.1 versus beta.2:0/5 HandyPhone,201/316 Mobile
and18/61 MA7 files change. These differences do not establish which sounds
better. The earlier beta.1/2 corrections affect MA3/5; the later MA7 pan/CC1
changes do not affect the tested Mobile output. The documented Hold1 pool
saturation/end-of-score caveat remains; do not claim all MA3/5 glitches fixed.

Manufacturer MA5 manual1.3.3 confirms the existing FM/WT LFO selections
1.8/4.0/5.9/7.0Hz and DAM1.3/2.8/5.8/11.8dB parameters. LFO shape, exact
interpolation/phase/depth behavior and physical envelope calibration remain
approximations. The manual does not establish the Mobile CC1 magnification
table; MA7's table is not copied to MA3/5 without its own evidence.

No new synthesis/profile/UI/APK/cache change is made by this audit.

## References

- Yamaha MA-5 Authoring Tool User's Manual, FM MULTI table (printed page 62):
  https://manuals.plus/m/66bfe529849a714a1d213edf827619a5c58de8170e50de271d0a9ea7e8cb2892
  Custom MA-3/5 multiplier values 11/13/14 alias 10/12/15. This must not
  be imposed on MA-7's separate expanded/fractional multiplier transport.
- Independent MA-3 oscillator model, reviewed as corroborating design evidence:
  https://github.com/keim/SiON/blob/master/src/org/si/sion/module/SiOPMTable.as
  The quarter-square WS30 agrees with the related Yamaha waveform chart.
  No SiON source code or sampled sounds are included. Its author's modeling
  is not an independent hardware recording or native MA-7 certification.

- Yamaha YMF825 tone parameters (Yamaha-authored manual retained in a fork):
  https://github.com/junk16/ymf825board/blob/master/manual/fbd_spec3.md
  This documents a related FM engine, not a verified MA-3/MA-7 hardware model.
- https://github.com/but80/smaf825 (VM35/VMA voice field interpretation).
- https://github.com/but80/go-smaf/blob/v1/voice/vm35fm.go and
  https://github.com/but80/go-smaf/blob/v1/enums/voice.go (transport fields/topology).
- Yamaha's MA-5 Authoring Tool User's Manual, sections 4.18.3-4 and 5.1.7,
  [archived manufacturer document](https://manuals.plus/m/66bfe529849a714a1d213edf827619a5c58de8170e50de271d0a9ea7e8cb2892).
  RM chooses ROM/RAM, Fs is the rate at note 60, equal LP/EP means one-shot,
  and PE overrides controller pan. This is a manufacturer reference, not a
  recording of either target handset.

## beta.7 root-cause corrections

The previous absence of Mwa/Awa chunks was **not** proof of ROM dependence.
Some local files carry their samples in MA-3 `43 79 06 7f 03` setup commands.
The supported command form has a wave ID, a zero format byte, then seven-bit
packed ADPCM. Its interpretation is an interoperability inference checked
against local payload lengths, PCM references and synthetic fixtures, not a
complete published specification for every command variant. Other formats
remain disabled. Local sample bytes are never included in this repository.

MA-3 PCM parameters also need bit unpacking. The former interpretation read
transport masks as Fs/envelope/TL fields and even treated RM as a loop flag.
ROM and embedded RAM are now separated; RAM instruments use their authored
wave, sample rate, envelope, pan and loop points. The old blanket PCM TL
compensation on fallback FM is removed. Real ROM timbres remain approximated.

Each note-on and its scheduled gate-off now share an instance ID. Key-only
matching released the wrong same-pitch note, including when gates overlapped
or an earlier note was still decaying. Silent FM modulators no longer retain
voice slots after all output carriers finish. At capacity, quiet release tails
are preferred over held notes. This stealing policy is not a hardware model.

Algorithm 3 is routed as op0 + (op1 -> op2) feeding op3, not (op0 + op1) ->
op2 -> op3. Natural release/filter draining replaces beta.6's fixed +1-second
cutoff. A ten-second tail ceiling and ten-minute absolute ceiling remain;
the last 20 ms fade only at that safety boundary. `totalSamples()` is a bound,
not an exact duration. Diagnostics expose safety truncation and fallback use.

## Validation and remaining fidelity work

`tools/audio-regression.cpp` uses only synthesized fixtures. It covers gate
identity for FM/PCM, all carrier masks, algorithm 3, voice stealing/reset,
packed PCM and bulk wave equivalence, ROM/RAM isolation, allocation guards,
sampled controllers, zero velocity and natural/safety endings. Sanitizer CI
also exercises malformed voice-wave packets. `tools/corpus-test.cpp` reports
fallback/PCM counts, stolen voices and safety endings without saving audio.

The beta.10 test build was rebuilt for all three Android ABIs and passed lint
plus the synthetic audio/security tests on 2026-09-26. A private 382-file
ten-second probe produced 382 finite, audible results, including all 61 MA-7
Format 3 files, with no unexpected failures; 97 earlier-format files exercised real PCM.
No held PCM voice was stolen. One non-target file saturated all held FM slots
(19 steals). Four longer focus files were rendered completely in normal and
phone-speaker modes with no held-voice steals or tail-limit hits. These local
checks contain no media and publish no private filenames.

Numerical checks do **not** establish perceptual similarity to J-SH51/905SH.
Still uncalibrated: FM modulation/feedback amplitudes, algorithm output gain,
envelope rate/key-scaling precision, waveforms, PCM vibrato/tremolo/damper,
ROM percussion and handset acoustics. At this earlier stage inline voice/wave
updates were collected at load time. The 1.6.2 section below fixes inline tone
activation; inline waveform replacement remains preloaded. MA-5 bulk-wave variants and
MA-7 device effects require separate validation. Future work must distinguish specification
corrections, tested transport decoding and listening/hardware calibration;
do not substitute global EQ or shorter tails for those tasks.

For MA-1/2 VMA voices, `SUS=ON` preserves the authored RR for the damped
pre-key-off stage and changes only the key-off release to rate 6. This follows
the YMU757B behaviour and prevents zero-RR accompaniment voices from remaining
held until the renderer's safety boundary. HandyPhone score parts also
retrigger their fixed voice slot instead of accumulating release tails from
earlier notes on the same hardware part.

## MA-7 investigation

Format 3 is not Mobile Standard format 2. Its SEQU stream keeps the Mobile event
classes but uses status bit 7 as a second 16-channel bank: `0x00/0x80` are
running-velocity notes, `0x10/0x90` are explicit-velocity notes, and the
control/program/pitch classes follow the same pairing. This grammar consumed
all 2,065,941 sequence bytes across 61 local files, reaching one EOS per file.

MA-7 `43 79 08 7F 21` tone messages carry expanded 2/4-op FM or WT register
images, optionally followed by an Analog Lite filter. The oscillator,
envelope and WT sections are folded into the existing VM35 representation;
the AL filter tail is intentionally omitted. Exact message lengths and type
flags are checked before conversion, and unknown shapes remain rejected. The
61 files all produced finite, audible output in the ten-second corpus probe.
This establishes compatible transport/event decoding, not cycle-accurate
YMU786 or handset-ROM emulation.

### MA-7 voice-wave RAM correction (1.6.1-beta.1)

- Native `43 79 08 7F 23` wave messages now decode the observed raw ADPCM
  codec 0 and unsigned PCM8 codec 2. MA-3 seven-bit transport remains unchanged.
  Unsupported codecs and oversized messages are ignored, not guessed.
- MA-7 sampled instruments only bind voice-wave RAM. A missing instrument
  waveform never aliases an unrelated Mwa/audio-stream waveform with the same ID.
- The observed WT+AL 43-byte image (flags 7) is accepted only when its nine-byte
  pitch-envelope extension is entirely zero. WaveID stays at body offset 33,
  before that extension. This narrow transport interpretation was checked
  against a private affected file; it is not general nonzero Pitch-EG support.
  The final WaveID/PEG transport alignment remains provisional; it must not
  be generalized to nonneutral pitch envelopes from this narrow case.
- MA-7 uses 32 PCM slots, as documented by Yamaha's MA-7 Authoring Guideline
  section 3.2.1. Earlier score formats retain their 16-slot allocation policy.
- Render cache revision r9 prevents an older cached conversion hiding this fix.

Synthetic tests cover native PCM8/PCM16 equivalence, raw ADPCM equivalence,
namespace collisions, neutral/unsupported/truncated expanded voices, allocation
bounds, render block sizes, and MA-7/legacy pool isolation. In the affected
private file, all 16 custom tone images and all 7 RAM waveforms loaded.
Across a complete normal/phone rendering, 6,109 of 6,368 notes used their custom
sampled instruments; fallback decreased from 5,112 notes to 259. There were no
held-voice steals, safety-tail hits, or limiter-ceiling samples. Those counts
are evidence of corrected binding, not a percentage of perceptual accuracy.
The final ten-second private corpus probe passed all 382 files, including
61 MA-7 files. This checks startup conversion and finite output, not full-song
or listening accuracy for every file.

Manufacturer documentation confirms sampled voice Fs is rooted at note 60,
distinct waveform IDs, and optional pitch envelopes. Raw codec/type mappings
are specifically validated by observed data plus synthetic decoding tests;
the authoring manuals do not document every wire-format byte. AL filters,
nonzero Pitch-EG, PCM vibrato/tremolo, non-MA7 bend-sensitivity conversion, exact MA-7
envelope precision, ROM percussion, and device effects still need separate work.
No direct listening match against the reference video is claimed.

### MA-3 wave and score Audio correction (1.6.1-beta.2)

- MA-3 seven-bit packed voice-wave codec 2 now decodes unsigned PCM8,
  alongside existing codec 0 ADPCM. Unknown codecs remain rejected.
- Official MA-3/5 custom banks 124/125 bind instruments to voice-wave RAM,
  never an unrelated recorded Mwa sample with the same ID. The legacy
  compatibility binding outside these banks is retained.
- Mwa uses its three-byte WaveType header: codec/bit depth/channel mode,
  then BE16 sampling frequency. The frequency byte is no longer decoded
  as audio. Mono ADPCM4, signed/unsigned PCM8 and signed PCM16 are supported;
  unsupported stereo/codecs, out-of-range rates and odd PCM16 are rejected.
- Recorded score Audio uses bank 125, LSB/program 0..9, notes 0..12 and
  92..110. The corresponding wave IDs are 1..13 and 14..32, local to each
  MTR. Audio runs at its own native Fs, with squared velocity and channel
  pan, without instrument transposition, envelopes or volume/expression.
- Audio has two dedicated slots, separate from the WT pool, following
  Yamaha's MA-7 Authoring Guideline sections 8.1/8.2 and 9.9. Gate events
  retain note-instance identity; a bounded 2ms stop fade is an anti-click
  approximation, not a simulation of handset NoteOff latency. Missing
  recorded Audio stays silent rather than becoming a wrong FM instrument.
- Cache revision r10 forces fresh conversion after installing this build.
  ATR/Awa legacy playback and normal player UI/transport are unchanged.

Synthetic regressions cover codec2 centering/transport, Mwa headers and
rejection guards, native-rate Audio mapping, velocity, controller isolation,
pan, two-slot replacement and stale gates, render blocks, per-MTR IDs and seek.
Both private focus files completed normal/phone renders without nonfinite
samples, held FM/WT steals, limiter-ceiling samples or safety-tail hits.
Recorded Audio fallback decreased from 259 and 1,083 triggers respectively
to zero, with no missing samples. One file still has 540 handset-ROM PCM
notes using FM approximations: this change does not supply proprietary ROM.
The MA-3 file's two previously omitted PCM8 voice waves now bind to voice RAM.
These are structural/numerical checks, not a direct reference-video listening
match. AL filters, modulation, accurate envelopes, ROM and effects remain
the limitations described above. No real songs or manufacturer files are
distributed with these synthetic tests.

The final ten-second private corpus probe passed all 382 files (61 MA-7),
with no held WT voice steals. Audio regressions and malformed-data smoke
tests passed; all three Android ABIs built successfully with debug lint.

### Custom FM drum sounding-key correction (1.6.1-beta.3)

DEV 1.6.2-beta.9 narrows this policy for Mobile Format1/2: channel-status
bits7..6 explicitly OFF (1) select DrumKey; ON (2), unspecified (0), and
reserved (3) retain score pitch. Unspecified uses the trigger-note policy
of the original but80/smaf825 player; its interpretation is implementation-
dependent, not a claimed manufacturer-mandated rule. Previously bank125 alone
selected fixed pitch even when the channel did not request it. The parsed raw
DrumKey is retained. MA7/HandyPhone policy remains unchanged; this does not
raise all raw keys by24 or remove fixed-key support. Seek preserves the policy.

Reference bit decoding and conditional substitution were inspected in
[scoretrack.go](https://github.com/but80/smaf825/blob/v1/smaf/chunk/scoretrack.go),
[keycontrolstatus.go](https://github.com/but80/smaf825/blob/v1/smaf/enums/keycontrolstatus.go),
and [sequencer.go](https://github.com/but80/smaf825/blob/v1/sequencer/sequencer.go).
Waveforms, gains, envelopes, ROM approximations and note-instance gating are
unchanged. Cache r25 prevents replaying previous converted audio.

MA-3/5 packed/direct and MA-7 expanded custom FM drum voices retain their
authored DrumKey separately from the mapped trigger key. Only custom FM
bank 125 uses this fixed oscillator pitch; zero is a valid DrumKey. Voice
lookup and paired note-instance gates still use the original mapping and
identity. Existing basic-octave transposition is retained. Melodic voices,
PCM, ROM approximations, recorded Audio, gain, envelopes and UI are unchanged.

The Yamaha authoring manuals distinguish the mapped drum key from the
sounding Drum Key (MA-7 User Manual sections B-1 item 4 and C items 8/9).
Synthetic tests cover MA-3/5/7 equivalence, DrumKey zero, melodic exclusion,
independent gates for overlapping mapped keys, and render-block independence.
All eight custom FM drum patches in the affected private score use neutral
basic octave; other basic-octave settings have not been compared against
Yamaha playback. Cache revision r11 forces fresh conversion.

The original beta.3 change addressed the mapped/sounding-key distinction,
but its unconditional Mobile activation was too broad; beta.9 corrects that
policy as noted above. Neither change proves a loudness match.
Perceived percussion balance remains subject to listening verification.
At beta.3 the other private MA-7 score's intro remained unresolved: CC15
suggested bend sensitivity but its Format-3 mapping was not yet validated.
See beta.5 below for the subsequently confirmed mapping. No direct listening
comparison is claimed.

### MA-7 controller curve and missing-ROM drum routing (1.6.1-beta.4)

MA-7 SMAF channel Volume and Expression now each use squared amplitude,
including default volume 100, following the manufacturer's MA-7 Authoring
Guideline sections 8.3.4/8.3.6 (40 log10(value/127) dB). The per-channel mode
is initialized for all 32 Format-3 channels, preserved across seek, and
cleared on a new init. Existing MA-1/2/3/5 controller behavior is retained.
Already-held WT/FM instruments receive the controller update. Recorded
Audio remains excluded, and note velocity is not squared a second time.
This restores authored delayed-layer/fade balance without deleting notes
or shortening gates. It does not implement per-voice AL filters, Pitch-EG,
the then-unverified CC15 mapping, or precise MA-7 envelope timing.

Custom drum bank 125 is now recognized as percussion even when its MTR
channel type is no-care. Previously a missing handset-ROM PCM drum lost
its drum classification when falling back, and could select a pitched GM
melodic patch. Existing custom voices and Audio dispatch still take priority.
The replacement is the existing percussion approximation: no ROM waveform,
gain calibration, transposition tuning, or new audio asset is supplied.

Synthetic tests cover MA-7 default/explicit controller equivalence, squared
Volume/Expression, updates during held WT notes, seek/reinit state, recorded
Audio isolation, and bank125 fallback with both rhythm flags unset. The
affected intro timbre remains unverified; no reference listening match is
claimed. Cache revision r12 forces a new conversion after this debug update.

### MA-7 pitch-bend sensitivity (1.6.1-beta.5)

Format-3 CC15 now selects the channel's pitch-bend range in semitones, 0..24,
with default 2. The identification was checked against Yamaha's public
MA-7 authoring converter: SMF RPN0/0 Data Entry values 0..24 are retained
per channel, then emitted as Format-3 CC15 before the next pitch bend.
This was static inspection only: no manufacturer executable was run,
installed, linked, copied into this repository, or distributed with the app.
The new implementation uses the existing semitone/rate calculation.

The signed bend and sensitivity are retained independently so sensitivity
changes recalculate active WT/FM pitch. Seeking and fresh init restore the
default. Invalid values above 24 are ignored. Earlier formats' CC15 behavior,
recorded Audio, voice mapping, gates, envelopes and controller gains are
unchanged. Cache revision r13 forces a new conversion in this DEV build.

The affected private score specifies 24 on five channels and 5 on one.
A raw bend of -2020 should therefore be -5.918 semitones on a range-24
channel, not -0.493 as before. This is a confirmed decoding omission,
not a claim that the intro now matches the reference. Per-voice AL filters
and more precise MA-7 envelope rates remain separate unresolved issues.

### MA-7 WT amplitude-rate precision (1.6.1-beta.6)

MA-7 WT rate values are 5-bit, with the four extra least-significant bits
in expanded WT image byte10: RR bit0, SR bit1, DR bit2 and AR bit3.
The legacy nibble is the upper four bits, so an MA-7 rate is
`2 * legacyNibble + extraBit`, not a high-bit extension of a legacy rate.
Manufacturer authoring-format getters/setters and legacy conversion were
checked statically: the latter doubles old values, retaining even-rate
semantics. No manufacturer program was executed or bundled in the app.

Only supported MA-7 WT shapes consume these bits. Existing MA-1/2/3/5
and MA-7 FM parsing, wave IDs, sample bounds, note gates, gains and recorded
Audio remain unchanged. Even WT rates use the previous calculations exactly.
Odd rates use an intermediate step of the existing related-YMF envelope
approximation; the slowest nonzero rate extends that progression. The bit
interpretation is verified, but the actual MA-7 envelope timing and shape
are NOT measured or bit-exact. Cache revision r14 requires fresh conversion.

Synthetic tests cover all16 bit combinations in WT-only, WT+AL and neutral
Pitch-EG images, each envelope phase independently, minimum/maximum rates,
unchanged adjacent fields, malformed lengths, block independence, seek and
fresh init. This addresses a decoding omission, not a full reference match.

The public MidRadio V7 Voice/Effect List describes its XG Lite built-in
normal voices (MSB0), drums and effects. It is useful as classification
reference, not a specification for custom MA-7 WT/AL synthesis or a source
of redistributable waveforms. Public authoring documentation shows AL filter
parameters, but their precise frequency/rate/coefficient mapping remains
unverified; this build does not insert a guessed filter. PCM modulation,
key scaling, nonneutral Pitch-EG, ROM and effects also remain incomplete.

### MA-7 embedded signed PCM8 (1.6.1-beta.7)

This private DEV build experimentally treats MA-7 voice-wave command23
codec3 as signed two's-complement PCM8, distinct from codec2 unsigned PCM8.
Manufacturer authoring-editor static inspection establishes internal
format2=unsigned8 and format3=signed8; its raw-wave format property is
passed unchanged to that renderer. The complete connection between the
project-wave prefix and the command23 serializer is NOT yet established.
The observed codec3 waveform bytes also have signed8-like signal statistics;
that is corroboration, not a wire-format specification. This mapping must
not be presented as manufacturer-certified or perceptually verified.

Decode each signed byte to PCM16 by scaling by
256, with no offset, compression, sample-rate, loop or endpoint changes.
The same 65536-sample allocation bound applies to both PCM8 formats.
ADPCM0 is unchanged. Other formats, including PCM16 codec1, remain
unsupported in this command until their complete byte layout is verified.
Voice RAM must remain separate from score Audio RAM; an invalid or empty
wave does not replace a previously decoded valid wave.

This is a waveform-reading correction, not a new replacement instrument
bank. ROM references, nonneutral MA-7 Pitch-EG, AL filters and effects
remain separate limitations. The latter must not be silently stripped
just to make a rejected voice play. No gain/EQ, note gates, controller
curves, voice selection or legacy-generation synthesis were changed.
Cache revision r15 requires a new conversion in this private DEV build.

Newly decoded codec3 waves can retire a loop only after entering a region
whose entire loop is exactly zero. This avoids ten seconds of dead runtime
for a one-shot wave with an authored silent endpoint loop, without changing
the envelope, shortening an audible tail, or altering older codecs' policy.

Synthetic tests compare all256 possible signed8 byte values with direct
PCM16 and offset unsigned8, then verify block independence, seek/fresh
init, maximum/oversized/empty payloads and invalid overwrite preservation.
No vendor program, ROM, code, or copyrighted sample was bundled.

## Modern timbre trial (1.6.1-beta.8)

This private DEV build broadens the earlier embedded-wave-only work to the
ROM-free replacement voices and modern FM synthesis. It is not a verified
listening match against reference videos, and neither manufacturer ROM nor
manufacturer synthesis code is incorporated.

### ROM identity and original replacement recipes

When a supported PCM voice selects missing ROM, use its wave ID to classify
the replacement rather than its program number or mapped drum key. Yamaha's
MA-7 Authoring Tool User Manual section 9.1, page94, lists wave0..21 percussion,
22..25 piano zones and 26..28 string zones. A user-defined program19 can thus
reference a piano wave, and program11 can reference a string wave. The old GM
fallback selected organ/vibraphone without considering those source waves.
This proves that arbitrary custom program numbers are not reliable identifiers
of the referenced ROM wave. However, an author can reshape a piano/string wave
into another timbre with envelope/filter settings; the source wave's name does
NOT establish the intended final instrument. Wave-family selection here is a
trial starting point, not a verified correction of the author's final timbre.

Observed MA-3/5 wave IDs and loop endpoints agree with those classifications;
using the MA-7 table for earlier-format ROM voices is provisional in this DEV
build. Unknown IDs retain the earlier fallback instead of inventing a mapping.
The 29 replacement entries are original FM recipes, not reconstructed ROM
samples. Distinct percussion recipes replace the three broad drum buckets;
layered piano/string/organ recipes are also used for unmatched modern GM voices.
Piano/string zones share a family recipe: original sample-zone pitch, spectral
balance and multisample transitions are still not reproduced.

Mapped drum keys address a voice and must not set the pitch of a missing ROM
crash/ride/etc. Trial drum substitutes use a fixed designer base pitch, scaled
by authored Fs/16000, retained through held bends. The 16000 reference and
base pitches are designer approximations, NOT measured ROM sampling rates.
PCM TL/envelope/loops are not blindly transferred onto independently voiced
FM substitutes; their original waveform levels remain unknown. Genuine custom
RAM waves, recorded Audio and authored custom FM selection still take priority.
Fallback counters remain unchanged: a differently voiced replacement is still
a replacement, not restored original audio.

### Related-chip modern FM profile

Mobile MA-3/5 and MA-7 channels opt into a trial profile using the Yamaha-authored
YMF825 tone parameter reference for exponential feedback: in cycles,
0,1/32,1/16,1/8,1/4,1/2,1,2 instead of a linear FB/24 scale. Tremolo endpoint
depths are 1.3/2.8/5.8/11.8 dB. The existing two-sample feedback average and
linear LFO amplitude interpolation remain approximations. These related-chip
values are NOT established MA hardware calibration. Feedback0/no-tremolo math
is unchanged. No global gain, output EQ, modulation-index constant, waveform
formula, envelope timing, note gates, limiter or player UI was changed.
HandyPhone channels retain the previous profile and GM replacements.

Reference: https://raw.githubusercontent.com/junk16/ymf825board/master/manual/fbd_spec3.md

### Mobile pitch-bend sensitivity

Mobile sequence RPN MSB/LSB (CC101/100) select standard RPN0/0, and Data Entry
CC6 selects semitones (supported0..24), CC38 selects cents (0..99). Null/other
RPNs and NRPN selection (CC98/99) do not update bend sensitivity. State is per
channel, reset on init and reconstructed on seek; sensitivity changes retune
active WT/FM voices without retriggering. Recorded Audio is unaffected.
MA-7 retains its previously verified compact CC15 mapping, including integer
sensitivity, and HandyPhone controllers remain unchanged. The affected MA-3
private score uses range1/4/8; previously all used the default range2.

Reference: https://midi.org/midi-1-0-control-change-messages

Synthetic regression fixtures cover feedback/tremolo endpoints, all ROM IDs,
classification/address isolation, unknown bounds, block/seek behavior, retained
replacement TL policy, RPN selection/null/NRPN isolation, cents, invalid values,
init and equivalence with existing MA-7 sensitivity handling. Cache r16 forces
fresh conversion. AL filters, complete pitch envelopes, accurate ROM timbres,
precise modern envelopes/waveforms/modulation and device effects still require
work and direct listening calibration. A passing conversion/fingerprint test
does not establish better sound.

## Private DEV beta.9: authored modulation and expanded FM rates

VM35 PCM and expanded MA-7 WT now retain the voice LFO selector, EAM/EVB
enable flags and DAM/DVB depth fields. Genuine instrument RAM playback applies
the previously omitted tremolo and vibrato per sample. Bend remains separate
from vibrato; phase resets on note allocation, init and seek. Recorded Audio
and ATR one-shots do not inherit this instrument modulation. The source field
mapping agrees with the go-smaf VM35 PCM description. MA-7 manual pages63/64
confirm WT LFO and amplitude modulation controls, including DAM endpoints
1.3/2.8/5.8/11.8dB. The four LFO speeds, sinusoidal shape and DVB pitch depths
reuse the existing FM approximation, not measured MA hardware behavior.
PCM's LFO uses a shared sine table with maximum interpolation error below3e-7
to avoid a transcendental calculation per active voice per output sample.

Source field reference:
https://raw.githubusercontent.com/but80/go-smaf/master/voice/vm35pcm.go

Expanded MA-7 FM operator rate-low bits are now retained just like WT:
RR/SR/DR/AR use bits0/1/2/3 of the additional operator byte. The envelope core
already accepts this resolution; previously FM silently rounded odd rates
down. Rate-to-time and key-scaling curves remain approximations.

MA-7 AL filter registers are retained as metadata rather than discarded:
resonance, five13-bit cutoff values, four5-bit rates, modulation and control
flags. The LFO Reset bit is not an enable flag. Fixed-frequency BLOCK/FNUM
registers are also retained, but neither AL nor fixed-frequency DSP is guessed
from these fields. Cutoff-to-Hz/Q/rate-time and fixed-frequency calibration
remain unknown. Nonneutral PEG remains unsupported; no envelope is stripped
merely to increase the native-note count. ROM substitute recipes and their
level policy are unchanged from beta.8.

Synthetic checks cover packed/direct/expanded transport, inactive enable
flags, independent modulation speeds, block/seek/init stability, held bends,
FM low-bit isolation, AL bounds and nonapplication of speculative DSP. Cache
r17 forces fresh conversion. These are functional corrections, not evidence
of a perceptual match to reference recordings. No UI/playback workflow change,
manufacturer code/ROM/sample redistribution or public release is included.

## Direct WAV playback (1.6.2-beta.7)

Folder discovery accepts .mmf and .wav (case-insensitive). WAV entries share
the existing queue, notification controls, audio focus, pause/seek, previous/
next and continuous/random playback. WAV uses Android MediaPlayer with the
selected document URI directly: no native MMF decoder, conversion, duplicate
WAV cache, phone-speaker effect or source-file modification. Actual WAV codec
support is the platform's; unsupported/corrupt files use the existing error
state. PCM WAV used by the comparison tools is the intended basic format.

Batch conversion and its count include only MMF. Phone mode is disabled in
the WAV UI and remains MMF-only in mixed queues; a service setting update
does not stop a playing WAV. The DEV converted-cache export is MMF-only.
MMF decoder and render cache r23 are unchanged, as are diagnostic comparison
files. C backing balance and D/E/F modulation probes are not app-wide DSP
changes. Host classification tests and Android build/lint do not substitute
for real-device checks of WAV pause, seek, completion and mixed queues.

## MA-5 native RAM wave correction (1.6.2-beta.6)

Native instrument wave command `43 79 07 7F 03` is now read without
MA-3 seven-bit unpacking. Only codec 0 (Yamaha ADPCM) and codec 2 (unsigned
PCM8) are enabled; codec 3 remains MA-7-only. This interpretation is supported
by native high-bit payloads and decoded sample counts matching the selecting
MA-5 PCM patch endpoints in the local audit, not independently established
manufacturer wire-codec documentation. The six previously ignored RAM waves
in the audited MA-3/5 song therefore replace erroneous FM fallback tones.

Setup and sequenced wave events share the existing bounded decoder. Limits,
same-tick ordering, note snapshots, seek and initialization are retained.
Malformed/unknown waves do not erase a valid bank entry. Synthetic tests
compare native bytes against packed MA-3 decoding, including internal F7,
80 and FF bytes, and cover unsupported codec 3 on MA-5. No ROM samples,
third-party assets, gain/EQ, FM synthesis or playback workflow changes are
included. Cache r23 invalidates earlier converted audio; audible balance and
faithfulness still require device listening. Public 1.6.1 remains unchanged.

## beta.3 implementation and limits

- SR=0 holds; VM35 and VMA-converted patches enter the sustain stage, rather
  than erroneously entering release when SR=0. XOF ignores key-off.
- AR/DR/SR/RR=0 stops that phase. Song termination is enforced by the player,
  not by inserting a fictitious envelope decay.
- Decay/release are exponential in amplitude. Related YMF825 rate timing is
  used as a documented approximation; attack shape and pitch-to-rate offset
  mapping are not cycle accurate. Sustain uses the Yamaha-family 3 dB scale.
- DT uses sign/magnitude with neutral values 0/4. Its depth remains approximate.
- KSL order and LFO rates follow the related manual. MULTI retains SMAF's linear
  interpretation; the YMF825 multiplier table differs, so it is not copied.
- No new ROM samples or third-party audio assets are included. The existing
  fallback drum bank, FM modulation depth, waveforms, PCM path and MA-7 AL
  filter omission remain approximations.
- Phone-speaker mode remains an optional generic effect, with no handset preset
  or claim of SH51/905SH acoustic calibration.

## Private 1.6.2-beta.8 PCM interpolation quality

The public 1.6.1 release remains unchanged. This DEV build uses code44/cache r24
and retains beta.7 WAV folder/direct-playback support.

Genuine instrument RAM WT in the modern Mobile/MA7 profile now uses an original
16-tap Blackman-windowed sinc interpolator while upsampling. Its normalized
257-row phase table is interpolated between rows, with exact integer-phase
endpoints. This reduces linear interpolation's amplitude error and image
components in synthetic upsampling tests; it is a renderer-quality decision,
not a verified proprietary handset resampler. No external code/assets added.

The interpolation blends smoothly back to the prior linear method as the
instantaneous sample advance moves from0.8 to1.0, and preserves that method at
and above unity. It does NOT solve high-transposition downsampling aliasing.
Bend/vibrato sample advance, envelopes, authored gain, note scheduling and
the final limiter/filter are unchanged. Sinc overshoot is not independently
clipped. Older HandyPhone, recorded Mwa/ATR Audio, ROM/FM substitutes and WAV
direct playback retain their previous paths.

Loop taps retain real attack history until they actually enter the periodic
loop, including the first several traversals of loops shorter than a kernel.
Wrapped-history storage saturates on a whole-loop boundary. Signed8 exact-zero
loop retirement keeps linear interpolation, preserving its scheduling contract.
Voice reuse, seek and initialization reset history/quality state.
The existing voice-steal level estimate is computed from the previous linear
sample independently of the quality-rendered output, so interpolation does not
change the established pool-priority policy. Interior kernel evaluation avoids
per-tap loop/history modulo to reduce conversion overhead.

The synthetic0.2-cycle/source sine test has RMS error0.0460767 (linear) versus
0.0000720724 (sinc), in normalized amplitude at fractional phase0.37. This is
one test signal, NOT a per-song perceptual improvement or handset measurement.
Private short-loop reference tests cover loop lengths1/2/3/7/8/16, nonzero
attack, history saturation and multi-loop jumps. A host-only32-voice/sec44100Hz
interpolation microbenchmark after optimization was about24ms linear/61ms quality;
the full private Netsujou host render was about3.3sec before/6.5sec after. Android CPU,
power and long-song conversion latency require real-device verification.

Netsujou's ghost-like vibrato remains a separate unresolved issue: deep MA7
CC1 modulation plus a delayed matching voice can swing nearly in opposition
under the current per-note phase model. Native controller/LFO semantics still
need evidence. No global vibrato cap, guessed shared-LFO behavior, global
accompaniment reduction or per-song source-data patch is hidden in this build.

## Private 1.6.2-beta.10 PCM downsampling quality

DEV code46/cache r26 adds rate-dependent anti-alias filtering for genuine
instrument RAM WT in the modern Mobile/MA7 profile when instantaneous sample
advance exceeds1. This is a renderer-quality improvement, not measured handset
emulation or a correction for the separate SO2/Courage FM timbre complaints.
No score, patch parameters, PCM sample assets, authored gain, pan, envelopes,
velocity, pitch/bend/vibrato clock, FM or ROM substitutes are changed.

An original32-tap Blackman-windowed sinc bank uses129 phase rows and33 cutoff
rows, bilinearly interpolated and DC-normalized. Cutoff is1/advance relative to
source Nyquist; the zero-cutoff row is the normalized window limit. Integer
phases are filtered too, including exact2:1/4:1 reductions. Shared in-place
storage is544896bytes with no per-voice allocation or sample-rate trigonometry.
The existing upsampling and exact-unity arithmetic are retained. AA engages
smoothly between advance1 and1.1; that transition deliberately provides weaker
alias rejection. Finite32-tap support does not guarantee alias removal near
the transition band or at extreme rate ratios. A filtered transient can ring
or overshoot; no independent gain normalization or limiter is added.

The longer kernel retains left15/right16 source history at loop boundaries;
history saturation remains a whole-loop multiple. Allocation recentLevel still
uses the prior exact linear sample, not the filtered signal. Recorded Mwa/ATR
Audio, WAV playback, older HandyPhone profiles and silent-loop retirement retain
their old processing. The direction follows sampling-rate conversion theory:
https://www.dsprelated.com/freebooks/pasp/Windowed_Sinc_Interpolation.html

Independent sine checks preserve a1/32-cycle/source passband tone at2/4x and
reject0.375-cycle/source at2x /0.25 at4x; these measurements are synthetic, not
per-song listening scores. Checks also cover1.5x rejection, phase endpoints,
cutoff-grid/unity continuity, tiny loops, DC, legacy arithmetic, allocator level,
source-history saturation, live controls, chunk sizes and seek replay.
Android conversion CPU and perceptual quality still require device testing.

### Mobile compact bend-range compatibility trial (1.6.2-beta.11)

Some later native Mobile Format1/2 files carry CC15 with semitone-like range
values instead of RPN0/0. The prior decoder ignored this controller. The trial
now accepts0..24 through the existing bend-range path, updates currently
sounding FM/WT voices from their stored base pitch, and resets fractional cents
without changing RPN/NRPN selection. Invalid values are ignored. MA7 CC15 and
HandyPhone decoding remain unchanged; no timbre, gain, vibrato, note/gate or
pool-capacity tuning is bundled with this change.

This is an empirical compatibility interpretation, NOT a confirmed MA5
manufacturer definition. Yamaha SMAF Specification3.05 Mobile p44 documents
RPN0/0 and a default +/-2semitones, not a CC15 assignment:
https://img.atwiki.jp/mmfuta/attach/19/78/SMAF3.05.pdf
MA7's documented compact mapping does not by itself prove the Mobile mapping.

A private native Mobile arrangement contains CC15=12 on two relevant FM
channels at17.6s and29.9s, with no RPN alternative. Numerous bends start114.8s,
matching the reported1:55 problem. Raw -2728/-3410/-4774 become approximately
-4/-5/-7semitones at range12, versus -.67/-.83/-1.17 at the previously retained
range2. Real-device listening remains necessary; no reference accuracy claim.
Original song data is untouched and excluded from app/public resources.

Regression checks compare compact and RPN output for FM/WT, initial/live range,
per-channel scope, values0..24, invalid25/127/255, render chunk sizes, seek,
cents reset and retained RPN/NRPN selection. Audio regression and security
smoke pass. A382-file first10sec memory-render corpus passes:365 float hashes
unchanged,17 changed; note/fallback counts, held steals, scheduled/probed
durations, finished and tail flags unchanged. This is not full-corpus listening.
Cache r27 requests reconversion. Deep MA7 vibrato, accompaniment balance and
FM noise remain separate unresolved issues, not claimed fixed by this patch.

### Whole-score coverage inventory (1.6.2-beta.12)

This diagnostics-only change instruments the actual setup/sequence decoder,
not a second independent parser. Mobile and MA7 controller scheduling and
coverage classification share one dispatch function. `MaPlayer::scoreAudit()`
is built during init and retained unchanged through render/seek. The score is
decoded to its end, not limited to the audio regression's ten-second probe.
The existing synthesis, gain, LFO, gates, routing order and cache r27 are retained.

Coverage statuses deliberately do not promise audible fidelity:

- `routed`: scheduled/recognized, not proof of changing an active note.
- `conditional`: Mobile DataEntry depends on selected RPN0/0 and its bounds.
- `unapplied`: known parsed-but-ignored controls, or retained voice fields with
  no DSP implementation. Mobile/HPS modulation is distinct from MA7 modulation.
- `partial`: incomplete parameter support, including NRPN cancellation only,
  nonzero/non-null RPN selections, and retained but uncalibrated MA7 detune.
- `compatibility`: empirical Mobile CC15 and proprietary ROM substitutes.
- `unsupported`: dropped controls/unrecognized SysEx or rejected wave/voice
  variants. Rejected extensions are not automatically labelled pitch EG.
- `reserved`: intentional fixed-width skips and reserved FM wave shapes.
- `malformed` / `resource_limit`: incomplete decoding, with scanComplete=false.

VoiceDefinition records describe **definition presence**, not actual note use.
AL presence and enabled fixed-frequency fields are inventoried separately;
all-zero AL registers do not prove a hardware bypass. Dormant fixed FNUM/block
values alone are not warnings. Rate low bits are used
and are not misreported as ignored. Approximate envelope/oscillator laws still
require calibration; absence of warnings is not manufacturer-equivalent sound.
Coverage completeness means the current grammar was traversed; it does not
certify every timbre field, reference recording or hardware behavior.

Groups are bounded at256 and aggregate by domain/status/format/track/channel/
opcode. Values and timestamps are ranges, never unbounded keys. Status totals
continue after capacity and overflow is explicit. Counters saturate. No audit
allocation/logging occurs in the per-sample render loop. Runtime voice stealing,
missing audio and fallback remain separate diagnostics, not immutable score data.

`tools/score-audit.cpp` initializes files without rendering and writes local TSV
reports (relative filenames, not public resources). `corpus-test` also includes
coverage summary columns; its float hash remains a change detector, not a
quality score. Synthetic tests cover late controls, format-specific handling,
reserved widths, truncated lengths/VLQs, partial Huffman inflation, init reset,
seek immutability, group capacity and saturation, dormant versus enabled fields.
Existing musical regressions cover RPN/compact pitch equivalence and live notes.

Android DEV builds emit bounded numeric `MMFScoreAudit` init logs (summary and
at most32 non-routed groups). Release builds do not emit these logs. No song
titles, paths, lyrics or raw SysEx are logged. Cached MMF preparation still calls
detectMotionPace/init, so its score can be audited without invalidating WAV cache.
No audio balancing/vibrato repair or fidelity guarantee is bundled with this work.

### Modern FM output DC conditioning (1.6.2-beta.13, withdrawn in beta.14)

Some rectified FM carriers leave a substantial DC component at the output.
This is retained inside the FM operator graph because unipolar modulators can
legitimately change timbre. Only the final stereo FM sum in modern-only
Mobile/MA7 scores now passes through a normalized3Hz one-pole DC blocker,
before WT/recorded Audio addition, the existing low-pass and peak limiter.
Pure HandyPhone and mixed HandyPhone/modern scores bypass this conditioning.
No note, patch, per-part gain, pan law, gate, vibrato, oscillator, allocator,
PCM interpolation or ordinary WAV-player change is bundled with this patch.
It is renderer-quality conditioning, not a calibrated MA hardware model.

The filter has two scalar histories per side, no render-time allocation or
trigonometry. Init/seek reset both histories. Natural ending waits for DC and
low-pass histories; the existing safety cap and final fade still apply.
At hard-pan changes the old side can briefly retain an exponentially decaying
subsonic history, not a continuing note. Quiet drain length may slightly vary
with pan gain. Sampled-only output remains exact; mixed FM/PCM output can
receive a different gain from the unchanged shared limiter. Transient peaks
can increase even when DC decreases, so reduced limiter loading is not promised
for every score. No automatic loudness normalization is added.

Whole-song private before/after probes keep notes, fallback counts, held steals
and all other voice steals identical. Disabling only the new filter reproduces
the delivered beta.12 exact float hashes on all five targets. For example:

| Private target | Mean L/R before -> after | Limiter-active frames before -> after |
| --- | --- | --- |
| SO2 | .124/.138 -> -.00068/-.00049 |49.35% ->27.32% |
| SO3 | .102/.069 -> -.000012/-.000012 |1.30% ->.49% |
| Courage | .223/.245 -> approximately0/0 |.42% ->0% |
| Saruin | -.0354/-.0350 -> .00439/.00432 |2.10% ->2.35% |
| Enthusiasm | .00087/.00048 -> .00017/-.00023 |0% ->0% |

These numerical measurements do not establish improved reference fidelity or
prove that the reported noisy timbres, accompaniment balance or excessive
vibrato are fixed. Residual DC in mixed sampled tracks is outside this patch.
SO2/SO3 tails extend by approximately78/109ms; other target lengths are unchanged.
Private scores and audio are excluded from public resources.

The382-file first10sec corpus passes. All note/steal/fallback, score coverage,
scheduled duration and safety diagnostics match beta.12;5 HandyPhone and17
FM-free files retain exact output hashes.306 Mobile and54 MA7 hashes change
as expected from filtering. Synthetic tests cover scalar recurrence, constant
bias,20/50/1000Hz attenuation (<.1dB loss at20Hz),8/44.1/48kHz, mixed-HPS bypass,
natural tail, init/seek, chunk independence, all128 MA7 pan gains and live pan.
Transport comparisons retain1e-6 sample tolerance including any added tail,
with an explicit64ms bound on quiet gain-dependent drain at8kHz. Live-pan tests
separately verify early residual and full settling, rather than pretending
the added3Hz history is the old fast low-pass history.

Cache r28 requests MMF reconversion. Real-device listening is still needed;
documented vibrato values have not been weakened without a calibrated reference.

### Default DC-conditioning rollback (1.6.2-beta.14)

Device feedback on SO2 reported louder accompaniment masking the main melody
in beta.13. Numerical DC reduction is not enough to establish a better audible
mix. A same-input, sample-synchronous off/on probe showed changed master-gain
dynamics: average audible AC level in individual1sec windows increased by up
to approximately1.13dB, with instantaneous limiter-gain differences reaching
2.94dB. Part volume settings and notes were identical. These measurements show
a dynamic change; they do NOT conclusively identify the perceptual masking
mechanism or a specific accompaniment channel.

The unconditional output-conditioning experiment is therefore withdrawn for
all formats, not bypassed by song name/hash. FM/PCM summation, output low-pass,
limiter input, natural ending and pan behavior are restored to beta.12. No
guessed per-song accompaniment attenuation, automatic balancing, gain recovery
or extra filter is substituted. The limitedFrames diagnostic remains; it does
not change samples. The standalone filter utility/test is retained for host
experiments only and is no longer included by the production engine.

Pan tests return to their original strict silence/drain checks. Cache r29
requests reconversion of beta.13 cached audio. This is a regression rollback,
not a claim that the pre-existing SO2 mix/timbre or vibrato problems are solved.
Future output-conditioning proposals need controlled listening comparisons
with comparable perceived level, in addition to numerical and musical tests.

### Specification-first diagnostic correction (2026-10-05, unreleased)

User-directed policy is generic faithful synthesis, not per-song gain, vibrato
or tone modes. No new DSP constant is justified by the current listening probes.
Manufacturer MA5 manual4.18.2 p63/64 defines WS15/23/31 as user SRAM waveforms,
not reserved values. Definition inventory now reports their unimplemented
binding as `unsupported`, preserving raw selector/operator and seek stability.
This does NOT implement user wave transport or change audio output.

All three selectors on both operators have synthetic diagnostic regressions.
Audio regression and security smoke pass. The original54-tone calibration
captures covering TL/PM/feedback/envelope/fixed waves remain SHA256-identical
before/after this metadata-only correction. Separate measurement arithmetic
tests cover anti-phase stereo, DC separation, harmonic power, fixed-gain TL
ratios and PCM16/24/32/float32 input. No factory/hardware reference recording
has yet validated the app's oscillator, PM, envelope or output normalization.
No APK/version/cache/UI change or release is part of this diagnostic work.

### Authored event causality and complete FM bodies (1.6.2-beta.16)

SMAF3.05 p42 defines each Duration relative to the preceding event's start;
p44 makes Program Change a channel voice-selection event. Remove the 50ms
first-bank/program hoist: a future assignment must not determine an earlier
note's initial voice. Stable-sort by sample only, without promoting controllers
before earlier same-time notes. Within a track this retains authored insertion
order. Cross-track ties and generated gate NoteOff events retain their existing
decode-insertion tie-break; this is not a claim of hardware arbitration accuracy.
Program changes on already sounding voices still use the existing note-time
snapshot policy; hardware register-update behavior is not newly implemented.

Require complete active operators before accepting native FM definitions:
direct VM35 3+7*operators, packed MA3 4+8*operators, VMA 2+5*operators. Incomplete
images no longer silently pad/default operators or mask an existing valid tone.
Legacy trailing bytes remain accepted. Expanded MA7 and PCM layouts are not
changed by these checks. No PM, gain, vibrato, wave, envelope or EQ retuning.

Original synthetic tests reproduce pre-fix failures and cover future program
changes at 10/30/50ms (plus a 60ms control), MSB/LSB bank causality, same-tick
Note/Program/Note, generated gate/Hold ordering, block sizes and seek. Every
truncation of 2/4-op direct, short, packed and VMA definitions is rejected;
complete/extended bodies and malformed setup/inline replacement are tested.
Audio regression and security smoke pass. Same-build first10sec corpus probes
pass382/382: HandyPhone5 unchanged, Mobile316 changed6/unchanged310, MA7 61
changed1/unchanged60. Notes/fallbacks/total steals/tail and inventory counters
are unchanged. One file's held-FM steal count decreases53->52 (total58 unchanged).
Hashes detect changes, not listening quality. Reported SO2/SO3/Minstrel tone,
balance and vibrato issues are not certified resolved by this causality fix.

DEV code52/cache r31 forces reconversion. UI/playback classes are unchanged.
Local DEV only: no release signing, GitHub publish or OneDrive export.

### Preserve valid voices across invalid PCM definitions (1.6.2-beta.17)

User accepts the current musical balance provisionally; do not normalize
individual parts or tune by song. The PCM parser can retain isPcm=true when
valid=false (short bodies, rejected transport, or unsupported Fs). Previously
setup/inline registration treated the family flag as permission to register.
An invalid definition could therefore mask a working FM/PCM voice or replace
it with a generic fallback. Registration and audit now require valid=true;
invalid definitions are rejected and remain visible in the setup inventory.
The parser's existing shape/Fs rules are unchanged. Valid PCM with unavailable
sample data, including proprietary ROM, keeps the existing fallback policy.
Clarify the ParsedVoice API contract so family is not mistaken for validity.

Original synthetic tests cover40 invalid direct/packed/expanded/AL/neutral-PEG
PCM definitions, setup order, inline replacement of FM/PCM, both Mobile/MA7
event formats, seeks, render blocks and ordinary missing-voice fallback.
Boundary Fs1500/8000/48000 and valid ROM metadata remain accepted. The pre-fix
test fails on a masked valid voice; all candidate audio and security tests pass.
No synthesis, gain, vibrato, interpolation, envelope, filter or limiter change.

Fresh same-build baseline/candidate corpus probes pass382/382 at44100Hz for
the first10sec; all382 audio hashes are identical (HPS5/Mobile316/MA7 61).
All notes/fallbacks/steals/tail counters are identical. One file's unsupported
inventory count changes, correctly exposing a rejected invalid PCM definition.
Six original targets (DQ4, SO2, SO3, Courage, Enthusiasm, Saruin) also match
exactly over their complete naturally-ended renders, without safety tail cuts.
This is a robustness/fidelity fix, not a newly demonstrated audible improvement
for these existing songs or proof of manufacturer-exact synthesis.

DEV code53/cache r32 prevents malformed-input conversions reusing old caches.
Offline lint/assembleDebug pass; existing UI/playback classes and DEV icon are
unchanged. Local DEV delivery to OneDrive; no public commit/push/release.

### Mobile All Notes Off, without tone retuning (1.6.2-beta.18)

Interpret Mobile Standard formats1/2 Bch 7B 00 as MIDI All Notes Off instead of
dropping it. SMAF3.05 p44 defines the channel/Control Number/Control Value
transport; the MIDI Association's controller table assigns123/value0 to All
Notes Off (https://midi.org/midi-1-0-control-change-messages). This is a standard
controller interpretation, not a claim that every handset implements all CCs.
Nonzero values remain unapplied. Compact MA7 retains its unsupported diagnostic;
no new CC120/121, mode change, global reset or guessed fixed-F implementation.

All Notes Off uses ordinary keyed-note release, not immediate channel muting.
FM/WT keep their authored SUS/Hold/XOF behavior, and recorded Audio keeps its
ordinary2ms stop fade. ATR one-shots have no key identity and remain unaffected.
Consume the released key's unique identity once; later gates or repeated
commands must not rearm Hold or restart an Audio fade. New keys receive new
identities even when they share a pitch or steal an old slot. Channel isolation,
decoded same-time ordering and existing gate tie-break remain unchanged.

Original synthetic tests cover overlapping keys, different channels, Hold/SUS/
XOF, repeat commands and later gates, new keys, same-sample command ordering,
seek, render chunks, invalid values, compressed/uncompressed Mobile transport,
MA7 nonapplication, recorded Audio and a deliberately same-channel ATR trigger.
The pre-change engine fails the channel-wide gate-release comparison. No new
gain, timbre, vibrato, envelope rate, filter, limiter or interpolation constants.
DEV code54/cache r33 requests reconversion; player UI and DEV icon unchanged.

Dedicated channel-mode, complete audio-regression and security-smoke tests pass.
Same-build44100Hz first10sec probes pass382/382 before and after. HandyPhone5
and MA7 61 remain exact; Mobile315/316 remain exact. The changed Mobile file
uses CC123 during the probe. Four files' unsupported-controller inventories
decrease; note/fallback/steal/tail counters in these probes remain unchanged.
Six previously reported original songs also match exactly over complete renders
and end naturally without the safety cutoff. A full render of the changed file
ends at the same existing safety tail limit in both versions; this remaining
tail condition is not fixed or hidden by the CC123 change. Fingerprints detect
changes, not perceptual quality or manufacturer-exact emulation.

Offline lintDebug/assembleDebug pass (zero errors, four existing warnings).
Debug-signed DEV APK delivery only; no public commit/push/release.

### Mobile All Sound Off, distinct from key release (1.6.2-beta.19)

Mobile formats1/2 CC120/value0 now stops the addressed channel's FM, keyed WT
and recorded Audio sources, including released voices with a consumed NoteId,
pedal-deferred gates and XOF voices. This differs from CC123's ordinary key
release. MIDI Association overview defines All Sound Off as oscillator stop
and zero envelopes as soon as possible:
https://midi.org/summary-of-midi-1-0-messages . Reject nonzero values; compact
MA7 remains unconfirmed/unsupported. Other channel-mode messages are unchanged.

Reset stopped FM slots and restore their output sample rate for new allocations.
Clear stopped key identities so late old gates cannot affect a replacement.
Keep channel voice/bank, volume, expression, pan, Hold, RPN/bend state and shared
output filter/limiter history. Independent ATR one-shots remain outside score
channel CC processing. This ATR policy follows the current separate-source
architecture, not newly verified manufacturer hardware behavior. Source stop
does not erase the output filter's short history or mute other channels.

Synthetic tests cover live/released/Hold/XOF sources, channel isolation, compressed
Mobile transport, repeats, seek/chunks, invalid values, MA7 nonapplication,
nondefault controls and new FM/WT notes, same-sample event ordering, recorded
Audio during playback and its existing stop fade, and ATR exclusion. The old
engine fails the first channel silence comparison. Dedicated, complete audio
regression and security smoke tests pass. No gain, timbre, vibrato, envelope,
oscillator, interpolation, filter or limiter constant is retuned.

Fresh same-build first10sec probes pass382/382 before and after. HPS5/MA7 61 and
Mobile315 remain exact; the one changed compressed Mobile file contains CC120.
Six files' controller inventories change; all probe note/fallback/steal/tail
counters match. Six previously reported original songs also match exactly over
complete naturally-ended renders, without safety cutoff. These are regression
and change-detection results, not a perceptual or manufacturer-exact rating.

A private read-only state probe of beta18's existing non-draining FF5 tail
finds six FM voices on channel81 with consumed gate identities and XOF enabled
on every active operator. Operators are still in Decay/Sustain at the safety
boundary. Do not override XOF or retune rates by song merely to shorten this
tail. CC120 only acts where the score actually sends it; it is not a global
tail cleanup rule. No proprietary data or debug-state probe is shipped.

All six private Mobile files whose inventory contains CC120 were additionally
rendered in full. Five outputs change; one remains exact. All six candidate
renders end naturally. Two pre-existing safety cutoffs disappear when the
authored stop is honored:107.692->97.692sec and83.256->73.258sec. No new cutoff.
The other previously reported six-song set stays exact; no full-corpus listening
or manufacturer-exact sound certification has been performed.

DEV code55/cache r34, unchanged player UI and DEV icon. Offline Android lint/
assembleDebug pass with zero errors and four existing warnings. Local OneDrive
DEV delivery only; no public commit/push/release.

### Mobile reset of implemented controller state (1.6.2-beta.20)

Mobile formats1/2 CC121/value0 applies Reset All Controllers to the currently
implemented per-channel states. RP-015 (MIDI Association) specifies expression
full scale, pedal off, pitch position centered and parameter selection null:
https://midi.org/response-to-reset-all-controllers . Its official one-page
download was read and visually checked during this work. This is standard
Mobile MIDI controller handling, not verification against manufacturer hardware.

Reuse existing live Expression/Hold/Pitch Bend processing without generating
new NoteOff events. Pedal-deferred keys release normally, still-pressed keys
continue, and other channels are unaffected. Null RPN selection prevents later
Data Entry from changing sensitivity until explicitly reselected. Retain the
registered sensitivity values (semitones/cents), including the existing CC15
compatibility setting, bank/PC, volume/pan, score running velocity and intrinsic
voice EVB. Do not reset filters/limiter, allocation or structural channel state.

Mobile modulation wheel, pressures and the other pedals are not implemented;
this change does not claim full RP-015 synthesis support or clear authored
vibrato as a substitute for those controls. Routed retains its existing meaning
of a scheduled implemented event, not hardware/perceptual certification. Audio
and ATR preserve existing controller exclusions. Reject nonzero values and keep
compact MA7 unsupported. No synthesis, gain, envelope or modulation constants
are retuned. DEV code56/cache r35 forces fresh conversions; UI/icon unchanged.

Original synthetic reset tests compare against explicitly authored ordinary
controller sequences, including active/held/SUS/XOF notes, independent channels,
RPN/null selection and preserved parameter values, intrinsic EVB, custom banks/
PC, running velocity, same-sample ordering, repeat/seek/chunk invariance,
compressed transport, invalid values, MA7 nonapplication and Audio/ATR sources.
The beta19 engine fails this new reset oracle before the production change.

Dedicated, complete audio regression and security smoke tests pass. Fresh
same-build44100Hz first10sec probes pass382/382 and all outputs remain exact:
HPS5, Mobile316 and MA7 61. Eight controller inventories lose unsupported CC121;
all probe note/fallback/steal/tail counters remain equal. All non-player native
sources match the immutable beta19 snapshot. Six previously reported original
songs also match exactly over complete renders and end naturally without cutoff.

Eight private Mobile files containing CC121 were rendered in full. Seven stay
exact; TW MOTIVITY changes at the late-reset/end region and now ends naturally
at231.004sec, versus beta19's229.492sec safety cutoff. Scheduling the formerly
ignored221.608sec reset also extends the safety horizon231.608 vs229.492sec;
the removed cutoff is an observed render result, not proof of a stuck-pedal
cause or manufacturer-exact sound. All eight candidate renders end naturally.
Offline lint/assembleDebug pass (zero errors/four existing warnings). Local
OneDrive DEV delivery only; no public commit/push/release.

### ATR header and track-local recorded samples (1.6.2-beta.21)

SMAF 3.05 pp47-49,51,55 were read and visually checked from the manufacturer's
reference. ATR has a six-byte header: format, sequence, two-byte WaveType,
duration timebase and gate timebase. Awa bodies contain raw audio, with no
per-wave header. The prior parser guessed format/rate from the first two audio
bytes, discarded those bytes and omitted the track's clocks. Decode the actual
header and preserve the entire raw payload. Supported forms are mono ADPCM4
and signed PCM8, at declared rates4000/8000/11000/22050/44100Hz (the table says
11kHz, not the former guessed11025Hz). Stereo, reserved fields, other codecs,
PCM12/16 and non-stream/unknown ATR sequence formats are not guessed.

Use the ATR's own clock, including explicit1ms, rather than inheriting score
timing. Its wave message has a six-bit ID1..62, unlike the score note nibble.
Bind each WaveOn to its source ATR number and keep Awa banks separate from
MTR/Mwa and instrument RAM, preventing equal IDs from substituting other
samples. Pure ATR input is decoded once as audio, not also as pitched score
notes. Reinitialization clears the banks; seek reuses stable decoded samples.
FM/WT gains, modulation, envelopes, interpolation and channel controls are not
retuned. This is partial ATR playback: existing one-shot, gate/EOS/controller
policies remain unchanged, not manufacturer-exact Audio Track emulation.

Original binary-container fixtures cover every supported rate/codec, raw
leading bytes and one-byte ADPCM, distinct clocks, pure/mixed tracks, ID
boundaries/reserved IDs, both directions of sounding Mwa/Awa collisions,
track-local banks, unsupported formats, seek/reinit and render-block invariance.
The immutable beta20 engine fails the new clock/header oracle; candidate
dedicated, complete audio regression and security smoke suites pass. No private
music or vendor ROM data is included in fixtures. DEV code57/cache r36,
unchanged UI, mascot, orange DEV icon and FM core. Local delivery only.

Fresh same-compiler44100Hz first10sec probes pass382/382. The one ATR-bearing
HandyPhone file changes; all other381 files (HPS4, Mobile316, MA7 61) retain
exact stereo-float fingerprints, duration and note/fallback/steal/tail counters.
That file's now-correct ATR metadata is the only track-description difference.
Full renders of the previously reported six songs remain exact and all end
naturally. The affected ATR original also ends naturally at42.993sec before
and after, with FM1124/PCM149 unchanged and peak0.693->0.609; no gain retuning
or perceptual fidelity score is inferred from that decrease. Offline Android
lint/assembleDebug pass (zero errors/four existing warnings), three ABIs,
verified existing debug signer. No manufacturer-hardware listening comparison.

### Empty container/track chunk boundary recovery (1.6.2 release)

The MTR, ATR and outer MMMD walkers previously added an extra byte when a child
payload length was zero. They had already advanced by the eight-byte header;
the extra skip lost the first byte of the following chunk ID and could hide
sequence or wave data. Remove only that redundant byte advance. Preserve
unknown/nonempty chunk handling, length clamps, scan ceilings, raw wave IDs,
empty-wave metadata and existing last-definition semantics. No DSP changes.

Original binary fixtures cover empty setup, wave and unknown chunks, repeated
empties, a later valid sequence/sample, mixed MTR/ATR order, end-of-container,
partial headers and a run beyond the existing scan ceiling. Source bytes,
audible output, seek and block invariance are checked. The beta21 engine fails
the new oracle; dedicated/audio/security tests pass after repair. Fresh382-file
first10sec fingerprints remain exact to beta21 (HPS5/Mobile316/MA7 61). The six
previously reported originals also render fully/exact and end naturally.
### Consolidated DEV beta.10: output fidelity and progressive coordination

DEV code68/cache r47 integrates the previously source-only muted-end-tail,
nearest-PCM16, native/progressive representable-quiet-output and native WAV I/O
failure batches. No per-song retuning, gain normalization or dither is added.
The PCM16 quantization improvement is bounded at the output conversion stage;
it is not a claim of audible improvement or manufacturer-exact instruments.

Progressive playback now exits paused/drain/zero-write waits after a producer
failure. Seek requests are claimed under the same lock that publishes them,
before slow track recreation, preserving newer and identical-destination
requests. Position snapshots and replacement publication use that lock; pending
seeks also veto drain completion before completion is committed. Tracks are
released once when replacement construction fails. Restarting after an already
committed completion is outside this patch's coverage. No samples, decoder,
effects, timelines or playback UI are changed by the coordination patch.

Actual Java control flow runs against host-only deterministic I/O doubles, not
an Android device: old source fails the paused, drain, zero-write, replacement
seek and drain-head-query assertions; candidate passes these plus whole-PCM
ordinary/partial-write/pause-resume/paused-seek tests. The separate render-only
60-case WAV oracle and four cleanup tests remain valid. Device audio behavior,
latency, vendor ROM timbres and perceptual quality require separate validation.
Both kinds of doubles live only under tools, excluded from Android source sets.
