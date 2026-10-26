# Self-Observing Microcontroller

**A single Raspberry Pi Pico (RP2040) that is simultaneously the experiment and the
instrument measuring it.** One core runs the code under test, the second core orchestrates,
and the chip's PIO hardware times stimulus-to-response gaps at **16 ns resolution** —
independently of both CPUs. On this base the project runs a calibration-certified latency
study and a **pre-registered 13-cell factorial experiment** on what perturbs on-chip
interrupt timing, including whether the measurement apparatus itself does (observer effect).

```
stimulus (PIO, GP0) ──► core-1 code under test ──► response (GP1)
        │                                              │
        └──────────── PIO latency timer ◄──────────────┘
                   (16 ns/count, hardware)
```

---

## Contents
1. [Headline results](#headline-results)
2. [Novelty & honest positioning](#novelty--honest-positioning)
3. [Architecture](#architecture)
4. [Repository layout](#repository-layout)
5. [Hardware & wiring](#hardware--wiring)
6. [Software setup](#software-setup)
7. [Build & flash](#build--flash)
8. [Running the 13-cell study](#running-the-13-cell-study)
9. [Running the faculty demo](#running-the-faculty-demo)
10. [Handing the demo to a teammate](#handing-the-demo-to-a-teammate)
11. [Methodology](#methodology)
12. [Troubleshooting](#troubleshooting)
13. [Limitations](#limitations)
14. [References](#references)

---

## Headline results

Certification (1000 fixture events): median 0 counts, **σ = 0.00** — the timer reads a
known-zero delay as zero, every time.

| Condition | p50 | p95 | max | story |
|---|---|---|---|---|
| bare IRQ handler (warm cache) | 176 ns | 176 ns | 2.9 µs | deterministic |
| busy-polling | 80 ns | 5.0 ms | 10.0 ms | faster on median, catastrophic on tails (missed 64 ns pulses) |
| 100 µs critical sections | 176 ns | ~99 µs | ~99.5 µs | worst case = the critical-section window, as theory predicts |
| flash handler, cache evicted | 1552 ns | 1792 ns | 2.7 µs | XIP cold-cache cost, ~10× tail growth |
| SRAM handler, cache evicted | 272 ns | 560 ns | 976 ns | mitigated, not immune (entry trampoline still XIP) |
| SDK callback path | 1112 ns | 1136 ns | 24.6 µs | the price of convenience |


## Novelty & honest positioning

Pico-based logic analyzers are established prior art — μLA, the "$4 logic analyzer" and its
forks, and **sigrok-pico (merged into mainline libsigrok, 2023)**; cheap FX2 clones provide
24 MS/s capture. **This project does not claim the analyzer is new.** Its contributions are:

1. **Self-observation:** instrument and subject on one chip, with a hardware timer that
   neither CPU can perturb.
2. **Calibration discipline:** a zero-delay fixture certifies the timer before any
   measurement (σ = 0.00 acceptance gate).
3. **Distribution-based methodology:** pre-registered numeric hypotheses, symmetric
   confirmed/refuted verdicts, tails over averages.
4. **Observer-effect quantification:** capture load (up to 62.5 MS/s, ≈62 MB/s DMA) and
   co-core code placement shift p95 by ≤ 1 timer count — a *null result* that certifies the
   self-measurement method as non-invasive at these loads.
5. **Cache-pressure isolation:** only wholesale XIP eviction perturbs timing; the flash-vs-
   SRAM placement effect appears exclusively in tails under eviction.

## Architecture

| Component | Role |
|---|---|
| Core 1 | subject: IRQ handlers (flash / SRAM / SDK-callback), polling loops, critical sections |
| Core 0 | scientist: USB command loop, stimulus orchestration, load generators, data collection |
| `demo_lattimer` (PIO) | counts 2-cycle units (16 ns) from stimulus edge to response edge |
| `demo_stim` (PIO) | 100 Hz stimulus pulses, 64 ns active |
| `demo_fixture` (PIO) | drives GP0+GP1 in one instruction → known-zero delay for calibration |
| `demo_capture` + DMA | 8-channel capture, 500 kS/s stream or 25/62.5 MS/s burst into wrapped ring |
| `demo_uarttx` (PIO1) | 8N1 UART at 9600 baud for protocol-capture demonstration |

Pin map: GP0 stimulus · GP1 response · GP0–7 capture window · GP4 UART TX · GP5 external
probe point · GP2/3/6/7 quiet inputs (pull-down).

## Repository layout

```
firmware/            Pico firmware (C + PIO), v5-cells
  pio/demo.pio       five PIO programs (capture, stim, fixture, lattimer, uarttx)
  src/main.c         dual-core firmware: handlers, load generators, command protocol
host/
  study_run.py       13-cell experiment runner (verdicts, CSVs, figures)
  demo.py            faculty demonstration driver (5 beats)
  run_demo_auto.py   auto-COM-detect wrapper with logged output
RUN_DEMO.bat         one-click demo regeneration (Windows)
HYPOTHESES-STUDY.md  frozen pre-registration (never edited after a run)
REPORT-SKELETON.md   report with measured results and verdict tables
study-out/           experiment outputs (CSVs, fig_study.png)
```

## Hardware & wiring

| Item | Notes |
|---|---|
| Raspberry Pi Pico | any RP2040 board; ~₹400 |
| breadboard + 2 jumpers + 1 resistor (1k–10k) | only for the UART-capture demo beat |
| micro-USB data cable | power-only cables are a classic failure |

**Study runs need no wiring at all.** For the UART demo beat:

```
Pico pin 6 (GP4) ──jumper──► breadboard row 10
Pico pin 7 (GP5) ──jumper──► breadboard row 15
resistor: one leg row 10, other leg row 15
Pico pin 3 (GND) ──jumper──► breadboard side rail
```
(Pins counted down the left edge, USB pointing up.)

## Software setup

Toolchain: Arm GNU Embedded (`arm-none-eabi-gcc`), CMake ≥ 3.13, Ninja, Git, Python 3.
Raspberry Pi **Pico SDK** with submodules initialized. Prebuilt host tools **picotool** and
**pioasm** (from `pico-sdk-tools` releases) — the SDK otherwise tries to compile them and
fails without a host C compiler.

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r host/requirements.txt
git clone https://github.com/raspberrypi/pico-sdk D:\projects\pico-sdk
cd D:\projects\pico-sdk; git submodule update --init
```

## Build & flash

```powershell
cd firmware
cmake -B build -S . -GNinja -DPICO_SDK_PATH=<path-to-pico-sdk> `
      -Dpicotool_DIR=<path-to-picotool-folder> -Dpioasm_DIR=<path-to-pioasm-folder>
cmake --build build
```
Flash: hold **BOOTSEL**, plug in, release, drag `build/selfobs_demo.uf2` onto the `RPI-RP2`
drive; the drive auto-ejects when the flash commits. Firmware announces itself over USB:
`selfobs-demo v5-cells ready` / `PONG selfobs-demo v5-cells`.

## Running the 13-cell study

```powershell
python host/study_run.py --port COM<x>      # ~4.5 min, 2000 events/cell
```
Outputs: console tables, seven hypothesis verdicts against `study-out/study_events.csv`,
`study-out/study_summary.csv`, `study-out/fig_study.png`. Refutations are valid, reported
results — the frozen sheet is never adjusted after a run.

## Running the faculty demo

Manual:
```powershell
python host/demo.py --port COM<x> --auto
```
One-click (auto COM detect, full log to `demo-out/demo_log.txt`):
```
double-click RUN_DEMO.bat
```
Beats: PING → calibration (σ line) → raw/poll/crit latency tables + `fig_latency.png` →
UART text transmitted on GP4, captured and decoded (through the breadboard wire on GP5 when
present) + `fig_uart.png` → `DEMO COMPLETE`.

## Handing the demo to a teammate

1. Plug the Pico in **normally** (no BOOTSEL); verify the 4-wire breadboard link above.
2. Double-click `RUN_DEMO.bat`; do not unplug during the ~1 minute run.
3. Tick the checklist in the window: `Pico found on COM..` · calibration σ ≤ 2 · three
   tables · `decoded: 'SELFOPS-PICO DEMO 0123' -> MATCH` · `DEMO COMPLETE`.
   If it reports *"wire not detected"*, reseat the resistor and rerun.
4. Return `demo-out/fig_latency.png`, `demo-out/fig_uart.png`, `demo-out/demo_log.txt`.
Anything else failing: photograph the window; do not debug.

## Methodology

- **Calibration gate** before every session: fixture median defines the baseline; σ ≤ 2
  counts required, else numbers are flagged indicative.
- **One variable per comparison**; identical stimulus, timer, handler body.
- **Statistics:** medians and p95 carry verdicts (p95 ≈ 100 samples at n = 2000); p99
  indicative; maxima are *observed maxima*, never bounds (measurement under-approximates
  WCET — Wilhelm et al. 2008).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `No CMAKE_C_COMPILER could be found` | missing `-Dpicotool_DIR` / `-Dpioasm_DIR` (SDK building host tools) |
| `could not open port COM..` | wrong/stale port, or another terminal holds it; re-list ports |
| no `RPI-RP2` drive | power-only cable, or hold BOOTSEL *while* plugging |
| PONG says an old version | flash didn't commit — drag again, watch the drive vanish |
| `NO PICO DETECTED` (bat) | board unplugged or in BOOTSEL; plug in normally, retry |
| decode `no match` | resistor/wiring loose on rows 10/15; reseat, rerun |

## Limitations

Single architecture and toolchain; digital thresholds only (no analog measurement);
observed maxima are statistical, not WCET bounds; the thrash condition is a composite
(cold cache + bus contention), pessimistic by design; p99 indicative at n = 2000.

## References

μLA (dotcypress/ula) · A. Gimenez Bernad, "$4 Logic Analyzer", EE Journal 2022 (+ forks
jobitjoseph, xqy828) · sigrok-pico (pico-coder; mainline libsigrok 2023) · fx2lafw/sigrok ·
Liu & Layland, JACM 20(1), 1973 · Audsley et al., SEJ 8(5), 1993 · Wilhelm et al., ACM TECS
7(3), 2008 · Raspberry Pi RP2040 Datasheet & Pico C/C++ SDK · Lee & Seshia, MIT Press 2017 ·
Wolf, Morgan Kaufmann 2022.
