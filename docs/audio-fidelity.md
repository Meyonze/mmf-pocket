# Audio fidelity work

The reference handsets are J-SH51 and 905SH. No hardware recordings are available;
neither handset is claimed to be emulated exactly. Phone speaker processing is a
separate optional output effect, not a replacement for accurate synthesis.

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
