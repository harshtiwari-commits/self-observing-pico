# HYPOTHESES-STUDY.md — FROZEN pre-registration (S1 merged study)
**Frozen BEFORE the first cell run. Never adjusted afterward.**
Verdicts: confirmed / refuted / partial. Refutations are valid results, reported equally.

Units: 1 cycle = 8 ns @ 125 MHz; 1 latency count = 16 ns. n = 2000 events/cell
(p95 rests on ~100 samples: usable; p99 ~20 samples: indicative only — verdicts use
median and p95). "max" = max observed, never a bound.

Design: 13 cells. Factors: handler {s1 flash, s2 SRAM, s3 SDK-callback} ×
core-0 load {usb idle, floop flash-loop, rloop RAM-loop, thrash 64 KB cache sweep} ×
capture {off, 25 MS/s, 62.5 MS/s} — reduced fractional design (capture varied only with
core0 = usb; sdk/thrash only with capture = off).

| ID | Prediction (numeric, ns) | Refutation criterion |
|---|---|---|
| H1a | \|med(s2-usb-off) − med(s1-usb-off)\| ≤ 64. Warm 16 KB XIP cache hides placement. | \|Δ\| > 64 |
| H1b | p95(s1-thrash-off) − p95(s1-usb-off) ≥ 320. Cold cache forces flash line fills. | Δ < +320 |
| H1c | p95(s2-thrash-off) − p95(s2-usb-off) ≤ 128. SRAM handler + RAM vector = cache-independent. | Δ > +128 |
| H2 | med(s3-usb-off) − med(s1-usb-off) ∈ [+320, +3200]. SDK dispatch (scan + ack + indirect call, all flash code). | outside range |
| H6a | Capture load perturbs the flash handler: Δp95(62M vs off) ≥ 160 for s1 AND the SRAM handler's Δ ≤ half of it. (Observer effect of the analyzer; refutation = "analyzer load negligible at these rates", also reportable.) | both conditions fail |
| H6b | Co-core code placement matters for the flash handler: p95(s1-floop-off) − p95(s1-rloop-off) ≥ 96 (shared XIP cache + bus). | Δ < +96 |
| H6c | SRAM handler invariant across capture loads: spread of medians (off/25M/62M) ≤ 64. | spread > 64 |

Condition notes:
- "thrash" = core 0 continuously sweeping a 64 KB flash array — composite cold-cache +
  XIP-contention (pessimistic). Array address printed per run; must be < 0x20000000.
- capture-62M = capture SM at 62.5 MS/s with wrapped DMA ring = ~62.5 MB/s of bus writes
  ("the analyzer is on"). capture-25M = quarter of that.
- floop/rloop = identical busy-loop bodies, differing ONLY in placement.
- All comparisons share stimulus, timer, and handler body — one variable each.
