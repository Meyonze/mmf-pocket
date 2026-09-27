# Player state manual checks

These checks require an Android device; they are not marked passed by a build.

- No song selected: toggle speaker mode; Play remains disabled.
- Select and play a song, then toggle speaker mode: audio stops, title/selection
  remain, progress resets, Play becomes enabled. Play uses the new mode from 0:00.
- Pause then toggle: same stopped/ready-to-restart behaviour.
- Toggle during conversion: the old conversion must not auto-start playback.
- Toggle repeatedly then Play: only the latest setting is used.
- Stop then Play: the selected song starts from the beginning.
- Previous and Next in the app move through the same queue as notification controls.
- Previous after a random transition returns to the track that actually played immediately before it.
- Continuous playback off: the selected song ends without advancing.
- Continuous playback on: ordered playback advances through the current folder and stops at its final song.
- Toggle continuous playback near a song boundary: the setting in force at completion decides whether playback advances.
- Toggle random playback during a song: the current song continues without restarting or jumping.
- Random playback with continuous playback off: the current song ends and playback stops.
- Random playback with continuous playback on: the next song is chosen from unplayed MMFs directly in the visible folder, with no duplicates or nested-folder files.
- Cached and uncached songs can be mixed in a queue, including while the screen is off.
- While an uncached song finishes rendering, the playback thumb never moves backward unless the user seeks backward.
- Changing folders after playback starts does not change the active queue snapshot.
- Switching speaker mode preserves the active queue and resets its current song to 0:00.
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
