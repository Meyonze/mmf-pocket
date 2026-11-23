# Built-in sample song: Blue Velocity

`app/src/main/res/raw/blue_velocity.mmf` is an original song bundled with the app
so that it can be tried before choosing a folder.

- Title: Blue Velocity. Artist: Meyonze feat.Claude (stored in the MMF CNTI chunk).
- Original composition, original custom FM voices. No ROM voices, samples,
  recordings or existing melodies. Distributed under the repository license
  (Apache License 2.0), like the rest of the app.
- Format: uncompressed Mobile Standard SMAF (MA-5 class), 16 channels of 4-op/2-op
  FM, 162 BPM, about 48 seconds (full band from the first beat; the chorus starts at
  15 s). SHA-256
  `8c1452ef8a62e4be325476813f9ac22e044ba1dd608817051fb776a2f308949d`.
- Checked with this engine: every note resolves to its own voice (no fallback),
  no audible voice stealing, no clipping (peak -1.2 dBFS).

## Showcase for the handset-speaker mode

The song is arranged so that the 携帯スピーカー風 mode (mono fold-down, 420 Hz
high-pass, 3.6 kHz low-pass, soft saturation; see `PhoneSpeakerFilter` in
`native_bridge.cpp`) is easy to hear: a pure sine sub bass and a low drone under
the choruses and the interlude, short bell sparkle and ride shimmer in the high
register, and a wide stereo image (arpeggio hard left, echo and strings hard
right, a left/right ping-pong sparkle in the interlude). The high band (4-8 kHz
-21 dB, above 8 kHz -24.5 dB of the total) stays between the levels of the
reference scores used for tuning.

This is a listening design, not a measured gain: the loudness-matched
band-by-band difference between normal and handset playback is about 4 dB for
this song and for the reference scores alike, because the filter shape and its
saturation dominate that number.

## In the app

The song is listed at the top of the list before a folder is chosen and at the
top of the chosen folder's root. It is played like any other MMF (the converted
audio is cached by content hash), but it is not part of the folder: it is not
counted in the folder summary, not queued for continuous playback and not
included in batch conversion.
