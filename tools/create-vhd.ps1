<#
.SYNOPSIS
    Create a fixed-size VHD from a raw disk image.

.DESCRIPTION
    Implements the VHD 1.0 fixed-disk format:
      [raw data padded to disk size] + [512-byte footer]
    No external tools required — pure PowerShell.

.PARAMETER InputPath
    Path to the raw binary image (e.g. mbr.bin).

.PARAMETER OutputPath
    Path for the output VHD file.

.PARAMETER SizeMB
    Disk size in megabytes (default: 16).
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPath,
    [Parameter(Mandatory)][string]$OutputPath,
    [int]$SizeMB = 16
)

$ErrorActionPreference = 'Stop'

# ---------- CHS geometry per VHD spec ---------------------------------------
function Get-VhdGeometry([long]$totalSectors) {
    $maxSectors = [long]65535 * 16 * 255
    if ($totalSectors -gt $maxSectors) { $totalSectors = $maxSectors }

    if ($totalSectors -ge ([long]65535 * 16 * 63)) {
        $spt   = 255
        $heads = 16
        $cyls  = [math]::Floor($totalSectors / ($heads * $spt))
    } else {
        $spt = 17
        $cylTimesHeads = [math]::Floor($totalSectors / $spt)
        $heads = [math]::Floor(($cylTimesHeads + 1023) / 1024)
        if ($heads -lt 4)  { $heads = 4 }
        if ($cylTimesHeads -ge ($heads * 1024) -or $heads -gt 16) {
            $spt = 31; $heads = 16
            $cylTimesHeads = [math]::Floor($totalSectors / $spt)
        }
        if ($cylTimesHeads -ge ($heads * 1024)) {
            $spt = 63; $heads = 16
            $cylTimesHeads = [math]::Floor($totalSectors / $spt)
        }
        $cyls = [math]::Floor($cylTimesHeads / $heads)
    }
    if ($cyls -gt 65535) { $cyls = 65535 }
    return @{ Cylinders = [int]$cyls; Heads = [int]$heads; SectorsPerTrack = [int]$spt }
}

# ---------- build the 512-byte footer --------------------------------------
function New-VhdFooter([long]$diskSize) {
    $totalSectors = $diskSize / 512
    $geo = Get-VhdGeometry $totalSectors

    # Timestamp: seconds since 2000-01-01 00:00:00 UTC
    $epoch2000 = [DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero)
    $timestamp = [int]([DateTimeOffset]::UtcNow - $epoch2000).TotalSeconds

    # Generate a UUID
    $guidBytes = [guid]::NewGuid().ToByteArray()

    $footer = [byte[]]::new(512)
    $ms = [System.IO.MemoryStream]::new($footer)
    $bw = [System.IO.BinaryWriter]::new($ms)

    # Helper: write big-endian values
    function Write-BE32([uint32]$v) {
        $b = [BitConverter]::GetBytes($v); [Array]::Reverse($b); $bw.Write($b)
    }
    function Write-BE64([uint64]$v) {
        $b = [BitConverter]::GetBytes($v); [Array]::Reverse($b); $bw.Write($b)
    }
    function Write-BE16([uint16]$v) {
        $b = [BitConverter]::GetBytes($v); [Array]::Reverse($b); $bw.Write($b)
    }

    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('conectix'))  # Cookie (8)
    Write-BE32 0x00000002                                           # Features
    Write-BE32 0x00010000                                           # Format Version 1.0
    Write-BE64 ([uint64]::MaxValue)                                   # Data Offset (fixed=none)
    Write-BE32 ([uint32]$timestamp)                                 # Timestamp
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('mnos'))       # Creator App (4)
    Write-BE32 0x00010000                                           # Creator Version
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('Wi2k'))       # Creator Host OS
    Write-BE64 ([uint64]$diskSize)                                  # Original Size
    Write-BE64 ([uint64]$diskSize)                                  # Current Size
    Write-BE16 ([uint16]$geo.Cylinders)                             # Cylinders
    $bw.Write([byte]$geo.Heads)                                     # Heads
    $bw.Write([byte]$geo.SectorsPerTrack)                           # Sectors/Track
    Write-BE32 2                                                     # Disk Type = Fixed

    $checksumPos = $ms.Position
    Write-BE32 0                                                     # Checksum placeholder

    $bw.Write($guidBytes)                                            # Unique Id (16 bytes)
    $bw.Write([byte]0)                                               # Saved State

    $bw.Flush()

    # Compute checksum: one's complement of the sum of all bytes
    [uint32]$sum = 0
    foreach ($b in $footer) { $sum += $b }
    $chk = (-bnot $sum) -band 0xFFFFFFFF

    # Write checksum at its position
    $chkBytes = [BitConverter]::GetBytes([uint32]$chk)
    [Array]::Reverse($chkBytes)
    [Array]::Copy($chkBytes, 0, $footer, $checksumPos, 4)

    $bw.Dispose()
    $ms.Dispose()
    return $footer
}

# ---------- main ------------------------------------------------------------
$diskSize = [long]$SizeMB * 1024 * 1024

$raw = [System.IO.File]::ReadAllBytes((Resolve-Path $InputPath))
if ($raw.Length -gt $diskSize) {
    throw "Input image ($($raw.Length) bytes) exceeds disk size ($diskSize bytes)."
}

$footer = New-VhdFooter $diskSize

$fs = [System.IO.FileStream]::new($OutputPath, 'Create', 'Write')
$fs.Write($raw, 0, $raw.Length)

# Pad to disk size
$padLen = $diskSize - $raw.Length
if ($padLen -gt 0) {
    $zeroBuf = [byte[]]::new([math]::Min($padLen, 65536))
    $remaining = $padLen
    while ($remaining -gt 0) {
        $chunk = [math]::Min($remaining, $zeroBuf.Length)
        $fs.Write($zeroBuf, 0, $chunk)
        $remaining -= $chunk
    }
}

$fs.Write($footer, 0, $footer.Length)
$fs.Close()

$total = $diskSize + 512
$geo = Get-VhdGeometry ($diskSize / 512)
Write-Host "Created VHD: $OutputPath"
Write-Host "  Disk size : $SizeMB MB ($diskSize bytes)"
Write-Host "  VHD file  : $total bytes ($([math]::Round($total / 1MB, 2)) MB)"
Write-Host "  Geometry  : C/H/S = $($geo.Cylinders)/$($geo.Heads)/$($geo.SectorsPerTrack)"
