<#
.SYNOPSIS
    Build script for MNOS16.  Assembles all boot components (release + debug),
    creates a unified partitioned VHD with both kernel configurations.

.DESCRIPTION
    1. Downloads NASM if not found on PATH or in tools/nasm/.
    2. Assembles shared components: MBR, VBR, LOADER
    3. Assembles release variants: FS, KERNEL, SHELL
    4. Assembles debug variants: FSD, KERNELD, SHELLD (with -dDEBUG)
    5. Creates a partitioned raw disk image with all 7 MNFS files
    6. Wraps the raw image as a VHD
    7. (Optional) Runs unit tests via pytest + Unicorn Engine

.PARAMETER Clean
    Remove the build/ directory before building.

.PARAMETER Test
    Run unit tests after a successful build.  Requires Python 3.10+ and
    the packages listed in tests/requirements.txt.
    Tests run by default; use -NoTest to skip.

.PARAMETER NoTest
    Skip unit tests after building.

.PARAMETER DiskSizeMB
    VHD disk size in megabytes (default: 16).
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Test,
    [switch]$NoTest,
    [int]$DiskSizeMB = 16
)

$ErrorActionPreference = 'Stop'
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Root       = Split-Path -Parent $ScriptDir

# ---------- paths -----------------------------------------------------------
$BuildDir   = Join-Path $Root 'build\boot'
$ToolsDir   = $ScriptDir
$NasmDir    = Join-Path $ToolsDir 'nasm'
$SrcBoot    = Join-Path $Root 'src\boot'
$MbrAsm     = Join-Path $SrcBoot 'mbr.asm'
$VbrAsm     = Join-Path $SrcBoot 'vbr.asm'
$LoaderAsm  = Join-Path $Root 'src\loader\loader.asm'
$KernelAsm  = Join-Path $Root 'src\kernel\kernel.asm'
$FsAsm      = Join-Path $Root 'src\fs\fs.asm'
$ShellAsm   = Join-Path $Root 'src\shell\shell.asm'
$MmAsm      = Join-Path $Root 'src\mm\mm.asm'
$IncludeDir = Join-Path $Root 'src\include'

# Shared binaries
$MbrBin     = Join-Path $BuildDir 'mbr.bin'
$VbrBin     = Join-Path $BuildDir 'vbr.bin'
$LoaderBin  = Join-Path $BuildDir 'loader.sys'

# Release binaries
$FsBin      = Join-Path $BuildDir 'fs.sys'
$KernelBin  = Join-Path $BuildDir 'kernel.sys'
$ShellBin   = Join-Path $BuildDir 'shell.sys'
$MmBin      = Join-Path $BuildDir 'mm.sys'

# Debug binaries
$FsDbgBin     = Join-Path $BuildDir 'fsd.sys'
$KernelDbgBin = Join-Path $BuildDir 'kerneld.sys'
$ShellDbgBin  = Join-Path $BuildDir 'shelld.sys'
$MmDbgBin     = Join-Path $BuildDir 'mmd.sys'

$RawImg     = Join-Path $BuildDir 'MNOS16.img'
$VhdOut     = Join-Path $BuildDir 'MNOS16.vhd'

# ---------- helpers ---------------------------------------------------------
function Write-Step([string]$msg) { Write-Host "[MNOS16] $msg" -ForegroundColor Cyan }

function Build-Binary {
    param(
        [string]$Name,
        [string]$AsmPath,
        [string]$BinPath,
        [int]$ExpectedSize = 0,
        [switch]$Debug
    )
    $label = if ($Debug) { "Assembling ${Name} (DEBUG)..." } else { "Assembling ${Name}..." }
    Write-Step $label

    $srcDir = Split-Path $AsmPath -Parent
    $flags = @('-f', 'bin', '-I', "$IncludeDir/", '-I', "$srcDir/", '-o', $BinPath)
    if ($Debug) { $flags = @('-dDEBUG') + $flags }
    $flags += $AsmPath
    & $nasm @flags
    if ($LASTEXITCODE -ne 0) { throw "NASM assembly of $Name failed." }

    $size = (Get-Item $BinPath).Length
    $sectors = [math]::Ceiling($size / 512)
    Write-Step "  $([System.IO.Path]::GetFileName($BinPath)): $size bytes ($sectors sectors)"

    if ($ExpectedSize -gt 0 -and $size -ne $ExpectedSize) {
        Write-Warning "$Name is $size bytes (expected $ExpectedSize)."
    }
    if (($size % 512) -ne 0) {
        Write-Warning "$Name size is not a multiple of 512 bytes."
    }
}

