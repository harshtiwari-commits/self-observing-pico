#!/usr/bin/env python3
"""S1 study runner — 13-cell merged experiment (handler x core0 x capture).

Usage (venv active, from selfobs-demo folder, Pico flashed with v5-cells):
    python host\\study_run.py --port COM5
    python host\\study_run.py --port COM5 --n 3000

Outputs: study-out/study_events.csv, study-out/study_summary.csv,
         study-out/fig_study.png  + console tables + auto-verdicts.
Runtime ~5 min at --n 2000.
"""
import argparse
import os
import time

import numpy as np
import serial

BAUD = 115200
NS_PER_COUNT = 16
HEXDIGITS = set("0123456789abcdefABCDEF")

CELLS = [
    ("s1", "usb",    "off"), ("s1", "usb",   "25M"), ("s1", "usb",   "62M"),
    ("s1", "floop",  "off"), ("s1", "rloop", "off"), ("s1", "thrash", "off"),
    ("s2", "usb",    "off"), ("s2", "usb",   "25M"), ("s2", "usb",   "62M"),
    ("s2", "floop",  "off"), ("s2", "rloop", "off"), ("s2", "thrash", "off"),
    ("s3", "usb",    "off"),
]

# frozen ranges from HYPOTHESES-STUDY.md (ns)
H1A_MAX = 64
H1B_MIN = 320
H1C_MAX = 128
H2_LO, H2_HI = 320, 3200
H6A_MIN = 160
H6B_MIN = 96
H6C_MAX = 64


def connect(port):
    ser = serial.Serial(port, BAUD, timeout=2)
    time.sleep(1.5)
    ser.reset_input_buffer()
    ser.write(b"PING\n")
    line = ser.readline().decode(errors="replace").strip()
    print("[1] connected:", line)
    if "v5-cells" not in line:
        print("    WARNING: expected 'PONG selfobs-demo v5-cells' — flash v5 firmware first!")
    return ser


def read_words(ser, timeout_s=180.0):
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


def latrun_cell(ser, h, c0, cap, n):
    ser.write(("LATRUN %s %s %s %d\n" % (h, c0, cap, n)).encode())
    header, words = read_words(ser)
    if not words:
        raise RuntimeError("no events for cell %s/%s/%s (%s)" % (h, c0, cap, header))
    thrasharr = None
    for tok in header.split():
        if tok.startswith("thrasharr="):
            thrasharr = int(tok.split("=")[1], 16)
    counts = 0xFFFFFFFF - np.array(words, dtype=np.uint64)
    return np.asarray(counts, dtype=np.float64), thrasharr


def stats(ns):
    p = np.percentile
    return dict(n=len(ns), min=ns.min(), mean=ns.mean(), p50=p(ns, 50),
                p95=p(ns, 95), p99=p(ns, 99), max=ns.max(), sigma=ns.std())


def stat_line(key, s):
    return ("  %-16s n=%5d min=%6.0f mean=%7.0f p50=%6.0f p95=%7.0f p99=%8.0f "
            "max=%9.0f sigma=%7.0f" % (key, s["n"], s["min"], s["mean"], s["p50"],
                                       s["p95"], s["p99"], s["max"], s["sigma"]))


