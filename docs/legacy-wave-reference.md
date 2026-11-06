# Decoded wave ownership and legacy provenance

The public SMAF3.05 specification, printed p43, leaves assignment of score
notes/banks/programs to stream Wave Numbers implementation-dependent. The
bank0 VM35 WT-to-Mwa compatibility path is not independently established as a
manufacturer rule. Duplicate Mwa IDs across MTRs reproduce an order-dependent
legacy selection, but this alone does not establish a different correct mapping.

This change preserves that selection: the last decoded score MTR containing
an ID wins the compatibility lookup. Instrument RAM and its active inline
version retain priority; instrument-only and ROM addresses never borrow Mwa.
ATR/Awa remains separate. No musical gain, pitch, timing or waveform changes.

Previously each selected Mwa was copied into a global fallback sample bank as
well as moved into its owning MTR bank. PCM and silence-index storage were
duplicated. The fallback now stores only ID-to-MTR references. Source lookup
resolves into the immutable owning bank after construction; it does not store
pointers that vector growth could invalidate. Initialization clears all source
references. Recorded Audio and legacy WT can safely read the same const sample.

An independent allocation probe counts allocations exactly matching the decoded
payload size during initialization. For eight distinct 1 MiB PCM8 inputs, the
old renderer allocated 32 MiB of decoded PCM payload and the new renderer 16 MiB.
This measures those payload allocations, not total app memory or device power.
Source ownership, namespace precedence, vector growth through ID 255, track
order, duplicate MTR numbers, seek and reinitialization have dedicated native
tests. A separate one-MTR audio oracle checks the preserved compatibility output.

The scheduled tone inventory appends actual wave binding/source and legacy
candidate counts to detailed reports; its aggregate TSV stays unchanged.
These diagnostics expose an ambiguous source for investigation rather than
silently choosing a new manufacturer mapping. See [tone-dependency-probes.md](tone-dependency-probes.md).
The render cache remains r82 because rendered output is preserved.

Validation: 514 ownership/lifetime checks under ASan/UBSan; eight diagnostic
source/precedence cases; 11 CLI tests including original serialized multi-MTR
fixtures; and 2,121 full WAVs byte-identical to r82. Android debug/release
builds pass for all three ABIs, with lint 0 errors and four existing warnings.
