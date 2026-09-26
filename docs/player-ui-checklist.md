# Player state manual checks

These checks require an Android device; they are not marked passed by a build.

- No song selected: toggle speaker mode; Play remains disabled.
- Select and play a song, then toggle speaker mode: audio stops, title/selection
  remain, progress resets, Play becomes enabled. Play uses the new mode from 0:00.
- Pause then toggle: same stopped/ready-to-restart behaviour.
- Toggle during conversion: the old conversion must not auto-start playback.
- Toggle repeatedly then Play: only the latest setting is used.
- Stop then Play: the selected song starts from the beginning.
- Batch conversion: browsing, playback controls and the speaker switch remain usable; batch progress is shown separately.
- Change folder: old selection cleared, Play disabled until a new song is chosen.
- Conversion error: Play remains available to retry, no stale audio starts.
- Background/foreground: audio stays stopped; Play can restart the retained song.
