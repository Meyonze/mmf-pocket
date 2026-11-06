# HandyPhone octave shift isolation (r82)

A HandyPhone MTR1 containing only a valid Octave Shift command could transpose
Mobile/MA7 notes on overlapping global channels 4..7. At 8 kHz the original
reproducer changed all 4,800 common float samples while keeping the 2,400-frame
output duration. The command took effect even though that HPS track had no note.
The same regression reproduces with a complete valid HPS terminator.

SMAF3.05 printed pp32–33 defines HPS Note Message as a note plus a two-bit octave
and uses Octave Shift to extend its four-octave encoding. Printed p39 defines
Mobile note numbers as 0..127, with 69=A440. Applying an HPS encoding extension
to those absolute notes changes their authored pitch.

The existing per-note `handyPhoneNote` tag now controls this final translation.
The command remains HPS channel state, including absolute/reset semantics;
only HPS notes consume it. This does not change pitch bend, bank/program,
voice assignment, controller gain, synthesis or the source waveforms.
The conversion cache changes to r82 so affected cached WAVs regenerate.

`tools/hps-shift-isolation-test.cpp` compares mixed files to the same modern
source without the HPS command track. Coverage includes compressed/ordinary
Mobile and MA7, channels 4..7, both track orders, commands before/between notes,
positive/negative shifts and reset, fallback/embedded FM, WT and recorded Audio,
8 kHz/44.1 kHz, seek, reinitialization and multiple block sizes. Route counters
and complete decoding are checked so a silent or rejected input cannot pass.
HPS-only equivalent scores independently move one octave between the note's
encoded octave and its shift command, preserving every supported shift.

The source-profile and tone-audit test fixtures also use complete four-byte
HPS terminators, and source-profile tests explicitly require complete decoding.
All test sources are synthetic; no manufacturer ROM or private music is used.

Local validation: the dedicated test fails against the r81 player and passes
509 checks with r82 under ASan/UBSan. All 2,121 existing synthetic WAVs are
byte-identical to r81. Android debug/release builds pass for all three ABIs;
lint reports zero errors and four existing warnings.
