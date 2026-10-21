@echo off
title selfobs demo runner
cd /d "%~dp0"
if not exist .venv\Scripts\activate.bat (
  echo ERROR: .venv missing. Run these two commands once, then retry:
  echo   python -m venv .venv
  echo   .venv\Scripts\activate ^&^& pip install -r host\requirements.txt
  pause
  exit /b 1
)
call .venv\Scripts\activate.bat
python host\run_demo_auto.py
pause
