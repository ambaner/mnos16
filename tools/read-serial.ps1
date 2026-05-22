<#
.SYNOPSIS
    Read serial debug output from a MNOS16 Hyper-V VM.

.DESCRIPTION
    Starts (or restarts) the VM, immediately connects to the COM1 named pipe,
    and streams debug output to the console.  This ensures boot messages are
    captured from the very first byte.

    On disconnect (VM reboot, reset, or shutdown), the script automatically
    reconnects and resumes reading — no manual intervention needed.

    Press Ctrl+C to stop.

.PARAMETER VMName
    Hyper-V VM name (default: MNOS16).

.PARAMETER PipeName
    Named pipe to connect to (default: MNOS16-SERIAL).

.EXAMPLE
    .\read-serial.ps1                    # uses defaults
    .\read-serial.ps1 -VMName my-vm      # custom VM name

.NOTES
    Requires Hyper-V PowerShell module and admin privileges (to start the VM).
    Run this INSTEAD of Start-VM — the script manages the VM lifecycle.
#>
#Requires -Version 7.0
param(
    [string]$VMName  = 'MNOS16',
    [string]$PipeName = 'MNOS16-SERIAL'
)

function Write-Serial([string]$msg) {
    Write-Host "[read-serial] $msg" -ForegroundColor Cyan
}

function Start-AndConnect {
    # --- Ensure VM is stopped so we can catch boot from the start -----------
    $vm = Get-VM -Name $VMName -ErrorAction SilentlyContinue
    if (-not $vm) {
        Write-Host "[read-serial] ERROR: VM '$VMName' not found. Run setup-vm.bat first." -ForegroundColor Red
        return $false
    }

    if ($vm.State -ne 'Off') {
        Write-Serial "Stopping VM '$VMName'..."
        Stop-VM -Name $VMName -TurnOff -Force
        while ((Get-VM -Name $VMName).State -ne 'Off') {
            Start-Sleep -Milliseconds 200
        }
    }

    # --- Start the VM and immediately connect to the pipe -------------------
    Write-Serial "Starting VM '$VMName'..."
    Start-VM -Name $VMName

    Write-Serial "Connecting to \\.\pipe\$PipeName ..."
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        '.', $PipeName, [System.IO.Pipes.PipeDirection]::In)

    try {
        $pipe.Connect(10000)    # 10-second timeout
    }
    catch [TimeoutException] {
        Write-Host "[read-serial] ERROR: Pipe not available — is COM1 configured?" -ForegroundColor Red
        Write-Host "  Run setup-vm.bat to configure COM1." -ForegroundColor Yellow
        $pipe.Dispose()
        return $false
    }

    Write-Serial "Connected — reading serial output..."
    Write-Host ''

    try {
        $reader = [System.IO.StreamReader]::new($pipe)
        while (-not $reader.EndOfStream) {
            $line = $reader.ReadLine()
            if ($null -ne $line) {
                Write-Host $line
            }
        }
    }
    finally {
        if ($reader) { $reader.Dispose() }
        $pipe.Dispose()
    }

    return $true   # Disconnected (VM rebooted/reset/stopped)
}

# --- Main loop: auto-reconnect on disconnect --------------------------------
Write-Serial "=== MNOS16 Serial Debug Reader ==="
Write-Host "  VM   : $VMName" -ForegroundColor Gray
Write-Host "  Pipe : \\.\pipe\$PipeName" -ForegroundColor Gray
Write-Host "  Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host ''

try {
    $firstRun = $true
    while ($true) {
        if (-not $firstRun) {
            Write-Host ''
            Write-Serial "--- Disconnected (VM rebooted/reset?) ---"
            Write-Serial "Waiting for VM to come back..."

            # Wait for VM to be running again (it may be mid-reboot)
            $waited = 0
            while ($waited -lt 30) {
                $vm = Get-VM -Name $VMName -ErrorAction SilentlyContinue
                if ($vm -and $vm.State -eq 'Running') { break }
                Start-Sleep -Seconds 1
                $waited++
            }

            if ($waited -ge 30) {
                Write-Serial "VM did not restart within 30 seconds. Exiting."
                break
            }

            Write-Serial "Reconnecting..."
            $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
                '.', $PipeName, [System.IO.Pipes.PipeDirection]::In)
            try {
                $pipe.Connect(10000)
                Write-Serial "Reconnected — reading serial output..."
                Write-Host ''
                $reader = [System.IO.StreamReader]::new($pipe)
                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()
                    if ($null -ne $line) { Write-Host $line }
                }
            }
            catch {
                Write-Serial "Reconnect failed: $($_.Exception.Message)"
            }
            finally {
                if ($reader) { $reader.Dispose(); $reader = $null }
                $pipe.Dispose()
            }
        }
        else {
            $firstRun = $false
            $ok = Start-AndConnect
            if (-not $ok) { break }
        }
    }
}
catch {
    # Ctrl+C or other termination
}
finally {
    Write-Host ''
    Write-Serial "Stopped."
}
