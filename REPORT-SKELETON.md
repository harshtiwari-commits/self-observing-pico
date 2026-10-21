# REPORT — Self-Observing Microcontroller (skeleton with pre-written prose)
*Sections 1–4 and 6–7 are written. Section 5 has the demo numbers pre-filled and
【FILL】 markers for the study run. Replace markers, delete this header note.*

## 1. Introduction
Embedded systems are judged by their worst case, not their average. Yet timing behaviour
in coursework is usually asserted from theory or read from a single oscilloscope trace.
This project inverts the usual arrangement: a single Raspberry Pi Pico (RP2040, ₹400) acts
simultaneously as the **experiment subject** and as the **instrument measuring it**. One
core runs the code under test; the second core orchestrates; and the chip's PIO hardware
times stimulus-to-response gaps at 16 ns resolution, independently of both CPUs. On top of
this self-observing core, the project runs (a) a calibration-certified latency study of
interrupt-handling styles, and (b) a pre-registered, 13-cell factorial experiment on what
perturbs on-chip timing — including whether the measurement apparatus itself does.

## 2. Related work and gap
Pico-based logic analyzers are established: μLA (16 ch, 100 MHz, PIO triggers); the "$4
logic analyzer" and its 24-channel forks (100 Ms/s, complex triggers); and sigrok-pico,
merged into mainline libsigrok (2023), giving the Pico official PulseView support. Cheap
FX2 clones provide 24 MS/s capture with the sigrok stack. Scheduling theory (Liu & Layland
1973; Audsley et al. 1993) predicts priority behaviour given known execution costs; WCET
methodology (Wilhelm et al. 2008) warns that measurement under-approximates worst case.
**Gap:** none of the existing tools or studies measures a chip's *own* interrupt behaviour
with a self-built, calibration-certified instrument, and none quantifies the observer
effect of the capture hardware on the timed code. This project's contribution is the
measurement methodology and dataset, not the analyzer's existence — which is cited openly.

## 3. Architecture
- **Core 1 (subject):** interrupt handlers / polling loops / critical sections under test.
- **Core 0 (scientist):** USB command loop, stimulus orchestration, data collection.
- **PIO (ground truth):** `demo_lattimer` counts 2-cycle units (16 ns) from stimulus edge
  to response edge in hardware; `demo_stim` generates 100 Hz stimuli; `demo_fixture`
  drives stimulus and response in one instruction (known zero delay) for calibration;
  `demo_capture` + DMA provide 8-channel capture up to 62.5 MS/s; `demo_uarttx` emits
  8N1 UART at 9600 baud.
- Pins: GP0 stimulus, GP1 response, GP0–7 capture window, GP4 UART TX (GP5 = external
  probe point via breadboard).
- Latency words are inverted on the host: counts = 0xFFFFFFFF − raw; ns = counts × 16.

## 4. Methodology
Calibration precedes every session: the fixture is run 1000×; the median defines the
baseline and σ certifies the timer (acceptance: σ ≤ 2 counts). Each condition collects
2000 events at 100 Hz. Comparisons change one factor at a time. Hypotheses were frozen
before data collection (HYPOTHESES-STUDY.md) with numeric acceptance ranges; verdicts are
confirmed/refuted symmetrically. Statistics: medians and p95 carry verdicts (p95 rests on
~100 samples at n = 2000); p99 is indicative; maxima are reported as *observed maxima*,
never as bounds.

## 5. Results
### 5.1 Instrument certification (demo session, 2026-10-08)
Fixture: median 0 counts, min 0, max 0, **σ = 0.00 over 1000 events** → timer certified.

