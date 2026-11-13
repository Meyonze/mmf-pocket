# Changelog

## 1.6.8 - 2026-11-13

- Restyle the header wordmark with bold MMF and regular Pocket. Check GitHub for
  stable updates only when the version label is tapped; offer to open the release
  page only if a newer version is available. No automatic checks or telemetry.
- Refine the extreme-rate PCM cutoff grid to reduce reconstruction error against
  the existing filter model. Refresh converted-audio caches.
- Expose requested AL register values in host diagnostics without applying AL DSP.

## 1.6.5 - 2026-10-31

- Show embedded MMF song titles and available artist/composer/lyricist/arranger
  credits, including Japanese CNTI and OPDA/Dch metadata. Separate title-embedded
  "by creator / site" credits into a normal-weight line below the bold title;
  preserve ordinary English titles and fall back to filenames when absent.
- Compact the playback panel by18dp at normal text size and reduce the gap
  above it by4dp. Preserve the mascot and play icon sizes, let large time text
  grow naturally, and keep status/settings beside the song information.
- Bind Mobile recorded Audio to the originating MTR, including compressed
  transport and remembered-velocity notes. A later MA7 track sharing the channel
  could select its own Mwa instead, or mute the note when absent. Keep existing
  channel profiles and legacy fallback; align tone diagnostics and refresh
  rendered caches to r63.
- Correct the safety-tail horizon when long ATR recordings are displaced by
  PCM voice allocation. Use completed source/gate bounds after all actual ATR
  voices and authored events finish; preserve live/muted long recordings,
  future cues, resampling rounding and seek replay. Refresh caches to r62.
- Preserve authored clocks for complete ignored controller/reserved commands
  in Mobile/MA7 scores, including physical EOF without EOS. Retain truncated
  recovery and coverage classification; correct the score/tail endpoint and
  refresh rendered caches to r61. Add EOF and sustained-tail regressions.
- Reduce aliasing in modern unmodulated square/saw final carriers with
  amplitude-compensated PolyBLEP. Exclude modulators, feedback nodes and
  carriers with live upstream operators; preserve legacy synthesis. Cache
  constant-pitch coefficients and share a small interpolated gain table.
  Refresh rendered caches to r60 and add eight original scope-control scores.
- Add a standalone FM carrier spectrum audit with analytic/DFT self-checks
  and twelve coherent-frequency measurements to establish a baseline for
  future alias-reduction work; leave the r59 renderer unchanged.
- Add explicit frame intervals to the PCM16 WAV comparison tool to measure
  onsets and sustained notes separately, retaining original frame positions
  and reporting the selected range without rewriting recordings.
- Add seven original FM waveform7–13 comparison scores and document the
  disagreement between published waveform models. Preserve all nineteen
  earlier captures and the r59 renderer while reference accuracy is unresolved.
- Keep unapplied Mobile CC1 as a timed no-op when mixed-format tracks share
  channels. Prevent it from borrowing another track's MA7 vibrato law while
  preserving genuine MA7 modulation and intrinsic voice EVB; refresh caches
  to r59.
- End exact-zero FM tails caused by the existing Nyquist guard or unbound
  user-wave selectors after all authored events and the shared filter drain.
  Preserve pitch revival, live carriers and slot allocation; refresh caches
  to r58 so older conversions lose the unnecessary silent safety tail.
- Reject MA7 FM/AL images that omit operators required by their algorithm.
  Preserve earlier complete voices and normal missing-voice fallback instead
  of synthesizing default extra operators; refresh caches to r57.
- Preserve recorded-Audio start order when several notes share a sample time.
  Replace the oldest successful trigger instead of repeatedly stealing the
  same slot; retain gates, source binding and the two-slot pool. Refresh caches
  to r56 and add original same-tick/multiple-track waveform regressions.
- Keep a selected custom PCM drum's classification when its RAM/unknown ROM
  wave is absent, including drum-key definitions outside bank125. Preserve
  mapped ROM recipes and bound RAM behavior; refresh converted caches to r55.
