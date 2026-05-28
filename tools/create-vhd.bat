@echo off
:: create-vhd.bat — Create a fixed-size VHD from a raw disk image
:: Usage: create-vhd.bat <input.bin> <output.vhd> [size_mb]

setlocal

cd /d "%~dp0"

where pwsh >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell 7 ^(pwsh^) is not installed or not on PATH.
    echo Install it from: https://aka.ms/powershell
    exit /b 1
)

if "%~1"=="" (
    echo Usage: create-vhd.bat ^<input.bin^> ^<output.vhd^> [size_mb]
    exit /b 1
)
if "%~2"=="" (
    echo Usage: create-vhd.bat ^<input.bin^> ^<output.vhd^> [size_mb]
    exit /b 1
)

if "%~3"=="" (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "create-vhd.ps1" -InputPath "%~1" -OutputPath "%~2"
) else (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "create-vhd.ps1" -InputPath "%~1" -OutputPath "%~2" -SizeMB %~3
)
