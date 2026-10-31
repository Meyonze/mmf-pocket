# Original custom FM comparison probes

`tools/generate-fm-probes.py` creates thirty-four small SMAF scores with original,
explicit MA-5 FM voices. They exercise synthesis parameters without a Yamaha
ROM, SDK, commercial song, or proprietary voice bank. The generator uses only
Python's standard library; no external implementation code was copied into it.

```sh
python3 tools/generate-fm-probes.py build/fm-comparison-probes
python3 tools/generate-fm-probes-test.py --native
```

The destination must not exist, so generation cannot overwrite captures. Files
are deterministic. `manifest.json` records every operator field, event byte,
timestamp, file length, CRC trailer, and SHA-256. Regenerate into a new directory
when changing the generator; preserve the manifest with external recordings.

| File | Parameter under comparison |
| --- | --- |
| `sine.mmf` | Sine carrier; inactive modulator with AR=0 |
| `fm-2op.mmf` | Two sine operators in series; modulator TL=24 |
| `fm-4op.mmf` | Four sine operators in series; algorithm 4 |
| `waveform-1.mmf` | Carrier waveform code 1 |
| `envelope.mmf` | Carrier AR=8, DR=6, SL=6, SR=0, RR=5 |
| `feedback.mmf` | Two-operator FM; modulator feedback code 3 |
| `vibrato-slow.mmf` | Carrier EVB=1, DVB=3, LFO code 0 |
| `vibrato-fast.mmf` | Carrier EVB=1, DVB=3, LFO code 3 |
| `pitch-bend.mmf` | Explicit two-semitone range; center, +4096, -4096 |
| `waveform-square.mmf` | Carrier square waveform code 6; inactive AR=0 modulator |
| `waveform-triangle.mmf` | Carrier triangle waveform code 16; inactive AR=0 modulator |
| `waveform-saw.mmf` | Carrier saw waveform code 24; inactive AR=0 modulator |
| `waveform-19.mmf` | Carrier WS19: positive ramps at phase 0–¼ and ½–¾; zero elsewhere |
| `waveform-22.mmf` | Carrier WS22: unity at phase 0–¼ and ½–¾; zero elsewhere |
| `waveform-26.mmf` | Carrier WS26: positive ramp repeating each half-cycle |
| `waveform-27.mmf` | Carrier WS27: half-height positive ramps at phase 0–¼ and ½–¾; zero elsewhere |
| `waveform-29.mmf` | Carrier WS29: positive ramps in the first two quarters, then zero for the second half-cycle |
| `waveform-7.mmf` | Carrier WS7; compare the unresolved decay shape |
| `waveform-8.mmf` | Carrier WS8; compare plateau boundaries and transition curvature |
| `waveform-9.mmf` – `waveform-13.mmf` | Carrier WS9–13, isolating the related gated/repeated shape family |
| `square-active-modulator.mmf` / `saw-active-modulator.mmf` | Shaped modulator into a sine carrier; retain nonlinear synthesis |
| `square-active-pm-carrier.mmf` / `saw-active-pm-carrier.mmf` | Live sine modulator into a shaped carrier; retain nonlinear synthesis |
| `square-feedback-output.mmf` / `saw-feedback-output.mmf` | Shaped direct output with feedback; retain feedback synthesis |
| `square-additive-output.mmf` / `saw-additive-output.mmf` | Independent sine and shaped direct outputs; only the shaped output is bandlimited |
| `ksr-bend-before-note.mmf` | KSR enabled; +8191 bend then NoteOn at 250 ms |
| `ksr-bend-after-note.mmf` | Same KSR voice; NoteOn then +8191 bend at 250 ms |

All scores use uncompressed Mobile Standard format 2, 10 ms duration and gate
bases, and a single melody channel with KeyControl ON. Bank MSB/LSB/program are
zero and match the embedded custom voice. Basic octave 1 leaves score pitch
untransposed. Voice pan is disabled; channel pan is 64, volume 100, expression
127, and hold 0. CC1 is omitted because its Mobile/MA-5 semantics remain
unverified; the vibrato probes vary only intrinsic voice EVB/DVB/LFO fields.
All notes have velocity 100, start at 250 ms, and have a 1500 ms gate.
EOS is at 3000 ms. All probes except the KSR pair use note 69. In `pitch-bend`, bends
occur at 750 and 1250 ms, with center restored at 2000 ms. The two-semitone range is set through RPN 0/0 followed by
RPN null. No guessed MA-7 extensions are included.

The two KSR probes use note 65 (F4), an inactive AR=0 modulator, and a sine
carrier with KSR enabled, AR=6, DR=0, SR=0, RR=5. They explicitly set the same
two-semitone range and RPN null, then send bend +8191 (14-bit value 16383) and
NoteOn at the same 250 ms timestamp. Only the order of those two events differs;
there are no later bends. Both events execute before the first note sample, so
the oscillator starts at the same bent frequency in both files. This pitch crosses
the current KSR offset's half-octave boundary (offset 8 to 9). The current
engine measures octaves from C0, `16.3515978313 Hz`; F4 moves from approximately
4.4167 to 4.5833 octaves above that reference. The test checks this configured
boundary, without treating the formula as a hardware oracle. KSR is bit 0 of VM5 operator byte 0,
verified against both the project's voice parser and the independent parser.

