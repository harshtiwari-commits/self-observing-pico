# EXPLAIN-STUDY — the 13-cell experiment in plain language (~10 min read, say it once out loud)

## The question

Last session we asked: *does it matter where interrupt code lives?* This session asks a
bigger, more interesting question:

> **When a chip measures itself, what else in the chip secretly changes the answer?**

Three suspects:
1. **Where the measured code lives** — flash or SRAM (the cache question)
2. **What the OTHER core is doing** — sitting idle, running code from flash, running the
   same code from RAM, or sweeping memory to wipe the cache
3. **Whether the analyzer itself is switched on** — the capture hardware writing tens of
   megabytes per second to memory while the measurement happens

That third suspect is the project's headline: the **observer effect**. In physics, the
act of measuring can disturb the thing measured. Here: does running our logic-analyzer
hardware slow down the very interrupts it times? Almost nobody measures this. We do.

## The three "knobs" (factors)

| Knob | Settings | What it represents in real systems |
|---|---|---|
| handler | s1 flash / s2 SRAM / s3 SDK-callback | where you put your ISR code |
| core-0 load | usb idle / floop / rloop / thrash | what background software is doing |
| capture | off / 25 MS/s / 62.5 MS/s | is the measurement hardware running |

13 combinations ("cells"), 2000 events each. Everything else — stimulus, timer, handler
body — identical, so each comparison isolates one knob.

## Why 13 and not 36?

The full grid would be 3×4×3 = 36 cells. We use a **fractional design**: the capture knob
is only turned with an idle core 0 (that's where the observer effect is cleanest), and the
expensive knobs (SDK path, cache sweep) are only tested with capture off. Every hypothesis
still has the cells it needs; we just don't measure combinations that would only repeat
information. This is standard experimental-design practice — say that word at the viva:
*fractional factorial*.

## What each hypothesis says, in words

- **H1a** — With a warm cache, flash and SRAM handlers are practically the same speed.
- **H1b** — But wipe the cache (thrash) and the flash handler's *tail* grows a lot.
- **H1c** — The SRAM handler barely notices the wiping. (That's the actual justification
  for "put handlers in RAM": not average speed — *tail robustness under memory pressure*.)
- **H2** — The SDK's convenient callback path costs a few hundred ns to a few µs.
- **H6a** — Switching the analyzer on (62.5 MS/s capture) inflates the flash handler's
  tail; the SRAM handler's tail stays put. **= the observer effect, measured.**
- **H6b** — Even with the analyzer off, what the *other core* runs (flash loop vs RAM
  loop) shifts the flash handler's tail — shared cache, shared bus.
- **H6c** — The SRAM handler's median is stable across analyzer loads — it's the
  reference point that makes the observer effect visible by contrast.

If any verdict prints REFUTED, that is a finding too (e.g. "the analyzer does not
perturb at these rates" is publishable honesty).

## Viva questions, in your own words

1. **"What is the observer effect here?"**
   "The capture hardware streams samples to memory through DMA at up to 62 MB/s. That
   traffic shares the chip's buses and the flash cache with the code I'm timing. H6a
   measures exactly how much the timing shifts when I switch the analyzer on."

2. **"Why would SRAM code be unaffected by cache wiping?"**
   "It never uses the cache — its instructions are already in internal RAM, and the
   interrupt vector is in the RAM vector table. Wiping the flash cache can't evict what
   was never there."

3. **"Why do you trust a 13-cell design?"**
   "One variable changes per comparison, predictions were frozen before the run, the
   timer was calibration-certified (sigma zero), and each cell has 2000 events."

4. **"Isn't this just benchmarks?"**
   "Benchmarks report averages of one configuration. This is a designed experiment:
   factors, controlled comparisons, pre-registered numeric predictions, and refutations
   counted as results."

5. **"What would change your conclusions?"**
   "Different clock settings, a different chip revision, or much longer runs could shift
   magnitudes. The *comparisons* are the claim, not the absolute numbers — and the CSVs
   make every number reproducible."

## Files
- `firmware/src/main.c` (v5-cells) — implements all three knobs
- `host/study_run.py` — runs the 13 cells, prints verdicts, saves CSVs + figures
- `HYPOTHESES-STUDY.md` — frozen predictions (never edited after a run)
- outputs in `study-out/`
