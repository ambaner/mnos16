@echo off
:: Read serial debug output from a MNOS16 Hyper-V VM.
:: Starts the VM, connects to COM1 pipe, and auto-reconnects on reboot/reset.
:: Requires admin privileges (to start/stop the VM).
::
:: Usage:  read-serial.bat              (defaults: VM=MNOS16)
::         read-serial.bat my-vm        (custom VM name)
if "%1"=="" (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\read-serial.ps1"
) else (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\read-serial.ps1" -VMName %1
)