- Add bounded per-instrument tone dependency reports and fifteen original
  fixtures separating explicit ROM requests, missing RAM and unmatched voices.
- Prevent the score inventory CLI from overwriting an input through an output
  path, symbolic link or hard link, and preserve reports when input is absent.
- Replace common half/quarter-period FM remainder calls with exact phase
  subtraction and explicit positive-zero endpoints. Preserve the rounded
  phase-one and nonfinite fallback; this optimization leaves rendered audio
  unchanged.
- Add five original FM waveform comparison scores (codes 19/22/26/27/29),
  preserving all fourteen earlier comparison MMFs byte-for-byte.
- Prepare 64/128-tap PCM filter coefficients separately to reduce synthesis
  work while retaining the filter and source timeline. Refresh caches to r54
  because compiler fused arithmetic can change float rounding.
- Add a portable 32-voice synthesis benchmark with 22 original workloads and
  Android build instructions; device measurements remain outstanding.
- Compute sine only in FM waveform branches that use it, preserving samples
  and selector behavior while reducing work for square, triangle and saw waves.
- Add original square/triangle/saw comparison scores; preserve the existing
  eleven comparison MMFs byte-for-byte.
- Reduce unused FM modulation work by holding its LFO at zero when none of the
  operators consumed by the algorithm has AM/VIB depth. Preserve rendered
  samples, live controls and retriggers, including retained operator state.
- End provably silent FM and instrument-WT tails after the authored score and
  filter drain, avoiding a ten-second silent safety tail for stopped attacks. Keep
  slow/MA7 odd attacks, quiet nonzero voices and later events intact.
- Preserve the complete HPS/VMA bank byte so melody and drum banks with the
  same low seven bits and program select their own embedded instruments.
- Let naturally finished Hold-deferred FM carriers stop protecting a remaining
  release tail from voice allocation, preserving live held carriers.
- Serialize playback starts and volume publication with pause, resume and
  cancellation; suppress readiness after cancellation or failure during creation.
- Preserve FM oscillator phase through Nyquist-suppressed intervals so pitch
  returning into range follows elapsed source time.
- Reduce repeated loop-boundary indexing without changing audio output.
- Add original WAV/audio-track and custom-FM SMAF probe generation, independent
  structure/CRC validation, and a reference-capture workflow.
- Harden the local WAV renderer's quantization, input validation and storage
  error handling.
- Reduce PCM resampling aliasing with 64-tap filtering above 4:1 and 128-tap
  filtering above 8:1, with smooth transitions. Retain the existing path at or
  below 4:1, legacy profiles, pitch, event timing and source endpoints.
- Trim unreachable filter-table rows: both higher-rate banks together use about
  0.71 MiB, down from the initial 64-tap bank's 2.05 MiB.
- Correct Yamaha ADPCM delta rounding to divide once after combining the
  magnitude terms; verify against independent synthetic reference vectors.
- Add a dependency-free PCM16 WAV comparison tool for per-channel level, DC,
  sample differences and unmatched tails, without gain or timing correction.
- Refresh converted audio caches and add high-rate spectral, loop-history
  and boundary regression checks. This is renderer quality, not handset-ROM
  or hardware calibration.

## 1.6.4 - 2026-10-12

- Make Mafu react to the currently playing sound in MMF and supported WAV
  files, with beat-linked bounce, sway, tilt and smoothly animated flourishes.
- Select prominent accents and thin dense small attacks, with a complete
  smooth takeoff/landing arc instead of restarting a hop on each ornament.
- Combine recent sound strength and attack activity to keep gentle passages
  restrained and sustained energetic passages more expressive. Change the
  strength gradually rather than switching on a single loud accent.
- Show slow breathing and swaying during silent introductions or unavailable
  visual measurements, so playback still looks alive.
- Reset visual history on track changes, pause and seek; perform optional WAV
  analysis in the background and stop drawing audio motion when the UI is hidden.
