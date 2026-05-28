<#
.SYNOPSIS
    Create a partitioned raw disk image with MNFS filesystem.

.DESCRIPTION
    Builds a raw disk image with:
      1. MBR at sector 0 (with partition table stamped in)
      2. One partition starting at a configurable LBA offset
      3. VBR written at the partition's first sector
      4. MNFS directory table at partition sector 2
      5. Files packed contiguously starting at partition sector 3:
         LOADER.SYS, FS.SYS, KERNEL.SYS, SHELL.SYS, MM.SYS,
         FSD.SYS, KERNELD.SYS, SHELLD.SYS, MMD.SYS
      6. Partition start LBA stamped into VBR header at offset 9

    The MNFS directory table is generated automatically from the binaries.
    No hardcoded file offsets — all positions are determined at build time
    and recorded in the directory entries.

.PARAMETER MbrPath
    Path to the assembled MBR binary (512 bytes).

.PARAMETER VbrPath
    Path to the assembled VBR binary (multiple of 512 bytes).

.PARAMETER LoaderPath
    Path to the assembled LOADER.SYS binary (multiple of 512 bytes).

.PARAMETER FsPath
    Path to the assembled FS.SYS binary (release, multiple of 512 bytes).

.PARAMETER KernelPath
    Path to the assembled KERNEL.SYS binary (release, multiple of 512 bytes).

.PARAMETER ShellPath
    Path to the assembled SHELL.SYS binary (release, multiple of 512 bytes).

.PARAMETER MmPath
    Path to the assembled MM.SYS binary (release, multiple of 512 bytes).

.PARAMETER FsDbgPath
    Path to the assembled FSD.SYS binary (debug, multiple of 512 bytes).

.PARAMETER KernelDbgPath
    Path to the assembled KERNELD.SYS binary (debug, multiple of 512 bytes).

.PARAMETER ShellDbgPath
    Path to the assembled SHELLD.SYS binary (debug, multiple of 512 bytes).

.PARAMETER MmDbgPath
    Path to the assembled MMD.SYS binary (debug, multiple of 512 bytes).

.PARAMETER OutputPath
    Path for the output raw disk image.

.PARAMETER SizeMB
    Disk size in megabytes (default: 16).

.PARAMETER PartitionStartLBA
    LBA sector where the partition begins (default: 2048 = 1 MB offset).

.PARAMETER PartitionType
    Partition type byte (default: 0x7F — experimental/private use).
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MbrPath,
    [Parameter(Mandatory)][string]$VbrPath,
    [Parameter(Mandatory)][string]$LoaderPath,
    [Parameter(Mandatory)][string]$FsPath,
    [Parameter(Mandatory)][string]$KernelPath,
    [Parameter(Mandatory)][string]$ShellPath,
    [Parameter(Mandatory)][string]$MmPath,
    [Parameter(Mandatory)][string]$FsDbgPath,
    [Parameter(Mandatory)][string]$KernelDbgPath,
    [Parameter(Mandatory)][string]$ShellDbgPath,
    [Parameter(Mandatory)][string]$MmDbgPath,
    [string[]]$UserPrograms = @(),
    [Parameter(Mandatory)][string]$OutputPath,
    [int]$SizeMB = 16,
    [int]$PartitionStartLBA = 2048,
    [int]$PartitionType = 0x7F
)

$ErrorActionPreference = 'Stop'

# --- MNFS constants (must match mnfs.inc) ------------------------------------
$MNFS_DIR_SECTOR    = 2             # Directory table at partition sector 2
$MNFS_DIR_SECTORS   = 1             # 1 sector (512 bytes)
$MNFS_HDR_SIZE      = 32            # Header size in directory sector
$MNFS_ENTRY_SIZE    = 32            # Each directory entry is 32 bytes
$MNFS_MAX_ENTRIES   = 15            # (512 - 32) / 32
$MNFS_ATTR_SYSTEM   = 0x01
$MNFS_ATTR_EXEC     = 0x02

function Write-Step([string]$msg) { Write-Host "[create-disk] $msg" -ForegroundColor Cyan }

