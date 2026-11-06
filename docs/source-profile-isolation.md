# Source-track synthesis profiles

An empty Mobile/MA7 track could change an existing HandyPhone voice on an
overlapping global channel. The reproduced four-channel, two-format,
two-insertion-order matrix changed every common waveform and shortened an
example from 4,985 to 2,397 frames at 8 kHz. No extra notes were required.

`decodeTrack_` now captures the source header's rhythm and FM key-control policy
on each note. The note grammar supplies the timbre and controller law. A missing
header field uses that grammar's default rather than another track's header.
Internally constructed untagged events retain their channel fallback.

Raw CC7 and CC11 values remain channel state. Each allocated FM/WT voice retains
its source controller law, so live CC7/11 and pan use the same law as note start.
MA7 CC1 updates only MA7-law voices; Mobile/HPS CC1 remains unapplied. Recorded
Audio retains its existing gain exclusions and uses its own source pan law.
Existing arithmetic order, explicit voice pan locks, gates, Hold, source
lifetimes and voice allocation are preserved. Bank/program/bend remain shared
channel operations; this change does not invent independent controller channels
or new instrument gains.

`tools/source-profile-regression.h`, included in `audio-regression`, checks
complete float waveforms and tails against the source without an inert track,
with both insertion orders, multiple rendering block sizes and seek. Fixtures
cover HPS melody/rhythm, Mobile/compressed Mobile/MA7, fallback/embedded FM and
WT, and live volume/expression/pan. Additional MA7-controller-only fixtures
check that Mobile intrinsic vibrato is preserved.

All fixtures are synthetic. Hardware timbre calibration, manufacturer ROM
levels and ambiguous cross-track legacy Mwa IDs remain separate work.

Local r81 validation: the original 16-case reproducer now has identical complete
waveforms and durations; 214 source-profile waveform/block/seek checks pass
under ASan/UBSan. A separate-channel oracle with duplicated controls checks
simultaneous Mobile/MA7 FM and WT output, including live MA7 CC1. Of 2,121
existing synthetic render assets, 2,089 WAVs are byte-identical to r80; all 32
changed assets contain mixed source formats (Mobile modulation and recorded
source-binding fixtures). Android debug/release builds pass for all three ABIs;
lint reports zero errors and four existing warnings.
