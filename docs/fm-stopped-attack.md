# Skip waveform work for stopped FM attacks

An operator with configured attack rate zero keeps an exactly zero envelope.
The renderer still advanced its clock and evaluated its waveform every sample.
Expensive sine/clipped-wave calculations were discarded after multiplying by
zero, even when other operators kept the same voice sounding.

The existing cached output guard now also records the stopped-attack condition
when base pitch is calculated. No envelope check is added to the normal
per-sample path. `env.advance()` and oscillator phase integration still run
before the guard, preserving bend, intrinsic vibrato, Hold and feedback clocks.
Modern EVB retains its actual-increment Nyquist path and existing arithmetic;
this optimization deliberately does not replace that path.

Only raw AR0 with its MA7 attack low bit clear qualifies. MA7 AR1 (AR0 with
low bit set) remains audible, including under key scaling. New configure/noteOn
and pitch changes refresh the cached state. No timbre, gain, note allocation,
gate timing or filter rule changes. The render cache stays r82.

`tools/fm-zero-envelope-test.cpp` checks 1024 original operator cases spanning
all 32 wave selectors, both renderer profiles, 8/44.1 kHz, KSR offsets, AM/EVB,
raw AR0/AR1, Hold/release, pitch excursions through Nyquist, oscillator-clock
agreement with an audible independent operator, and retrigger equivalence.
A separate before/after run transports 3,145,728 float samples from 3072 complete
voice cases: all 8 algorithms, all 32 wave selectors, both profiles, active and
stopped operator patterns, feedback, pitch, vibrato depth, volume, Hold/release
and retrigger. Those samples are byte-identical in the measured run.

All fixtures are synthetic. This is a software cost reduction; it does not
establish manufacturer FM aliasing behavior or measured phone power/latency.

The final host benchmark uses 32 voices, algorithms 4/7, sine/clipped-sine,
all-active and three-stopped-operator cases, with/without intrinsic LFO.
Five alternating baseline/candidate pairs run after other builds/renders finish.
For the no-EVB three-stopped-operator group, aggregate elapsed-time ratios are
0.26..0.42, median 0.36. Control measurements have substantial host-load scatter;
small differences in all-active/EVB cases are inconclusive and are not claimed
as speedups. The adopted change reuses the existing per-sample branch; the
initial general per-frame zero-gain check was not adopted.

All 2,121 existing synthetic WAVs remain byte-identical. Six focused ASan/UBSan
suites pass, including phase continuity, carrier bandlimiting, silent allocation
and the independent FM sideband oracle. Android debug/release builds pass for
all three ABIs; lint reports zero errors and four existing warnings.
