# PRESENTER PITCH — final demo (~6 minutes, simple words)

## The one sentence (memorize cold)
> "One ₹400 board does two jobs at once — it is the experiment, and it is the instrument measuring it."

## OPENING (30 s, while connecting)
"Normally, measuring a microcontroller's speed needs an oscilloscope or logic analyzer —
expensive lab equipment. My project asks: can the board measure itself? One core runs the
code being tested; the second core plus special hardware called PIO do the measuring."
(Run: `python host\demo.py --port COM<x> --auto`)

## BEAT 1 — Connection (15 s) — point at PONG line
"My Python talks to the board over USB. It answered — the setup is alive."

## BEAT 2 — Calibration (45 s) — your credibility moment
"Before trusting an instrument, check it. The chip raises two pins at the same instant and
asks its hardware timer: how much time passed? Correct answer: zero. One thousand runs:
zero, zero, zero — sigma zero. Each tick is 16 nanoseconds. Now every number after this
is trustworthy."

## BEAT 3 — Three experiments (2 min) — the heart
"The chip sends itself a pulse — an interrupt — and code must answer on another pin. The
hardware timer measures the gap, two thousand times, three ways:"
- **RAW:** "A direct handler: 176 nanoseconds, almost every time. Very stable."
- **POLL:** "Polling is usually FASTER — 80 nanoseconds. But look at the worst cases: it
  sometimes misses the pulse entirely and answers 5 milliseconds late. Faster… until it
  isn't. Real-time systems fail on tails, not averages." *(your best story — land it)*
- **CRIT:** "I disable interrupts for 100 microseconds, like real critical sections. The
  worst case jumps to almost exactly 100 microseconds. Theory predicted it; hardware proves it."

## BEAT 4 — The graph (30 s) — open fig_latency.png
"Three ways to write the same code. Blue tight, green with a spike far right, red
stretched to 100 microseconds. This picture is the whole argument for measuring tails."

## BEAT 5 — The analyzer (45 s) — hold up the breadboard
"It also works as a logic analyzer. Text goes out this pin, through this wire and
resistor, back into this pin — captured and decoded letter by letter."
(Point at MATCH line; if it says GP5: "decoded through the physical wire.")

## CLOSING (30 s)
"One low-cost board, both roles, calibrated to zero error. Beyond the demo, the same
instrument ran a 13-cell designed experiment on what perturbs on-chip timing — including
whether the analyzer itself disturbs what it measures. Thank you."

## IF SOMETHING BREAKS
"That's embedded systems — here's last night's recording." (Play it. Never debug live.)

## LIKELY QUESTIONS
- **Why useful?** "Brake controllers, medical devices — they must meet deadlines; you must know the worst case."
- **What is PIO?** "Tiny programmable hardware units with exact timing, independent of the CPU — so the measurement never waits for software."
- **Why did polling miss?** "The pulse is 64 ns wide; if the loop is elsewhere in that instant, it waits a full period."
- **Is this new? Analyzers exist.** "Yes — cited in my report. New is the self-measurement: instrument and subject on one chip, calibrated, distributions not averages, plus the observer-effect experiment."
- **Is your max a guaranteed worst case?** "No — observed maximum. A guarantee needs formal analysis; that distinction is part of what I learned."

## DELIVERY RULES
Speak 20% slower than feels natural · point at numbers, never read code · one action + two
sentences per beat, then stop · hold up the breadboard once · rehearse twice out loud.
