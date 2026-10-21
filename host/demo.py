#!/usr/bin/env python3
"""selfobs-demo host script — the faculty demonstration driver (final, all fixes).

Usage (venv with pyserial+numpy+matplotlib, from the selfobs-demo folder):
    python host\\demo.py --port COM7 --auto        # full scripted demo
    python host\\demo.py --port COM7               # interactive menu

Beats: PING -> FIXTURE calibration -> LATRUN raw/poll/crit -> fig_latency.png
       -> UARTCAP through the breadboard wire (GP5 first, GP4 fallback) -> fig_uart.png
"""
import argparse
import os
import sys
import time

import numpy as np
import serial

BAUD = 115200
NS_PER_COUNT = 16
CAP_RATE = 500_000
UART_BAUD = 9600
HEXDIGITS = set("0123456789abcdefABCDEF")


# ---------------- serial helpers ----------------
def connect(port):
    ser = serial.Serial(port, BAUD, timeout=2)
    time.sleep(1.5)
    ser.reset_input_buffer()
    ser.write(b"PING\n")
    line = ser.readline().decode(errors="replace").strip()
    if "PONG" not in line:
        print("WARNING: unexpected first reply:", repr(line))
    else:
        print("[1] connected:", line)
    return ser


def read_words(ser, timeout_s=120.0):
    header, words = "", []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = ser.readline().decode(errors="replace").strip()
        if not raw:
            continue
        if raw.startswith("#END"):
            return header, words
        if raw.startswith("#ERR"):
            raise RuntimeError("device: " + raw)
        if raw.startswith("#"):
            header = raw
            continue
        if len(raw) == 8 and set(raw) <= HEXDIGITS:
            words.append(int(raw, 16))
    raise TimeoutError("no #END within %.0f s" % timeout_s)


def fixture(ser, n=1000):
    ser.write(("FIXTURE %d\n" % n).encode())
    header, words = read_words(ser)
    counts = 0xFFFFFFFF - np.array(words, dtype=np.uint64)
    return np.asarray(counts, dtype=np.float64)


def latrun(ser, mode, n=2000):
    ser.write(("LATRUN %s %d\n" % (mode, n)).encode())
    header, words = read_words(ser)
    if not words:
        raise RuntimeError("no events captured (%s)" % header)
    counts = 0xFFFFFFFF - np.array(words, dtype=np.uint64)
    return np.asarray(counts, dtype=np.float64)


def uartcap(ser, text="SELFOPS-PICO DEMO 0123"):
    ser.write(("UARTCAP " + text + "\n").encode())
    header, words = read_words(ser)
    return words, header


# ---------------- stats / plots ----------------
def stats_ns(counts, baseline_counts):
    ns = np.maximum((counts - baseline_counts) * NS_PER_COUNT, 0)
    p = np.percentile
    return {
        "n": len(ns),
        "min": ns.min(), "mean": ns.mean(),
        "p50": p(ns, 50), "p95": p(ns, 95), "p99": p(ns, 99),
        "max": ns.max(), "sigma": ns.std(),
    }


def print_table(name, s):
    print("  %-6s n=%5d  min=%7.0f  mean=%7.0f  p50=%7.0f  p95=%7.0f  p99=%7.0f  max=%9.0f  sigma=%7.0f  (ns)"
          % (name, s["n"], s["min"], s["mean"], s["p50"], s["p95"], s["p99"], s["max"], s["sigma"]))


