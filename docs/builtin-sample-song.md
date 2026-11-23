# Built-in sample song: Blue Velocity

`app/src/main/res/raw/blue_velocity.mmf` is an original song bundled with the app
so that it can be tried before choosing a folder.

- Title: Blue Velocity. Artist: Meyonze feat.Claude (stored in the MMF CNTI chunk).
- Original composition, original custom FM voices. No ROM voices, samples,
  recordings or existing melodies. Distributed under the repository license
  (Apache License 2.0), like the rest of the app.
- Format: uncompressed Mobile Standard SMAF (MA-5 class), 13 channels of 4-op/2-op
  FM, 162 BPM, about 57 seconds. SHA-256
  `241b022d7c96b21dca0160e0a64118b781008b0efabb999b2495c04dd89d72e4`.
- Checked with this engine: every note resolves to its own voice (no fallback),
  no voice stealing, no clipping (peak -1.8 dBFS).

## In the app

The song is listed at the top of the list before a folder is chosen and at the
top of the chosen folder's root. It is played like any other MMF (the converted
audio is cached by content hash), but it is not part of the folder: it is not
counted in the folder summary, not queued for continuous playback and not
included in batch conversion.