# ---------- validate inputs -------------------------------------------------
function Read-Binary([string]$Path, [string]$Name, [string]$ExpectedMagic, [int]$MagicOffset = 0) {
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Path))
    if (($bytes.Length % 512) -ne 0) {
        throw "$Name must be a multiple of 512 bytes (got $($bytes.Length))."
    }
    if ($ExpectedMagic) {
        $magic = [System.Text.Encoding]::ASCII.GetString($bytes, $MagicOffset, 4)
        if ($magic -ne $ExpectedMagic) {
            throw "$Name magic is '$magic' at offset $MagicOffset (expected '$ExpectedMagic')."
        }
    }
    $sectors = $bytes.Length / 512
    Write-Step "$Name`: $($bytes.Length) bytes ($sectors sectors)"
    return $bytes
}

$mbrBytes    = Read-Binary $MbrPath    'MBR'     $null
if ($mbrBytes.Length -ne 512) { throw "MBR must be exactly 512 bytes." }
if ($mbrBytes[510] -ne 0x55 -or $mbrBytes[511] -ne 0xAA) { throw "MBR missing boot signature." }

$vbrBytes    = Read-Binary $VbrPath    'VBR'     'MNOS' 3
if ($vbrBytes[510] -ne 0x55 -or $vbrBytes[511] -ne 0xAA) { throw "VBR missing boot signature." }

$loaderBytes = Read-Binary $LoaderPath 'LOADER'  'MNLD'
$fsBytes     = Read-Binary $FsPath     'FS'      'MNFS'
$kernelBytes = Read-Binary $KernelPath 'KERNEL'  'MNKN'
$shellBytes  = Read-Binary $ShellPath  'SHELL'   'MNEX'
$mmBytes     = Read-Binary $MmPath     'MM'      'MNMM'
$fsDbgBytes     = Read-Binary $FsDbgPath     'FSD'      'MNFS'
$kernelDbgBytes = Read-Binary $KernelDbgPath 'KERNELD'  'MNKN'
$shellDbgBytes  = Read-Binary $ShellDbgPath  'SHELLD'   'MNEX'
$mmDbgBytes     = Read-Binary $MmDbgPath     'MMD'      'MNMM'

# ---------- build file list and pack contiguously ---------------------------
# Files are packed starting at partition sector 3 (after VBR + directory)
$vbrSectors = $vbrBytes.Length / 512