function Build-RelocModule {
    <#
    .SYNOPSIS
        Build a relocatable system module (MNEX v2 format).
    .DESCRIPTION
        1. Assembles source at ORG 0 (raw binary, no header)
        2. Runs gen_relocs.py to produce relocation table (.rel)
        3. Runs pack_module.py to produce final .SYS with v2 header
    #>
    param(
        [string]$Name,
        [string]$AsmPath,
        [string]$BinPath,
        [string]$Magic,
        [switch]$Debug
    )
    $label = if ($Debug) { "Building relocatable ${Name} (DEBUG)..." } else { "Building relocatable ${Name}..." }
    Write-Step $label

    $srcDir = Split-Path $AsmPath -Parent
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($BinPath)
    $rawBin = Join-Path $BuildDir "${baseName}_raw.bin"
    $relFile = Join-Path $BuildDir "${baseName}.rel"

    # Common NASM flags
    $commonFlags = @('-I', "$IncludeDir/", '-I', "$srcDir/")
    if ($Debug) { $commonFlags = @('-dDEBUG') + $commonFlags }

    # Step 1: Assemble raw binary (ORG 0) — this is what goes into the module
    $flags = @('-f', 'bin') + $commonFlags + @('-o', $rawBin, $AsmPath)
    & $nasm @flags
    if ($LASTEXITCODE -ne 0) { throw "NASM assembly of $Name (raw) failed." }

    # Step 2: Generate relocation table via gen_relocs.py
    $genRelocs = Join-Path $ToolsDir 'gen_relocs.py'
    $genArgs = @(
        $genRelocs, $AsmPath,
        '--nasm', $nasm,
        '--header-size', '0',
        '-I', "$IncludeDir/",
        '-I', "$srcDir/",
        '-o', $relFile
    )
    if ($Debug) { $genArgs += @('-D', 'DEBUG') }
    & $python @genArgs
    if ($LASTEXITCODE -ne 0) { throw "gen_relocs.py failed for $Name." }

    # Step 3: Package with pack_module.py
    $packModule = Join-Path $ToolsDir 'pack_module.py'
    & $python $packModule $rawBin $relFile --magic $Magic --pad-sectors -o $BinPath
    if ($LASTEXITCODE -ne 0) { throw "pack_module.py failed for $Name." }

    $size = (Get-Item $BinPath).Length
    $sectors = [math]::Ceiling($size / 512)
    Write-Step "  $([System.IO.Path]::GetFileName($BinPath)): $size bytes ($sectors sectors)"
}

function Get-NasmPath {
    $found = Get-Command nasm -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    $local = Join-Path $NasmDir 'nasm.exe'
    if (Test-Path $local) { return $local }
    return $null
}

function Install-Nasm {
    Write-Step 'NASM not found — downloading...'
    $version = '2.16.03'
    $zip     = "nasm-$version-win64.zip"
    $url     = "https://www.nasm.us/pub/nasm/releasebuilds/$version/win64/$zip"
    $tmp     = Join-Path $env:TEMP $zip

    Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
    Expand-Archive -Path $tmp -DestinationPath $ToolsDir -Force
    Rename-Item (Join-Path $ToolsDir "nasm-$version") $NasmDir -Force
    Remove-Item $tmp -ErrorAction SilentlyContinue

    $exe = Join-Path $NasmDir 'nasm.exe'
    if (-not (Test-Path $exe)) {
        throw "NASM download/extract failed — $exe not found."
    }
    Write-Step "NASM installed to $NasmDir"
    return $exe
}

# ---------- clean -----------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step 'Cleaning build directory...'
    Remove-Item $BuildDir -Recurse -Force
}

# ---------- ensure build dir ------------------------------------------------
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

# ---------- NASM ------------------------------------------------------------
$nasm = Get-NasmPath
if (-not $nasm) { $nasm = Install-Nasm }
Write-Step "Using NASM: $nasm"

# ---------- Python (needed for relocation tools) ----------------------------
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "Python not found on PATH — required for relocatable module builds." }

# ---------- assemble shared binaries ----------------------------------------
Build-Binary -Name 'MBR'    -AsmPath $MbrAsm    -BinPath $MbrBin    -ExpectedSize 512
Build-Binary -Name 'VBR'    -AsmPath $VbrAsm    -BinPath $VbrBin
Build-Binary -Name 'LOADER' -AsmPath $LoaderAsm -BinPath $LoaderBin

# ---------- assemble release variants ---------------------------------------
Write-Step '--- Release variants ---'
Build-RelocModule -Name 'FS'     -AsmPath $FsAsm     -BinPath $FsBin     -Magic 'MNFS'
Build-Binary      -Name 'KERNEL' -AsmPath $KernelAsm -BinPath $KernelBin
Build-RelocModule -Name 'SHELL'  -AsmPath $ShellAsm  -BinPath $ShellBin  -Magic 'MNEX'
Build-RelocModule -Name 'MM'     -AsmPath $MmAsm     -BinPath $MmBin     -Magic 'MNMM'