def plot_latency(results, baseline, outdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(10, 5.5))
    colors = {"raw": "#1f77b4", "poll": "#2ca02c", "crit": "#d62728"}
    for mode, counts in results.items():
        ns = np.maximum((counts - baseline) * NS_PER_COUNT, 0)
        ax.hist(ns, bins=80, histtype="step", linewidth=2,
                color=colors.get(mode, None),
                label="%s (p50=%.0f ns, p99=%.0f ns, max=%.0f ns)"
                      % (mode.upper(), np.percentile(ns, 50),
                         np.percentile(ns, 99), ns.max()))
    ax.set_xlabel("stimulus-to-response latency (ns), fixture baseline subtracted")
    ax.set_ylabel("events")
    ax.set_yscale("log")
    ax.set_title("Self-observed interrupt latency — one Pico is both instrument and subject")
    ax.legend()
    ax.grid(alpha=.3)
    path = os.path.join(outdir, "fig_latency.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print("saved", path)


# ---------------- UART decode ----------------
def unpack_samples(words, lsb_first=True):
    samples = []
    for w in words:
        b = [w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF]
        samples.extend(b if lsb_first else b[::-1])
    return samples


def decode_uart(samples, lane, rate=CAP_RATE, baud=UART_BAUD):
    bp = rate / float(baud)
    bits = [(s >> lane) & 1 for s in samples]
    out, i, n = [], 0, len(bits)
    while i < n - 1:
        if bits[i] == 1 and bits[i + 1] == 0:          # falling edge = start
            start = i + 1
            stop_i = start + int(round(bp * 9.5))
            if stop_i >= n:
                break
            byte, ok = 0, True
            for k in range(8):
                idx = start + int(round(bp * (1.5 + k)))
                if idx >= n:
                    ok = False
                    break
                byte |= bits[idx] << k                 # LSB first
            if ok and bits[stop_i] == 1:
                out.append(chr(byte))
                i = stop_i
                continue
        i += 1
    return "".join(out)


def plot_uart(words, decoded, outdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    bits = None
    first = None
    for order in (True, False):
        samples = unpack_samples(words, order)
        b = [(s >> 4) & 1 for s in samples]            # lane 4 = GP4 (UART TX)
        f = None
        for i in range(len(b) - 1):
            if b[i] == 1 and b[i + 1] == 0:
                f = i
                break
        if f is not None:
            bits, first = b, f
            break
    if first is None:
        print("no UART start bit found — check GP4->GP5 wiring")
        return

    bp = CAP_RATE / float(UART_BAUD)
    lo = max(first - int(bp), 0)
    hi = min(first + int(bp * 26), len(bits))
    seg = bits[lo:hi]
    fig, ax = plt.subplots(figsize=(10, 3.5))
    ax.step(range(len(seg)), seg, where="post", color="#1f77b4", linewidth=2)
    for k in range(0, len(seg), int(bp)):
        ax.axvline(k, color="gray", alpha=.25, lw=.5)
    ax.set_yticks([0, 1]); ax.set_ylim(-.2, 1.4)
    ax.set_xlabel("sample index (500 kS/s; 1 bit = %.1f samples)" % bp)
    ax.set_title("Logic-analyzer capture of PIO-UART on GP4 (wire through breadboard) — decoded: %r"
                 % decoded[:12])
    path = os.path.join(outdir, "fig_uart.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print("saved", path)


# ---------------- the demo ----------------
def run_auto(ser, outdir):
    print("\n[2] CALIBRATION — PIO fixture drives GP0+GP1 together")
    fx = fixture(ser, 1000)
    baseline = float(np.median(fx))
    print("     counts: median=%.1f  min=%d  max=%d  sigma=%.2f  ->  %.0f ns baseline, 16 ns/count"
          % (baseline, fx.min(), fx.max(), fx.std(), baseline * NS_PER_COUNT))
    if fx.std() > 2.0:
        print("     WARNING: fixture not near-constant — treat numbers as indicative")

    results = {}
    for mode, blurb in (("raw",  "bare interrupt handler (RAM-resident)"),
                        ("poll", "busy-polling the stimulus pin"),
                        ("crit", "interrupt under 100 us critical sections")):
        print("\n[3.%s] LATRUN %s — %s" % (mode.upper(), mode, blurb))
        results[mode] = latrun(ser, mode, 2000)
        print_table(mode, stats_ns(results[mode], baseline))

    plot_latency(results, baseline, outdir)

    print("\n[4] LOGIC-ANALYZER MODE — UART capture through the breadboard wire")
    text = "SELFOPS-PICO DEMO 0123"
    words, hdr = uartcap(ser, text)
    # try GP5 FIRST: it only carries a signal if the breadboard wire is in place
    # (GP4 is the driving pin itself and always decodes — that's the on-chip path)
    decoded = decode_uart(unpack_samples(words, True), lane=5)
    if decoded.startswith("SELF"):
        print("     (decoded from GP5 - through the breadboard wire!)")
    else:
        decoded = decode_uart(unpack_samples(words, True), lane=4)
        print("     (decoded on-chip from GP4 - breadboard wire not detected)")
    if decoded.startswith("SELF"):
        print("     sent:    %r" % text)
        print("     decoded: %r  -> MATCH" % decoded)
        plot_uart(words, decoded, outdir)
    else:
        print("     decoded: %r (no match)" % decoded)
        print("     -> if empty: check the GP4 -> resistor -> GP5 breadboard wiring")
        plot_uart(words, decoded, outdir)

    print("\nDEMO COMPLETE. Figures in", outdir)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--outdir", default="demo-out")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    ser = connect(args.port)

    if args.auto:
        run_auto(ser, args.outdir)
        return

    while True:
        print("\nmenu: fix (calibrate) | raw | poll | crit | uart <text> | quit")
        choice = input("> ").strip().lower()
        if choice == "quit":
            break
        elif choice == "fix":
            fx = fixture(ser, 1000)
            print("median=%.1f sigma=%.2f min=%d max=%d"
                  % (np.median(fx), fx.std(), fx.min(), fx.max()))
        elif choice in ("raw", "poll", "crit"):
            counts = latrun(ser, choice, 2000)
            print_table(choice, stats_ns(counts, 0))
        elif choice.startswith("uart"):
            text = choice[5:].strip() or "SELFOPS-PICO DEMO 0123"
            words, _ = uartcap(ser, text)
            print("decoded:", repr(decode_uart(unpack_samples(words, True), lane=4)))
        else:
            print("unknown")


if __name__ == "__main__":
    main()
