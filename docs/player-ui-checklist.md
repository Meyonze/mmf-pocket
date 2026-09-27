# Player state manual checks

These checks require an Android device; they are not marked passed by a build.

- No song selected: toggle speaker mode; Play remains disabled.
- Select and play a song, then toggle speaker mode: audio stops, title/selection
  remain, progress resets, Play becomes enabled. Play uses the new mode from 0:00.
- Pause then toggle: same stopped/ready-to-restart behaviour.
- Toggle during conversion: the old conversion must not auto-start playback.
- Toggle repeatedly then Play: only the latest setting is used.
- Stop then Play: the selected song starts from the beginning.
- Batch conversion: browsing, playback controls and the speaker switch remain usable; a progress badge stays visible in the top-right corner.
- Batch pause: after the current file completes, progress stops; Resume continues with the next unprocessed file.
- Change folder while playing: the current song continues; the new folder can be browsed independently.
- Conversion error: Play remains available to retry, no stale audio starts.
- Background/foreground during playback: audio continues; returning shows the current song, state and position.
- Background/foreground while paused: playback remains paused and the Activity shows the retained position.
- Notification controls: Play, Pause, Stop and seek update playback and the Activity state.
- Lock-screen controls: Play, Pause, Stop and seek update playback and the Activity state.
- Bluetooth/headset controls: Play, Pause and Stop update playback and the Activity state.
- Audio Focus transient loss: playback pauses and resumes only if it was still intended to play.
- Audio Focus permanent loss: playback pauses and does not restart by itself.
- Audio Focus change while paused or stopped: no unintended playback starts.
- Disconnect headphones or an active Bluetooth output: playback pauses.
- Stop from any surface, then Play: the retained song restarts from the beginning.
- Screen off and task swipe while playing: playback and its notification remain available.
- Background batch conversion: leaving the Activity cancels the batch after the current operation boundary.
