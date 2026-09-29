# Player state manual checks

These checks require an Android device; they are not marked passed by a build.

- No song selected: toggle speaker mode; Play remains disabled.
- The solid 3D mascot Mafu (「まふ」) keeps a rounded pale-blue body, note ears,
  mitten hands and oval feet; it must not regress to a simplified insect shape.
- Mafu fills the enlarged left character area without clipping during jumps.
  Its body reads as white and softly padded, with pale-blue shadowing rather
  than a gray or metallic surface; playback displays it at full opacity.
- A clearly visible dark outer line follows the complete 3D silhouette at all
  angles without trails, doubled edges or gaps around the note ears and feet.
- The large mascot spans the metadata and seek rows on the left. The seek bar
  begins beside the mascot, and the player card does not gain a separate tall
  mascot row or excessive vertical whitespace.
- The mascot column is narrower than its height, leaving more horizontal space
  for metadata. Previous, Play/Pause, Stop and Next are visibly larger than
  before, while the complete player card retains the same compact height.
- The player card keeps Mafu, title/status and format on its first row, the seek
  bar on its second row, and time/transport/mode controls on its third row.
- The seek thumb remains a full circle at 0:00 and at the track end; neither
  horizontal edge clips it into a semicircle.
- The status line says only the state (for example, "再生中") and never repeats
  the filename already shown directly above it.
- The blue primary Play/Pause button remains centered in the transport group;
  Stop sits before Next, and Repeat/Shuffle stay grouped after the divider.
- Paused/stopped Mafu breathes subtly. Slow songs float, ordinary songs dance
  with changing accents, and genuinely fast songs use larger whole-character
  run and hop motions without distorting the illustrated design. Ordinary
  playback adds occasional acrobatic choreography; switching tracks must select
  the new motion pace.
- Playback choreography visibly includes a cartwheel, a backward flip, a
  single figure-style pirouette and a two-step happy hop. Every move
  begins and ends at the same pose without a visual jump or rapid trembling.
- Different tracks can begin with different tricks. A backflip must show a
  crouch, a higher backward rotation and a landing squash, not resemble another
  low cartwheel.
- Halfway through a backflip the back emblem is visible upside down. The face,
  feet and ears are depth-tested parts of one solid model and disappear behind
  the body, never showing through or leaving translucent duplicate images.
- At 90 and 270 degrees in either rotation axis, the body retains its rounded
  thickness. Every intermediate angle is rendered from the same geometry.
- Check frame pacing on a device while an uncached song is converting, as well
  as during cached playback. Desktop render timing is not a device FPS result.
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
