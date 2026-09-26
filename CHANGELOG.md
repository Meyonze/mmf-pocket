# Changelog

## 1.4.0-beta.7

- Decode MA-3 packed PCM parameters and embedded voice-wave ADPCM blocks that
  previously fell back to unrelated FM instruments. Distinguish ROM from RAM;
  RM is not a loop flag. Use authored Fs, loop/end points, pan and XOF.
- Apply live volume, expression, pan and pitch bend to sampled voices; handle
  interpolation and large rate steps at loop boundaries.
- Pair each gate with its note instance, fixing overlapping/repeated-key FM
  and PCM notes that released too early or kept sounding.
- Correct FM algorithm 3 routing, retire voices when their output carriers
  finish, and prefer quiet release tails when the FM voice pool is full.
- Replace the fixed one-second cutoff with natural envelope/filter draining,
  retaining bounded conversion and a fade at the safety boundary.
- Remove beta.6's uncalibrated PCM-to-FM level compensation. Do not substitute
  unrelated embedded waves or treat Analog Lite voice data as PCM.
- Honor zero velocity; make pool allocation deterministic after seek/reset.
- Expand synthetic audio/security tests and local corpus diagnostics.
- Invalidate previous WAV caches automatically (renderer r6).

## 1.4.0-beta.6

- Limited post-song release rendering to the declared one-second allowance;
  the renderer no longer adds a second hidden second of sustained audio.
- Preserved note velocity for embedded PCM and total-level attenuation when a
  proprietary handset-ROM PCM voice falls back to the ROM-free FM bank.
- Removed artificial sample-rate reduction from the handset-speaker effect and
  reduced its saturation to avoid buzzy noise on dense arrangements.
- Invalidated older converted WAV caches automatically (renderer r5).

## 1.4.0-beta.5

- Replaced the recursive flat MMF list with hierarchical folder browsing.
- Folders are listed before MMF files and can be opened or left with the parent
  entry or Android Back action.
- Limited batch conversion to the currently displayed folder.

## 1.4.0-beta.4

- Enlarged the launcher artwork and reduced transparent outer padding so the
  retro flip-phone mark is easier to recognize at normal launcher-icon sizes.

## 1.4.0-beta.3

- Retain the selected song after stop/speaker-mode changes; Play renders with
  the current mode and restarts it without requiring another list selection.
- Expanded the abbreviated speaker-mode label and disabled mode changes during
  batch conversion so its settings stay unambiguous.

- Corrected VM35/VMA sustain semantics: SR=0 holds rather than starting release.
- Replaced linear amplitude decay with exponential decay and related Yamaha
  YMF825 documented rate timing (an approximation, not measured MA-3 emulation).
- Corrected zero-rate, XOF, neutral detune, key-level scaling and LFO handling.
- Added synthetic envelope/timing/end-of-song regression tests to CI.
- Invalidated old render caches automatically (renderer r4).
- MA-7 remains unsupported: its event and voice grammar require further work.

## 1.4.0-beta.2

- Fixed 10x playback speed on files using 10/20/40/50 ms SMAF timebases.
- Implemented the 29 published VM35 operator waveform shapes instead of folding them onto eight approximations.
- Fixed conversions that could continue far beyond the song safety limit because a release envelope never became idle.
- Made cached WAVs renderer-versioned so timing and synthesis fixes take effect without manual cache clearing.
- Made the handset-speaker effect clearly audible and renamed it to the more accurate "携帯SP" label.
- Detect MA-7 Score Format 3 and report it as unsupported instead of mis-decoding it as MA-3/MA-5.

## 1.4.0-beta.1

- Added the compact player panel and integrated the phone-speaker mode.
- Added reusable conversion cache and batch conversion.
- Fixed system-bar insets and light status/navigation-bar visibility.
- Added bounded parsing for untrusted MMF input.
- Added privacy, security, licensing and release documentation.
- Hardened GitHub Actions and added a native sanitizer smoke test.
