@echo off
REM SPDX-License-Identifier: MIT
REM patch_renesas_core.windows.bat — Upgrade Arduino Renesas core (Uno R4) to TinyUSB 0.21.0
REM Double-click wrapper around patch_renesas_core.windows.ps1 (stock Windows
REM PowerShell 5.1 only — no Python, no Git, no patch.exe required).
REM Usage: patch_renesas_core.windows.bat             (upgrade)
REM        patch_renesas_core.windows.bat -Restore    (restore backed-up core files)
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_renesas_core.windows.ps1" %*
pause
