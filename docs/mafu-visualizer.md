# Mafu audio-reactive animation

## Prominent accents and calmer cadence

Separate phrase-attack accounting from visible jump selection without changing
the sound engine or PCM measurements. Weak decorations after stronger recent accents contribute to the
overall expression but do not each trigger a jump or flip the lean direction.
Select prominent measured onsets relative to a decaying recent peak (58%),
with a minimum 420 ms visible recovery and a bounded 420..800 ms recovery adapted
from accepted accent spacing. Very fast patterns deliberately feel half-time
or slower. This is prominence-based thinning, not exact tempo/meter tracking,
lead-melody extraction, fixed note counting or a predicted/fake beat grid.
Each accepted jump follows a continuous sine-squared takeoff/landing arc over
340..640 ms. No beat-free periodic hops; existing started acrobatics still finish.
Reset cadence history on pause/seek/source changes. No extra worker or timeline
allocation. Cadence tests cover dense 160 ms attacks, strong 480 ms accents plus
80 ms ornaments, 30/60/120 Hz, slower accents, complete flight and resets.

Starting with 1.6.4, Mafu samples visual measurements at the controller's current
playback position, not the renderer's ahead-of-playback cursor. This changes
only animation: the native decoder, sound processing, conversion-cache identity,
fixed 3D model and compact player layout stay as in 1.6.3.

## Motion and limitations

40 ms windows contain sound strength, approximate low/high-band energy and a
positive energy-onset estimate. Stereo is measured per channel before combining
energy, retaining opposite-phase recordings. Display-rate smoothing drives
bounce, sway, tilt, squash/stretch and the shadow pulse.

A 1.6-second energy history and 2.2-second weighted attack history determine
expression strength. Loud sustained notes alone or very quiet quick notes do
not imply an energetic passage. Intensity rises with a 0.9 s time constant and
falls with 1.8 s; this deliberately takes several seconds to follow a new section.
Acrobatics are armed above 0.62 and disabled below 0.45, with a 7-second cooldown.
Cartwheels, backflips, pirouettes and hops alternate; an already-started move
finishes naturally within 3.2 s even when the passage becomes quiet.

Silent or unmeasured playback uses gentle breathing, side sway and tilt.
These cues blend away as measured sound arrives; they are not invented beats.
Pause and stop keep the existing subtler idle pose. Track changes, pause and
seeks reset visual history; missing windows settle smoothly without freezing
an unfinished flourish or switching to a fake-tempo routine.

This is a heuristic response to the mixed audio, including accompaniment, not
lead-melody extraction, genre/mood recognition, FFT, exact drum/BPM detection,
microphone capture, system-volume measurement or speaker-output analysis.
Sub-window timing is approximate. Device performance and perceived sync can
vary; analysis and animation have a bounded but nonzero CPU/memory cost.

## Playback isolation and privacy

The progressive renderer calls a bounded no-I/O PCM observer after WAV bytes
are written. It does not retain or mutate the reused array. Optional runtime
callback failures disable the observer without failing conversion. Cached and
directly selected WAVs are analyzed incrementally on a separate background
worker, not the UI, preparation worker or playback loop.

Visual WAV analysis supports mono/stereo RIFF PCM8/16/24/32 and float32,
including matching WAVEFORMATEXTENSIBLE subtypes/valid bits. Odd chunk padding
and parent boundaries are honored. Unsupported codecs/layouts or missing
measurements leave gentle motion without failing otherwise supported playback.
Provider analysis owns its descriptor and stream, independent of MediaPlayer's
descriptor/position; asset offsets/lengths are respected. Opening has a
CancellationSignal; cancellation and closing run off the UI. Providers that
ignore cancellation can still delay optional analysis.

A generation token plus media ID identifies the exact same-process source.
Old callbacks cannot replace a newer track/mode. No Context, player or View is
retained in the static source bridge. The Activity detaches its provider when
hidden; service audio playback continues independently.

Timeline storage is lazy paged and capped at 90000 windows/one hour, about 1.45 MB
of float payload at the cap. A 3-minute track needs about 74 KB. Later audio plays
normally but has no new visual measurements. Data is not saved or transmitted;
no microphone, network or broad-storage permission is added.

## Regression coverage

Original synthetic host tests cover stereo phase, bands, block invariance, PCM
immutability, unusual sample-rate timing, 24 WAV variants/bounds, cancellation,
source ownership, smooth expression, weak attacks, equal-volume slow/fast
phrases, loud sustains, quiet quick notes, isolated accents, transitions,
30/60/120 Hz, seek/reset/gaps and bounded concurrent timeline publication.
Actual Java render-loop tests keep whole WAV bytes/frames identical with the
observer enabled, disabled or throwing. Analysis lifecycle tests use cooperative
provider doubles, not proof of real SAF timing/cancellation. Pose tests exercise
the actual View math and opaque mesh with host graphics stubs for silent motion
and compact-card headroom, not Android Canvas/GPU/frame-rate emulation.
