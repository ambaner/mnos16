@echo off
:: setup-vm.bat — Create or update a Hyper-V VM for MNOS16
:: Must be run as Administrator.

setlocal

:: Ensure we're running from the repo root
cd /d "%~dp0"

:: Check for PowerShell 7
where pwsh >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell 7 ^(pwsh^) is not installed or not on PATH.
    echo Install it from: https://aka.ms/powershell
    exit /b 1
)

pwsh -NoProfile -ExecutionPolicy Bypass -File "tools\setup-vm.ps1"
