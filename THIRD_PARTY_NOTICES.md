# Third-party notices

## yamaha-smaf-player

- Project: [akustikrausch/yamaha-smaf-player](https://github.com/akustikrausch/yamaha-smaf-player)
- Vendored commit: `627f7ad8583ce34c5fec0269ef9778167af080d6`
- Copyright: 2026 Akustikrausch (Andreas Wendorf)
- License: Apache License 2.0
- Local source: `third_party/yamaha-smaf-player/`

The original license is retained at `third_party/yamaha-smaf-player/LICENSE`.

MMF Pocket changes the vendored source in the following ways:

- If an MMF requests a handset-ROM PCM voice without carrying its waveform, playback falls back to a ROM-free FM approximation instead of silence.
- Parser and decoder resource ceilings are added for tracks, wave chunks, custom voices and chunk scans.
- Bounds checks used while skipping malformed event data are hardened.
- SMAF 10/20/40/50 ms timebases and the published VM35 operator waveform shapes are implemented.
- MA-7 Score Format 3 is detected and rejected rather than decoded with the incompatible MA-3/MA-5 grammar.
- Envelope sustain, zero rates, decay curves, detune and LFO behaviour are
  corrected using related Yamaha documentation; see `docs/audio-fidelity.md`.
- Note-instance gate matching, FM routing/voice retirement and release draining
  are corrected. MA-3 PCM parameters/voice-wave blocks are unpacked and played
  with ROM/RAM separation, authored pitch/loop settings and live controllers.

Modified source files carry a modification notice as required by Apache-2.0.

## Android NDK / LLVM runtime

Native binaries are built with the Android NDK and may contain parts of the LLVM runtime and libc++. LLVM is distributed under Apache License 2.0 with LLVM Exceptions. The Android NDK distribution contains the authoritative notices in `NOTICE`, `NOTICE.toolchain`, and the toolchain `NOTICE` files.

- [LLVM license](https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT)
- [Android NDK](https://developer.android.com/ndk)

The root `LICENSE` contains the Apache License 2.0 text.
