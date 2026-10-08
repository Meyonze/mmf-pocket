# Audio fidelity work

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