- Expand original synthetic visualization, cancellation, PCM-output and pose
  regressions. No microphone, network permission or audio transmission added.
- Keep the 1.6.3 sound engine, conversion cache and compact player layout.
  Mafu reacts to the mixed sound, not an extracted lead melody or exact BPM.

## 1.6.3 - 2026-10-12

- Improve recorded score-Audio and ATR sample-rate conversion with interpolation
  and anti-alias filtering; preserve authored pitch, gain and event timing.
- Honor supported ATR gates, explicit end-of-sequence, volume, expression and
  pan with track-local state, without leaking controls into score instruments.
- Correct HandyPhone note retriggering, pitch/control field validation and
  malformed/zero-gate recovery; preserve valid following notes and prior state.
- Correct MA-7 silent velocity/bank handling and recorded-Audio origin/pan;
  preserve score rests, terminal event clocks and Mobile running velocity.
- Bound waveform/container reads and stop safely on unsupported-width or
  truncated events instead of guessing following notes or sample definitions.
- Improve PCM16 output rounding; retain representable quiet audio and detect
  WAV storage failures. Finish provably muted end tails without cutting off
  live sources or later authored events.
- Fix progressive playback failure cleanup and overlapping seek requests,
  including seeks during track replacement and playback-end draining.
- Expand original synthetic regression and sanitizer CI coverage. Refresh
  converted audio automatically; retain the player UI, Mafu and privacy policy.
- Hardware ROM timbres, device effects and several MA-7 synthesis/codec features
  remain incomplete. No per-song balance correction or exact emulation claim.

## 1.6.2 - 2026-10-08

- Improve parsed MA-3/5 custom FM synthesis while preserving the separate
  legacy, expanded MA-7 and ROM-free substitute profiles.
- Improve embedded instrument-wave interpolation and high-pitch downsampling
  without applying instrument processing to recorded score-Audio playback.
- Apply timed tone/wave updates and bank/program changes in authored order;
  keep sample sources separate and reject malformed replacement definitions.
- Honor implemented Mobile Hold1, All Notes Off, All Sound Off and Reset All
  Controllers semantics, including held notes and independent channel state.
- Correct ATR recorded-audio header/rate/clock parsing, retain all raw sample
  bytes and use track-local six-bit wave IDs. Fix empty container/track chunks
  that previously hid following sequence or sample data.
- Add direct WAV playback alongside MMF files, including folder queues,
  previous/next playback and background/notification controls. Batch conversion
  remains MMF-only; WAV files are not converted or processed by handset mode.
- Expand original synthetic audio/security tests and decoder diagnostics.
  Invalidate old converted audio automatically; retain player UI and Mafu.
- Fidelity remains partial: device-ROM sounds/effects are approximations,
  MA-7 AL filters, nonneutral pitch envelopes and calibrated fixed-frequency
  synthesis remain incomplete, and ATR gate/EOS/control handling is partial.

## 1.6.1 - 2026-10-02

- Improve MA-3/5 and MA-7 embedded instrument-wave decoding, with separate
  instrument RAM and recorded score-Audio playback to avoid wrong substitutes.
- Correct custom FM drum pitches, MA-7 volume/expression curves and pitch-bend
  sensitivity, plus Mobile RPN pitch-bend range changes during held notes.
- Retain the extra MA-7 FM/WT envelope-rate bits and apply authored PCM
  vibrato/tremolo; use a lightweight sine lookup for the new PCM modulation.
- Refine original ROM-free fallback voices and modern FM feedback/tremolo.
  These remain approximations, not recovered handset ROM or exact emulation.
- Expand synthetic audio/security regressions and invalidate older render
  caches automatically. Keep the existing player, Mafu and background controls.
- MA-7 signed PCM8 decoding is experimental. Analog Lite filters, nonneutral
  pitch envelopes, fixed-frequency synthesis and device-specific effects remain
  incomplete; full reference-recording fidelity is not claimed.

## 1.6.0 - 2026-09-29

