# Audio fidelity work

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

## References

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
ROM percussion and handset acoustics. Inline voice/wave updates are collected
at load time rather than changed during playback. MA-5 bulk-wave variants and
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

This fixes a confirmed pitch-decoding error, not a proven loudness match.
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
