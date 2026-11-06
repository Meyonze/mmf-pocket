# Original tone-dependency probes

`tools/generate-tone-dependency-probes.py` writes fifteen small, original SMAF files
for checking scheduled tone-dependency reports. They contain no vendor ROM,
commercial melody, or proprietary voice bank.

```sh
python3 tools/generate-tone-dependency-probes.py build/tone-dependency-probes
build/host-tests/tone-usage-audit build/tone-dependency-probes /tmp/tone-dependency.tsv
build/host-tests/tone-usage-audit --details build/tone-dependency-probes /tmp/tone-dependency-details.tsv
```

The destination must be new. The generator refuses to overwrite an existing
folder, including external captures. Its manifest records explicit expected
routing reasons and substitute families, authored parameters, event bytes,
lengths, CRCs, and SHA-256 hashes. Expectations are fixture declarations, not
results computed by another copy of the player routing logic.

| Fixture | Authored request | Expected reason | Substitute |
| --- | --- | --- | --- |
| `embedded-fm` | Embedded two-operator sine carrier | `bound_fm` | None |
| `unbound-melodic-gm` | Bank 0, program 40, no definition | `no_matching_supported_voice` | `gm_modern_recipe` |
| `unbound-percussion` | Bank 125, note 60, no definition | `no_matching_supported_voice` | `drum_recipe` |
| `bound-ram-wt` | Bank 124, RAM wave 1 with authored 80-sample wave | `bound_ram` | None |
| `missing-ram-wt` | Same WT voice without its wave | `missing_ram_wave` | `gm_modern_recipe` |
| `explicit-rom-wt-0` | Explicit ROM wave 0 | `explicit_rom_wave` | `rom_recipe` |
| `explicit-rom-wt-22` | Explicit ROM wave 22 | `explicit_rom_wave` | `rom_recipe` |
| `explicit-rom-wt-26` | Explicit ROM wave 26 | `explicit_rom_wave` | `rom_recipe` |
| `explicit-rom-wt-63` | Explicit unmapped ROM wave 63 | `explicit_rom_wave` | `gm_modern_recipe` |
| `custom-drum-missing_ram-bank0/124` (two files) | Drum key 60 with missing RAM wave 63 | `missing_ram_wave` | `drum_recipe` |
| `custom-drum-rom-bank0/124` (two files) | Drum key 60 with unmapped ROM wave 63 | `explicit_rom_wave` | `drum_recipe` |
| `custom-drum-bound-ram` | Drum key 60 with authored RAM wave 1 | `bound_ram` | None |
| `custom-drum-known-rom` | Drum key 60 with mapped ROM wave 22 | `explicit_rom_wave` | `rom_recipe` |

All files use uncompressed Mobile Standard format 2, 10 ms duration/gate bases,
and one melody channel with KeyControl ON. Bank, program, volume, expression,
pan, hold, and centered bend are explicit. One velocity-100 note starts at
250 ms, has a 1500 ms gate, and precedes a 3000 ms sequence endpoint. Note 60
in the bank-125 fixture lies outside the recorded-Audio note ranges, isolating
the percussion substitute path. Custom-drum fixtures also use note 60. Other
fixtures use note 69.

WT voices use MA5 long-form type 1. The PCM body's last byte contains ROM bit
7 and a seven-bit wave ID; its sample rate is 8000 Hz, loop point 0, and end
point 80. The RAM bulk exclusive supplies unsigned PCM8 wave 1. Its 80 samples
combine two self-authored sinusoidal components. The explicit-ROM fixtures
contain addresses only, without any ROM sample bytes. Known ROM addresses name
current software approximation recipes; they do not establish ROM fidelity.
Custom-drum fixtures exercise the current parser's nonzero drum-key interpretation
outside bank 125. They check consistent handling of an accepted definition, not
independent proof of handset-format interpretation.

Outer/chunk lengths are big-endian payload lengths. The MMMD payload includes
the trailing CRC: non-reflected polynomial `0x1021`, initial `0xffff`, final XOR
`0xffff`, covering every preceding byte. Setup exclusives use Mobile variable
lengths including the F7 terminator.

Native inventory and local renders can establish that definitions bind and the
scheduled paths produce finite output. These checks do not verify handset
acceptance, vendor timbre, audible dependency severity, or a reference waveform.
Keep manifests with any external recordings; a local render is a candidate,
not an independent synthesis oracle.

## Detail report

`--details` emits a summary for every parsed input and groups note routes by
channel, source track, bank MSB/LSB, zero-based program, raw trigger note,
zero-based selected definition, wave ID, synthesis profile, reason and substitute
strategy. A missing definition/wave field is `-1`; no substitute is `-`.
Counts and first/last scheduled seconds identify which passages request a path.
The default aggregate TSV remains unchanged.

`no_matching_supported_voice` is not evidence of a missing ROM: there may be
no definition, a different key/split, a definition not yet activated, or a
rejected unsupported voice image. Only a selected PCM definition with the ROM
bit set is `explicit_rom_wave`. Missing RAM is reported separately. Recorded
Audio takes priority over instrument lookup and never becomes fallback FM.
Zero velocity, permanent MA7 silence and bound-wave EP0 no-pronunciation have
separate reasons. Recorded velocity 1 can yield `recorded_no_instance` without
checking whether the wave exists. ATR rows use their own source track and
one-based wave number, with note/bank/program set to `-1`.

`rom_recipe` identifies the existing original FM approximation, not a recovered
ROM waveform. Known modern-profile ROM addresses 0–28 currently select 18
distinct patch recipes; some addresses share a recipe. Other addresses and
missing RAM use the current drum or GM policy. The `requested_*` fields expose
retained AL/fixed-frequency/MA7 detune/user-wave metadata of the selected voice;
they do not claim that fallback synthesis applies those fields or that the
note sounds. Rejected/unused definitions still require `score-audit`.

This replays scheduled routing without advancing oscillators or envelopes.
It cannot establish audibility, voice lifetimes, actual simultaneous voices,
hardware quality, or how severely a listener notices a substitute. Events at
or beyond the score's playback limit (at most 600 seconds) are excluded and
counted in `after_bound`. Detail memory is capped at 4096 groups per file;
new groups beyond capacity increment `detail_overflow`, and the CLI returns
nonzero after writing the partial report. Summary rows exist even for scores
with no eligible note/ATR routes. Parse/init failures also return nonzero;
always check the exit code before interpreting a report as complete.

Both inventory CLIs refuse output that identifies an input, including symbolic
and hard links, before opening the output file. Empty/missing input also leaves
an existing report intact. Ordinary report output can be replaced deliberately.

Run the original wire-fixture and input-protection checks against built tools:

```sh
python3 tools/tone-usage-audit-cli-test.py --binary build/host-tests/tone-usage-audit
python3 tools/score-audit-cli-test.py --binary build/host-tests/score-audit
```

For mixed score formats, the synthesis-profile and substitute-family columns
use each note's originating MTR header and grammar, matching the r81 renderer.
An inert overlapping track cannot change a HandyPhone report to modern GM or
turn a Mobile melody report into a drum substitute. Self-tests include twelve
source-profile cases alongside the existing route and output-safety cases.