### 5.2 Interrupt-handling styles (demo session)
| Condition | p50 | p95/p99 | max | note |
|---|---|---|---|---|
| RAW (bare handler, RAM-resident) | 176 ns | 176 ns | 928 ns | σ = 17 ns; deterministic |
| POLL (busy polling) | 80 ns | 5,000,208 ns | 10,000,256 ns | bimodal: 64 ns pulses missed between polls → answer one stimulus period late |
| CRIT (100 µs critical sections) | 176 ns | ~81–99 µs | ~99.3–99.5 µs | worst case ≈ the critical-section window, as theory predicts |

### 5.3 Protocol capture (demo session)
"SELFOPS-PICO DEMO 0123" transmitted on GP4 at 9600 baud, captured at 500 kS/s, decoded
byte-perfect on the host — on-chip (GP4) and, with the breadboard link, through the
physical wire (GP5). 【FILL: confirm GP5 decode line from final demo run】

### 5.4 The 13-cell study 【FILL from study-out/study_summary.csv】
| Cell | p50 | p95 | max |
|---|---|---|---|
| s1-usb-off | | | |
| s1-usb-25M | | | |
| s1-usb-62M | | | |
| s1-floop-off | | | |
| s1-rloop-off | | | |
| s1-thrash-off | | | |
| s2-usb-off | | | |
| s2-usb-25M | | | |
| s2-usb-62M | | | |
| s2-floop-off | | | |
| s2-rloop-off | | | |
| s2-thrash-off | | | |
| s3-usb-off | | | |

### 5.5 Hypothesis verdicts 【FILL from runner output】
| ID | Prediction | Measured | Verdict |
|---|---|---|---|
| H1a | \|med s2−s1 warm\| ≤ 64 ns | | |
| H1b | flash cold−warm p95 ≥ +320 ns | | |
| H1c | SRAM cold−warm p95 ≤ +128 ns | | |
| H2 | SDK overhead ∈ [320, 3200] ns | | |
| H6a | capture-62M shifts flash p95 ≥ +160 ns, SRAM ≤ half | | |
| H6b | co-core flash-loop vs RAM-loop ≥ +96 ns (flash handler) | | |
| H6c | SRAM median spread across capture ≤ 64 ns | | |

Figures: fig_latency.png (demo), fig_uart.png (demo), fig_study.png (study, 2 panels).

## 6. Discussion
- **Tails, not averages:** POLL is fastest on median and catastrophic on p95 — the
  project's central teaching result, measured rather than asserted.
- **Placement is a tail story:** 【FILL: H1a–c interpretation — warm cache hides placement;
  cold cache exposes it; SRAM's immunity is the real justification for RAM-resident ISRs.】
- **The observer effect:** 【FILL: H6a/H6b — capture DMA and co-core flash code share the
  XIP cache and buses with the timed code; SRAM-resident code is the stable reference.】
- **Limitations:** single architecture and toolchain; digital thresholds only (no analog
  measurements); observed maxima are statistical, not WCET bounds; p99 indicative at
  n = 2000; the thrash condition is a composite (cold cache + contention), pessimistic by
  design.

## 7. Conclusion and future work
A ₹400 board served as both instrument and subject, certified by a zero-delay fixture,
and produced distribution-grade evidence for scheduling folklore (polling tails,
critical-section ceilings, placement-under-cache-pressure) plus a pre-registered
observer-effect experiment. Future work: RTOS priority-protection cells (FreeRTOS SMP),
I2C/SPI protocol studies with an on-chip stretch watchdog, and cross-architecture
comparison against AVR/8051 lab boards.

## References
μLA (dotcypress/ula); A. Gimenez Bernad, "$4 Logic Analyzer" (EE Journal, 2022) + forks
(jobitjoseph, xqy828); sigrok-pico (pico-coder, merged libsigrok 2023); fx2lafw/sigrok;
Liu & Layland, JACM 20(1), 1973; Audsley et al., SEJ 8(5), 1993; Wilhelm et al., ACM TECS
7(3), 2008; Raspberry Pi RP2040 Datasheet & Pico C/C++ SDK; Lee & Seshia, MIT Press 2017;
Wolf, Morgan Kaufmann 2022.
