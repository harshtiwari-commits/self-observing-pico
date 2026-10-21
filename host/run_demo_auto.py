#!/usr/bin/env python3
"""Foolproof demo runner: auto-detects the Pico's COM port, runs the full 5-beat
demo, saves fig_latency.png + fig_uart.png + demo_log.txt.
Launch via RUN_DEMO.bat (double-click) or: python host\\run_demo_auto.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import serial.tools.list_ports
import demo

LOG = os.path.join("demo-out", "demo_log.txt")


class Tee:
    def __init__(self, *files):
        self.files = files

    def write(self, s):
        for f in self.files:
            f.write(s)

    def flush(self):
        for f in self.files:
            f.flush()


def main():
    os.makedirs("demo-out", exist_ok=True)
    ports = [p for p in serial.tools.list_ports.comports()
             if p.vid == 0x2E8A or "USB Serial" in (p.description or "")]
    if not ports:
        print("NO PICO DETECTED.")
        print("Plug the Pico in with the USB cable (NORMAL boot - do NOT hold")
        print("BOOTSEL), wait 5 seconds, then run this again.")
        input("Press Enter to exit...")
        sys.exit(1)
    port = ports[0].device
    print("Pico found on", port)

    log = open(LOG, "w", encoding="utf-8")
    old = sys.stdout
    sys.stdout = Tee(old, log)
    try:
        ser = demo.connect(port)
        demo.run_auto(ser, "demo-out")
    finally:
        sys.stdout = old
        log.close()

    print("\nLog saved to:", os.path.abspath(LOG))
    print("Figures: demo-out\\fig_latency.png  and  demo-out\\fig_uart.png")
    input("Press Enter to close this window...")


if __name__ == "__main__":
    main()