This pair asks an unresolved reference question: does KSR latch from pitch at
NoteOn, or update with later pitch changes? The current engine initializes envelope
KSR at NoteOn using the bent frequency; later pitch changes update the oscillator.
It can therefore give this pair different envelopes. That difference is a candidate
behavior to compare with external recordings, not evidence of a defect or of the
correct ordering. The common timestamp isolates event ordering from elapsed
samples. File titles and CRCs also differ; their custom voices and other events
are identical. Tests pin all twenty-six pre-r60 MMF SHA-256 values (including the F4 KSR pair)
to preserve captures.

The square, triangle, and saw probes retain the sine probe's carrier defaults,
changing only its waveform code to 6, 16, and 24 respectively. The five probes
`waveform-19`, `waveform-22`, `waveform-26`, `waveform-27`, and `waveform-29`
also retain those carrier defaults and the inactive AR=0 modulator, changing only
the carrier waveform code. The table uses normalized
phase 0–1 and describes the current implementation. These original software
test inputs also support external reference capture; their names do not establish
hardware waveform calibration or spectral accuracy.

The seven WS7–13 additions retain the same carrier defaults and inactive
modulator. Published software references disagree on these curves; the board
waveform chart does not resolve the exact formula. These files extend reference
capture coverage without changing the renderer. See the
[waveform investigation](fm-waveform-investigation.md). All nineteen earlier
MMF files remain byte-identical, so existing captures retain their meaning.

r60 adds eight scope-control files. The active-modulator, active-PM-carrier and
feedback-output pairs retain exact r59 WAVs. Only the two additive-output cases
change, along with the earlier pure-square and pure-saw candidates. All26 older
FM input bytes are preserved. The full pack now has123 original MMFs; six WAVs
change in total, including two fallback-tone cases, while117 remain exact.

Container lengths are big-endian payload lengths. The outer MMMD length includes
the two-byte CRC. CRC is normal/non-reflected polynomial `0x1021`, initial
`0xffff`, final XOR `0xffff`, over the entire file before the CRC trailer.
Setup uses `F0 length 43 79 07 7F 01 ... F7`; its single-byte length includes F7.
The MA-5 voice body has three global bytes followed by seven bytes per operator.

## Validation and its limits

The suite checks nested lengths, CRC (with a separate bitwise implementation),
delta/gate encoding, exact score events, deterministic generation, and refusal
to overwrite an existing directory. `--native` compiles a temporary C++ checker
against the project's source. For every probe it compares decoded voice fields,
checks a complete score scan and the 3000 ms endpoint, and renders finite,
audible PCM with one FM note, zero fallback/PCM/audio notes, zero limiter events,
and no tail truncation. The custom voice therefore binds during project playback.
These checks are not measurements of hardware accuracy.

All thirty-four probes have also passed an independent structure, CRC, event, and voice
field check with [but80/go-smaf](https://github.com/but80/go-smaf), MIT license,
commit `545503dc3bc1fa7253b17c9739b154053b8cfd82`. Its parser was used as an
external oracle and format reference; its source and dependencies are not vendored
in this project. The parser does not report EOS as an event, so EOS timing is
checked by the separate byte decoder and the project's score endpoint. That old
parser is not a complete format conformance validator.

Handset and Galamelo acceptance, FM envelope/rate accuracy, and reference audio
remain unverified. The generated MMFs contain no reference WAVs: a local render
is a candidate recording, not a synthesis oracle. Code names such as `sine` and
`vibrato-fast` describe the authored parameters; physical oscillator and LFO
calibration still requires external recordings. These probes do not measure
Yamaha ROM timbre fidelity.

To capture, play the **same generated MMF** in each player. Keep raw recordings,
device/player versions, gain and effect settings, output rate, recording chain,
and the manifest. See [reference-capture.md](reference-capture.md) and
[audio-comparison.md](audio-comparison.md). Keep several seconds of capture after
the sequence endpoint if a reference player has a longer release tail.

## Repeating the independent parser check

Use a temporary checkout of the pinned reference commit, leaving its source
unchanged. The historical repository lacks `go.mod`. In a temporary copy add:

```go
module gopkg.in/but80/go-smaf.v1

go 1.20

require (
    github.com/pkg/errors v0.9.1
    github.com/golang/protobuf v1.5.4
    golang.org/x/text v0.22.0
)
```

Create `cmd/probecheck/main.go` in that temporary copy:

```go
package main

import (
    "encoding/json"
    "os"
    "gopkg.in/but80/go-smaf.v1/chunk"
)

func main() {
    if len(os.Args) != 2 { panic("expected one MMF path") }
    f, err := chunk.NewFileChunk(os.Args[1])
    if err != nil { panic(err) }
    if f.CRCWant != f.CRCGot { panic("CRC mismatch") }
    err = json.NewEncoder(os.Stdout).Encode(map[string]interface{}{
        "crc_valid": true, "file": f,
    })
    if err != nil { panic(err) }
}
```

Run `go mod tidy` and `go mod verify`, preserving TLS verification and Go module
checksum verification, then `go build -buildvcs=false -o /tmp/fm-go-oracle-check
./cmd/probecheck`. Keep `go.mod` and `go.sum` with the validation record. The check
was run with Go 1.27.3; the portable SDK archive was verified against its HTTPS
release-manifest SHA-256. These temporary tools are development dependencies,
not app dependencies.

From this repository:

```sh
python3 tools/generate-fm-probes-test.py --native --oracle /tmp/fm-go-oracle-check -v
```

The wrapper must exit nonzero for a parse or CRC failure and emit the documented
JSON envelope. Tests compare decoded timing, controller numbers and values,
notes, gates, bends, voice identity, global voice fields, and every authored
operator field. Without `--oracle`, the independent parser test is explicitly
skipped; without `--native`, the native binding/render test is explicitly skipped.