def verdict(tag, ok, detail):
    print("  %-4s %-9s %s" % (tag, "CONFIRMED" if ok else "REFUTED", detail))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--n", type=int, default=2000)
    ap.add_argument("--outdir", default="study-out")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    ser = connect(args.port)

    print("\n[2] calibration fixture")
    ser.write(b"FIXTURE 1000\n")
    hdr, fx = read_words(ser)
    fxc = 0xFFFFFFFF - np.array(fx, dtype=np.uint64)
    baseline = float(np.median(fxc))
    print("    baseline median=%.1f counts, sigma=%.2f" % (baseline, fxc.std()))

    print("\n[3] 13 cells x %d events (~%d s total)" % (args.n, 20 * len(CELLS)))
    data, summary_rows, events_rows = {}, [], []
    for (h, c0, cap) in CELLS:
        counts, thrasharr = latrun_cell(ser, h, c0, cap, args.n)
        if c0 == "thrash" and thrasharr is not None and thrasharr >= 0x20000000:
            print("    FATAL: thrash array not in flash (0x%08X)" % thrasharr)
            return
        ns = np.maximum((counts - baseline) * NS_PER_COUNT, 0)
        key = "%s-%s-%s" % (h, c0, cap)
        data[key] = ns
        s = stats(ns)
        summary_rows.append((key, s))
        events_rows.extend((key, i, int(v)) for i, v in enumerate(ns))
        print(stat_line(key, s) + "  (ns)")

    with open(os.path.join(args.outdir, "study_events.csv"), "w") as f:
        f.write("cell,event,ns\n")
        for r in events_rows:
            f.write("%s,%d,%d\n" % r)
    with open(os.path.join(args.outdir, "study_summary.csv"), "w") as f:
        f.write("cell,n,min,mean,p50,p95,p99,max,sigma\n")
        for key, s in summary_rows:
            f.write("%s,%d,%.0f,%.1f,%.0f,%.0f,%.0f,%.0f,%.1f\n"
                    % (key, s["n"], s["min"], s["mean"], s["p50"], s["p95"],
                       s["p99"], s["max"], s["sigma"]))
    print("\nsaved CSVs to", args.outdir)

    print("\n[4] verdicts vs HYPOTHESES-STUDY.md (frozen before this run)")
    med = lambda k: float(np.median(data[k]))
    p95 = lambda k: float(np.percentile(data[k], 95))

    d = med("s2-usb-off") - med("s1-usb-off")
    verdict("H1a", abs(d) <= H1A_MAX,
            "med(s2)-med(s1) warm = %+.0f ns (|limit| %d)" % (d, H1A_MAX))

    d = p95("s1-thrash-off") - p95("s1-usb-off")
    verdict("H1b", d >= H1B_MIN,
            "p95 flash-handler cold - warm = %+.0f ns (need >= %d)" % (d, H1B_MIN))

    d = p95("s2-thrash-off") - p95("s2-usb-off")
    verdict("H1c", d <= H1C_MAX,
            "p95 SRAM-handler cold - warm = %+.0f ns (limit %d)" % (d, H1C_MAX))

    d = med("s3-usb-off") - med("s1-usb-off")
    verdict("H2", H2_LO <= d <= H2_HI,
            "med(SDK cb) - med(flash raw) = %+.0f ns (expected %d..%d)" % (d, H2_LO, H2_HI))

    d1 = p95("s1-usb-62M") - p95("s1-usb-off")
    d2 = p95("s2-usb-62M") - p95("s2-usb-off")
    verdict("H6a", (d1 >= H6A_MIN) and (d2 <= max(d1 / 2.0, 0)),
            "capture-62M p95 shift: flash %+.0f ns vs SRAM %+.0f ns — observer effect of the analyzer" % (d1, d2))

    d = p95("s1-floop-off") - p95("s1-rloop-off")
    verdict("H6b", d >= H6B_MIN,
            "p95 flash-handler: co-core flash-loop vs RAM-loop = %+.0f ns (need >= %d)" % (d, H6B_MIN))

    spread = max(med("s2-usb-off"), med("s2-usb-25M"), med("s2-usb-62M")) - \
             min(med("s2-usb-off"), med("s2-usb-25M"), med("s2-usb-62M"))
    verdict("H6c", spread <= H6C_MAX,
            "SRAM-handler median spread across capture loads = %.0f ns (limit %d)" % (spread, H6C_MAX))

    print("\n[5] figures")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (axA, axB) = plt.subplots(2, 1, figsize=(10, 9))
    pa = ["s1-usb-off", "s1-usb-25M", "s1-usb-62M",
          "s2-usb-off", "s2-usb-25M", "s2-usb-62M"]
    ca = {"s1-usb-off": "#ff7f0e", "s1-usb-25M": "#d62728", "s1-usb-62M": "#8b0000",
          "s2-usb-off": "#1f77b4", "s2-usb-25M": "#2ca02c", "s2-usb-62M": "#006400"}
    for k in pa:
        axA.hist(data[k], bins=100, histtype="step", lw=2, color=ca[k], label=k)
    axA.set_title("Observer effect: capture load (analyzer OFF/25/62.5 MS/s) — flash vs SRAM handler")
    axA.set_xlabel("latency (ns)"); axA.set_yscale("log"); axA.legend(fontsize=7); axA.grid(alpha=.3)

    pb = ["s1-usb-off", "s1-floop-off", "s1-rloop-off", "s1-thrash-off",
          "s2-usb-off", "s2-floop-off", "s2-rloop-off", "s2-thrash-off", "s3-usb-off"]
    cb = ["#ff7f0e", "#d62728", "#e377c2", "#8b0000",
          "#1f77b4", "#2ca02c", "#17becf", "#006400", "#7f7f7f"]
    for k, c in zip(pb, cb):
        axB.hist(data[k], bins=100, histtype="step", lw=2, color=c, label=k)
    axB.set_title("Co-core load & handler path (capture OFF)")
    axB.set_xlabel("latency (ns)"); axB.set_yscale("log"); axB.legend(fontsize=7); axB.grid(alpha=.3)
    path = os.path.join(args.outdir, "fig_study.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print("saved", path)

    print("\nSTUDY COMPLETE.")


if __name__ == "__main__":
    main()