- Add the 3D mascot Mafu (「まふ」), whose floating, dancing and running pace is
  selected from per-channel note activity in the current MMF.
- Give Mafu a solid rounded body with white plush shading, pale-blue shadows,
  a bold dark outline and depth-correct cartwheels, backflips, pirouettes and
  hops without sprite dissolves or overlapping afterimages.
- Rebuild the player card with track metadata and the seek bar beside Mafu,
  larger transport buttons and compact vertical spacing.
- Keep status text from repeating the filename, keep the seek thumb round at
  both ends, and vary the choreography between tracks.

## 1.5.2

- Correct MA-1/2 VMA `SUS` handling so affected accompaniment voices switch
  to the specified bounded release instead of remaining held after key-off.
- Retrigger each fixed HandyPhone part in one voice slot so released `SUS`
  tails do not layer underneath subsequent notes as an echo.
- Invalidate older converted WAV caches so the corrected MA-1/2 rendering
  is used immediately after the app update.

## 1.5.1

- Add optional continuous playback for the current folder, stopping at its end.
- Add an optional random-next mode for continuous folder playback without duplicate tracks.
- Add previous and next track controls to the in-app player.
- Keep the playback thumb visually stable when the final rendered duration becomes known.
- Use a light-blue selection background with dark text in the file list.
- Show the app version beside the folder conversion controls.
- Keep the parent-folder action fixed above the scrolling file list.
- Replace the platform-dependent color folder emoji with a monochrome outline icon.

## 1.5.0

- Continue playing the selected song while the app is in the background or the
  screen is off.
- Add notification and lock-screen controls for play, pause, stop and seek.
- Accept media controls from Bluetooth devices and wired headsets.
- Handle Audio Focus and audio-route changes without unintended playback
  restarts.
- Keep the Activity synchronized with the service-owned song, playback state
  and progress when the screen is reopened.
- Keep folder batch conversion foreground-only; it stops when the Activity is
  no longer visible.

## 1.4.1

- Run folder batch conversion on its own low-priority worker so browsing,
  playback controls, track selection and speaker-mode changes remain usable.
- Show batch progress separately from player status and serialize cache cleanup
  across interactive and batch rendering.
- Keep a compact batch-progress badge visible in the top-right corner while the
  conversion is running.
- Allow batch conversion to pause safely between files and resume from the next
  unprocessed file.

## 1.4.0

- Publish the first generally downloadable Android release of MMF Pocket.
- Browse folders and play MA-1/2/3/5 files plus experimental MA-7 Score
  Format 3 without a proprietary handset ROM.
- Start uncached songs after a short prebuffer and show rendered versus played
  progress separately while conversion continues.
- Display the detected MA generation and raw SMAF score format in the player.
- Use a proper adaptive launcher icon so Android does not add a second layer of
  legacy-icon padding around the artwork.
- Include cache reuse, folder batch conversion and the optional handset-speaker
  effect, with no ads, analytics or network permission.

## 1.4.0-beta.11

- Show the selected file's MA generation and raw SMAF score format in the
  player's lower-right corner. Shared legacy formats are labeled MA-1/2 or
  MA-3/5 rather than guessing a chip generation the file does not identify.

## 1.4.0-beta.10

- Add experimental MA-7 Score Format 3 playback with its validated 32-channel
  SEQU event grammar.
- Fold MA-7 2/4-operator FM and wavetable tone images into the existing
  ROM-free engine, including split-key selection. Analog Lite filter tails and
  device-ROM-specific effects remain approximations or unsupported.
- Validate all 61 local MA-7 files through a ten-second finite/audible probe;
  all 382 files in the private compatibility corpus pass.

## 1.4.0-beta.9

- Start uncached songs after a short PCM prebuffer while rendering and caching
  the remainder in parallel. The seek bar shows generated audio as secondary
  progress and limits seeking to the available prefix.

## 1.4.0-beta.8

- Buffer each rendered PCM block and write it to the WAV cache in one operation.
  Generated audio and the renderer cache identity are unchanged.

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