# ---------- assemble debug variants -----------------------------------------
Write-Step '--- Debug variants ---'
Build-RelocModule -Name 'FSD'     -AsmPath $FsAsm     -BinPath $FsDbgBin     -Magic 'MNFS' -Debug
Build-Binary      -Name 'KERNELD' -AsmPath $KernelAsm -BinPath $KernelDbgBin -Debug
Build-RelocModule -Name 'SHELLD'  -AsmPath $ShellAsm  -BinPath $ShellDbgBin  -Magic 'MNEX' -Debug
Build-RelocModule -Name 'MMD'     -AsmPath $MmAsm     -BinPath $MmDbgBin     -Magic 'MNMM' -Debug

# ---------- assemble user programs (relocatable, MNEX v2) -------------------
Write-Step '--- User programs ---'
$ProgramsDir = Join-Path $Root 'src\programs'
$ProgramOut  = @()
if (Test-Path $ProgramsDir) {
    # Programs to skip (source kept as examples but not included in VHD)
    $SkipPrograms = @('hello.asm')

    $programs = Get-ChildItem $ProgramsDir -Filter '*.asm'
    foreach ($prog in $programs) {
        if ($prog.Name -in $SkipPrograms) { continue }
        $outName = [System.IO.Path]::GetFileNameWithoutExtension($prog.Name) + '.mnx'
        $outPath = Join-Path $BuildDir $outName
        Build-RelocModule -Name $outName.ToUpper() -AsmPath $prog.FullName -BinPath $outPath -Magic 'MNEX'
        $ProgramOut += $outPath
    }
    # Also build programs in subdirectories (e.g., src/programs/edit/edit.asm)
    $subdirs = Get-ChildItem $ProgramsDir -Directory
    foreach ($subdir in $subdirs) {
        $mainAsm = Join-Path $subdir.FullName ($subdir.Name + '.asm')
        if (Test-Path $mainAsm) {
            $outName = $subdir.Name + '.mnx'
            $outPath = Join-Path $BuildDir $outName
            # Skip if already built from top-level (avoids double-build during transition)
            if ($outPath -notin $ProgramOut) {
                Build-RelocModule -Name $outName.ToUpper() -AsmPath $mainAsm -BinPath $outPath -Magic 'MNEX'
                $ProgramOut += $outPath
            }
        }
    }
}

# ---------- create partitioned disk image -----------------------------------
Write-Step 'Creating partitioned disk image...'
$DiskScript = Join-Path $ToolsDir 'create-disk.ps1'
$diskParams = @{
    MbrPath       = $MbrBin
    VbrPath       = $VbrBin
    LoaderPath    = $LoaderBin
    FsPath        = $FsBin
    KernelPath    = $KernelBin
    ShellPath     = $ShellBin
    MmPath        = $MmBin
    FsDbgPath     = $FsDbgBin
    KernelDbgPath = $KernelDbgBin
    ShellDbgPath  = $ShellDbgBin
    MmDbgPath     = $MmDbgBin
    OutputPath    = $RawImg
    SizeMB        = $DiskSizeMB
}
if ($ProgramOut.Count -gt 0) {
    $diskParams['UserPrograms'] = $ProgramOut
}
& $DiskScript @diskParams

# ---------- create VHD ------------------------------------------------------
Write-Step 'Creating VHD...'
$VhdScript = Join-Path $ToolsDir 'create-vhd.ps1'
& $VhdScript -InputPath $RawImg -OutputPath $VhdOut -SizeMB $DiskSizeMB

# ---------- done ------------------------------------------------------------
Write-Host ''
Write-Step '=== Build complete ==='
Write-Step "VHD: $VhdOut"
Write-Host ''
Write-Host 'To test in Hyper-V:' -ForegroundColor Yellow
Write-Host "  build.bat           — build the OS"
Write-Host "  setup-vm.bat        — create/update the VM"
Write-Host "  Start-VM 'MNOS16'   — boot it"
Write-Host ''

# ---------- unit tests -------------------------------------------------------
if (-not $NoTest) {
    Write-Host ''
    Write-Step '=== Running unit tests ==='

    $Python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $Python) {
        Write-Warning 'Python not found on PATH — skipping tests.'
        Write-Warning 'Install from https://www.python.org/ and run: pip install -r tests/requirements.txt'
    } else {
        $ReqFile = Join-Path $Root 'tests' 'requirements.txt'
        if (Test-Path $ReqFile) {
            Write-Step 'Installing test dependencies...'
            & $Python.Source -m pip install -q -r $ReqFile 2>&1 | Out-Null
        }

        # Generate constants.py from .inc files
        $GenScript = Join-Path $Root 'tests' 'gen_constants.py'
        Write-Step 'Generating test constants from .inc files...'
        & $Python.Source $GenScript
        if ($LASTEXITCODE -ne 0) {
            Write-Error 'Failed to generate test constants.'
            exit 1
        }

        $TestDir = Join-Path $Root 'tests'
        & $Python.Source -m pytest $TestDir -v --tb=short
        if ($LASTEXITCODE -ne 0) {
            Write-Error 'Unit tests FAILED.'
            exit 1
        }

        Write-Host ''
        Write-Step '=== All tests passed ==='
        Write-Host ''
    }
}
