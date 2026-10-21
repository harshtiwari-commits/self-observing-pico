# RUN-S1 — setup, run, test, wiring (the complete checklist)

## A. WIRING — read first
**The 13-cell study needs NO wiring at all.** Every signal path is on-chip.
Leave the breadboard alone (or unplugged) for the study.

Only if you ALSO re-run the faculty demo (`demo.py`, UART beat): keep the existing wiring
- Pico physical pin 6 (GP4) → breadboard row 10
- resistor (any 1k–10k) between rows 10 and 15
- Pico physical pin 7 (GP5) → row 15
- Pico GND (pin 3) → ground rail
If it's still wired from before the break: touch nothing.

## B. SETUP (10 min)
1. From the workspace, download these 5 files into your local repo (overwrite):
   - `selfobs-demo/firmware/src/main.c`      → firmware v5-cells
   - `selfobs-demo/host/study_run.py`        → the 13-cell runner
   - `selfobs-demo/HYPOTHESES-STUDY.md`      → frozen predictions
   - `selfobs-demo/EXPLAIN-STUDY.md`         → your 10-min read
   - `selfobs-demo/RUN-S1.md`                → this file
2. Sanity-check the overwrite took (both must print lines):
   ```powershell
   Select-String -Path D:\projects\selfobs-demo\firmware\src\main.c -Pattern "v5-cells"
   Select-String -Path D:\projects\selfobs-demo\host\study_run.py -Pattern "13 cells"
   ```
3. Build:
   ```powershell
   cd D:\projects\selfobs-demo\firmware
   cmake --build build
   ```

## C. FLASH + VERIFY (3 min)
1. Hold BOOTSEL → plug in → release → drag `build\selfobs_demo.uf2` onto RPI-RP2
2. Watch the drive vanish (flash committed) → wait 5 s → replug normally
3. Find the COM number (may have changed):
   ```powershell
   python -c "import serial.tools.list_ports; [print(p.device) for p in serial.tools.list_ports.comports()]"
   ```

## D. RUN THE STUDY (~5 min)
```powershell
cd D:\projects\selfobs-demo
python host\study_run.py --port COM<your-number>
```
While it runs (~4.5 min): read EXPLAIN-STUDY.md. Do not unplug anything.

## E. TEST / ACCEPTANCE CHECKLIST
- [ ] `[1] connected: PONG selfobs-demo v5-cells`  (else: flash didn't take — redo C)
- [ ] `[2] calibration` sigma ≤ ~2
- [ ] 13 stat lines printed, each n=2000
- [ ] `[4] verdicts`: seven lines H1a/H1b/H1c/H2/H6a/H6b/H6c, each CONFIRMED or REFUTED
- [ ] `[5] figures`: `study-out\fig_study.png` exists; open it — two stacked plots
- [ ] `study-out\study_summary.csv` has 13 rows
- [ ] `STUDY COMPLETE.` printed

## F. OPTIONAL — prove one firmware serves both (2 min)
```powershell
python host\demo.py --port COM<your-number> --auto
```
All five demo beats still work on v5 (commands are backward compatible). With the wire
in place, beat 5 decodes through GP5.

## G. LOCK IT IN (3 min)
```powershell
cd D:\projects\selfobs-demo
git add -A
git commit -m "S1: 13-cell observer-effect study complete"
git tag s1-complete
```
Append a RUNLOG line with the date + the seven verdicts.

## H. TROUBLESHOOTING
| Symptom | Cause | Fix |
|---|---|---|
| PONG says v4/v3/v2 | flash didn't take | redo C, watch the drive vanish |
| `#ERR handler ...` | command parse | runner bug — paste output |
| a cell returns n=0 | mode switch timeout | paste the `#LAT` header line |
| H6a REFUTED with tiny deltas | genuinely no observer effect at these loads | that IS the result — report honestly |
| thrash FATAL message | array placement | paste output; I fix linker placement |
| COM error | wrong/stale port | re-run the port lister in C.3 |

## I. THEN
Paste me the FULL console output (all 13 lines + verdicts). I turn it into the report's
results chapter (step S2) the same day.
