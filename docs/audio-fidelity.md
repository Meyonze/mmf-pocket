# Audio fidelity work

The reference handsets are J-SH51 and 905SH. No hardware recordings are available;
neither handset is claimed to be emulated exactly. Phone speaker processing is a
separate optional output effect, not a replacement for accurate synthesis.

## References

- Yamaha YMF825 tone parameters (Yamaha-authored manual retained in a fork):
  https://github.com/junk16/ymf825board/blob/master/manual/fbd_spec3.md
  This documents a related FM engine, not a verified MA-3/MA-7 hardware model.
- https://github.com/but80/smaf825 (VM35/VMA voice field interpretation).

## MA-7 investigation

Format 3 must not be decoded as Mobile Standard format 2. Local MA-7 files use
different sequence commands and `43 79 08` voice messages. The `SEQU` branch in
vavi-sound is explicitly marked uncertain for format 3 and describes a four-channel
grammar; it is not sufficient evidence for the 32-channel MA-7 files here.
Keep format 3 rejected until event and voice decoding can be validated together.
Do not turn an audible but incorrectly decoded result into a compatibility claim.

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
  fallback drum bank, FM modulation depth, waveforms and PCM path remain
  approximations. MA-7 cannot yet be enabled safely.
- Phone-speaker mode remains an optional generic effect, with no handset preset
  or claim of SH51/905SH acoustic calibration.