$files = @(
    @{ Name = 'LOADER  SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $loaderBytes }
    @{ Name = 'FS      SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $fsBytes }
    @{ Name = 'KERNEL  SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $kernelBytes }
    @{ Name = 'SHELL   SYS'; Attr = $MNFS_ATTR_SYSTEM -bor $MNFS_ATTR_EXEC; Bytes = $shellBytes }
    @{ Name = 'MM      SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $mmBytes }
    @{ Name = 'FSD     SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $fsDbgBytes }
    @{ Name = 'KERNELD SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $kernelDbgBytes }
    @{ Name = 'SHELLD  SYS'; Attr = $MNFS_ATTR_SYSTEM -bor $MNFS_ATTR_EXEC; Bytes = $shellDbgBytes }
    @{ Name = 'MMD     SYS'; Attr = $MNFS_ATTR_SYSTEM; Bytes = $mmDbgBytes }
)

# Add user programs (.MNX files)
foreach ($progPath in $UserPrograms) {
    $progBytes = Read-Binary $progPath ([System.IO.Path]::GetFileNameWithoutExtension($progPath).ToUpper()) 'MNEX'
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($progPath).ToUpper()
    # Pad name to 8 chars, extension to 3 chars (8.3 format)
    $paddedName = $baseName.PadRight(8).Substring(0, 8) + 'MNX'
    $files += @{ Name = $paddedName; Attr = $MNFS_ATTR_EXEC; Bytes = $progBytes }
}

if ($files.Count -gt $MNFS_MAX_ENTRIES) {
    throw "Too many files ($($files.Count)) — MNFS supports max $MNFS_MAX_ENTRIES."
}

# Calculate start sectors (contiguous packing after directory)
$nextSector = $MNFS_DIR_SECTOR + $MNFS_DIR_SECTORS   # First file starts at sector 3
$totalDataSectors = 0

foreach ($f in $files) {
    $f.StartSector = $nextSector
    $f.SizeSectors = $f.Bytes.Length / 512
    $f.SizeBytes   = $f.Bytes.Length
    $nextSector   += $f.SizeSectors
    $totalDataSectors += $f.SizeSectors
    Write-Step "  $($f.Name): sector $($f.StartSector), $($f.SizeSectors) sectors"
}

# ---------- generate MNFS directory sector ----------------------------------
$dirSector = [byte[]]::new(512)

# Header (32 bytes)
$magic = [System.Text.Encoding]::ASCII.GetBytes('MNFS')
[Array]::Copy($magic, 0, $dirSector, 0, 4)                    # magic
$dirSector[4] = 0x01                                            # version
$dirSector[5] = [byte]$files.Count                             # file_count
$totalSectors = $MNFS_DIR_SECTORS + $totalDataSectors
$totalBytes = [BitConverter]::GetBytes([uint16]$totalSectors)
[Array]::Copy($totalBytes, 0, $dirSector, 6, 2)               # total_sectors

# Directory entries (32 bytes each, starting at offset 32)
$entryOffset = $MNFS_HDR_SIZE
foreach ($f in $files) {
    # Name (11 bytes, already in 8.3 format)
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($f.Name)
    [Array]::Copy($nameBytes, 0, $dirSector, $entryOffset + 0, 11)

    # Attributes (1 byte)
    $dirSector[$entryOffset + 11] = [byte]$f.Attr

    # Start sector (4 bytes, uint32 LE)
    $startBytes = [BitConverter]::GetBytes([uint32]$f.StartSector)
    [Array]::Copy($startBytes, 0, $dirSector, $entryOffset + 12, 4)

    # Size in sectors (2 bytes, uint16 LE)
    $secBytes = [BitConverter]::GetBytes([uint16]$f.SizeSectors)
    [Array]::Copy($secBytes, 0, $dirSector, $entryOffset + 16, 2)

    # Size in bytes (4 bytes, uint32 LE)
    $szBytes = [BitConverter]::GetBytes([uint32]$f.SizeBytes)
    [Array]::Copy($szBytes, 0, $dirSector, $entryOffset + 18, 4)

    # Remaining 10 bytes are already zero (reserved)
    $entryOffset += $MNFS_ENTRY_SIZE
}

Write-Step "MNFS directory: $($files.Count) files, $totalSectors total sectors"

# ---------- disk geometry ---------------------------------------------------
$diskSize = [long]$SizeMB * 1024 * 1024
$totalDiskSectors = [int]($diskSize / 512)

if ($PartitionStartLBA -ge $totalDiskSectors) {
    throw "Partition start LBA ($PartitionStartLBA) exceeds disk size ($totalDiskSectors sectors)."
}

$partSizeSectors = $totalDiskSectors - $PartitionStartLBA
Write-Step "Disk: $SizeMB MB ($totalDiskSectors sectors)"
Write-Step "Partition 1: LBA $PartitionStartLBA, size $partSizeSectors sectors"

# Stamp data area capacity into MNFS directory header
# Capacity = partition size - VBR sectors (everything MNFS manages)
$capacitySectors = $partSizeSectors - $vbrSectors
$capacityBytes = [BitConverter]::GetBytes([uint16]$capacitySectors)
[Array]::Copy($capacityBytes, 0, $dirSector, 8, 2)            # capacity

# ---------- stamp partition table into MBR ----------------------------------
$partEntry = [byte[]]::new(16)
$partEntry[0]  = 0x80
$partEntry[1]  = 0xFE; $partEntry[2] = 0xFF; $partEntry[3] = 0xFF
$partEntry[4]  = [byte]$PartitionType
$partEntry[5]  = 0xFE; $partEntry[6] = 0xFF; $partEntry[7] = 0xFF
$lbaBytes = [BitConverter]::GetBytes([uint32]$PartitionStartLBA)
[Array]::Copy($lbaBytes, 0, $partEntry, 8, 4)
$sizeBytes = [BitConverter]::GetBytes([uint32]$partSizeSectors)
[Array]::Copy($sizeBytes, 0, $partEntry, 12, 4)
[Array]::Copy($partEntry, 0, $mbrBytes, 0x1BE, 16)
Write-Step "Partition table stamped into MBR."

# ---------- stamp partition start LBA into VBR header -----------------------
$partLbaBytes = [BitConverter]::GetBytes([uint32]$PartitionStartLBA)
[Array]::Copy($partLbaBytes, 0, $vbrBytes, 9, 4)
Write-Step "Partition LBA ($PartitionStartLBA) stamped into VBR header."

# ---------- build the raw disk image ----------------------------------------
Write-Step "Writing disk image..."

$fs = [System.IO.FileStream]::new($OutputPath, 'Create', 'Write')

# Sector 0: MBR
$fs.Write($mbrBytes, 0, 512)

# Gap: sectors 1 to partition start
$gapBytes = ($PartitionStartLBA - 1) * 512
if ($gapBytes -gt 0) {
    $zeroBuf = [byte[]]::new([math]::Min($gapBytes, 65536))
    $remaining = $gapBytes
    while ($remaining -gt 0) {
        $chunk = [math]::Min($remaining, $zeroBuf.Length)
        $fs.Write($zeroBuf, 0, $chunk)
        $remaining -= $chunk
    }
}

# Partition sector 0+: VBR (all sectors)
$fs.Write($vbrBytes, 0, $vbrBytes.Length)

# Gap between VBR end and directory sector
$vbrEndSector = $vbrSectors
$gapToDir = ($MNFS_DIR_SECTOR - $vbrEndSector) * 512
if ($gapToDir -gt 0) {
    $zeroBuf = [byte[]]::new($gapToDir)
    $fs.Write($zeroBuf, 0, $gapToDir)
}

# MNFS directory sector
$fs.Write($dirSector, 0, 512)

# Write each file contiguously
$expectedSector = $MNFS_DIR_SECTOR + $MNFS_DIR_SECTORS
foreach ($f in $files) {
    # Fill any gap (shouldn't be any with contiguous packing, but be safe)
    $gap = ($f.StartSector - $expectedSector) * 512
    if ($gap -gt 0) {
        $zeroBuf = [byte[]]::new($gap)
        $fs.Write($zeroBuf, 0, $gap)
    }
    $fs.Write($f.Bytes, 0, $f.Bytes.Length)
    $expectedSector = $f.StartSector + $f.SizeSectors
}

# Zero-fill the rest of the disk
$lastAbsLBA = $PartitionStartLBA + $expectedSector
$remainingBytes = $diskSize - ($lastAbsLBA * 512)
if ($remainingBytes -gt 0) {
    $zeroBuf = [byte[]]::new([math]::Min($remainingBytes, 65536))
    $remaining = $remainingBytes
    while ($remaining -gt 0) {
        $chunk = [math]::Min($remaining, $zeroBuf.Length)
        $fs.Write($zeroBuf, 0, $chunk)
        $remaining -= $chunk
    }
}

$fs.Close()

$fileSize = (Get-Item $OutputPath).Length
Write-Step "Raw image: $OutputPath ($fileSize bytes)"
Write-Step "  Sector 0       : MBR (with partition table)"
Write-Step "  Sector $PartitionStartLBA  : VBR ($vbrSectors sectors, $($vbrBytes.Length) bytes)"
Write-Step "  Sector $($PartitionStartLBA + $MNFS_DIR_SECTOR)  : MNFS directory ($($files.Count) files)"
foreach ($f in $files) {
    $name = $f.Name.Trim()
    Write-Step "  Sector $($PartitionStartLBA + $f.StartSector)  : $name ($($f.SizeSectors) sectors, $($f.SizeBytes) bytes)"
}
