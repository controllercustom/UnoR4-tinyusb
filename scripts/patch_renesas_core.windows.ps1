# patch_renesas_core.windows.ps1 - Upgrade Arduino Renesas core (Uno R4) to
# TinyUSB master snapshot using only stock Windows PowerShell 5.1 (no patch.exe,
# no Git, no Python required). Tree copy + checksum via built-in cmdlets;
# USB.cpp edits are exact-match replacements; LF and CRLF
# files both work. Idempotent; partially-patched cores are rejected.
#
# Run from cmd.exe or PowerShell:
#   powershell -NoProfile -ExecutionPolicy Bypass -File patch_renesas_core.windows.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File patch_renesas_core.windows.ps1 -Restore
# (or double-click patch_renesas_core.windows.bat)
#
# Sibling scripts: patch_renesas_core.sh (Linux), patch_renesas_core.macos.sh.
# All three do the same steps; the flow is documented in patch_renesas_core.sh.
#
# Pinned core version: 1.6.0 (pass -AllowAnyVersion to override).
# Test hook: set R4TUSB_CORE_DIR to patch a copy of the core instead.

param([switch]$Restore, [switch]$AllowAnyVersion)
$ErrorActionPreference = 'Stop'

$ScriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$PinnedVersion = '1.6.0'
$ConfigSrc     = Join-Path (Split-Path $ScriptDir -Parent) 'src\tusb_config_unor4tinyusb.h'
$Vendored      = Join-Path $ScriptDir 'tinyusb-master'

if (-not (Test-Path $ConfigSrc))  { Write-Error "ERROR: $ConfigSrc not found" }
if (-not (Test-Path $Vendored -PathType Container)) { Write-Error "ERROR: vendored tree not found: $Vendored" }
$TargetTusb = (Get-Content (Join-Path $Vendored 'VERSION') -Raw).Trim()
if (-not $TargetTusb) { $TargetTusb = 'unknown' }

# ---- Locate the installed core ---------------------------------------------
if ($env:R4TUSB_CORE_DIR -and (Test-Path $env:R4TUSB_CORE_DIR)) {
    $CoreDir = $env:R4TUSB_CORE_DIR
} else {
    $DataDir = $null
    try {
        $out = & arduino-cli config get directories.data 2>$null
        if ($LASTEXITCODE -eq 0 -and $out) { $DataDir = [string]$out }
    } catch { }
    if (-not $DataDir -or -not (Test-Path $DataDir)) {
        $DataDir = Join-Path $env:LOCALAPPDATA 'Arduino15'
    }
    $CoreRoot = Join-Path $DataDir 'packages\arduino\hardware\renesas_uno'
    if (-not (Test-Path $CoreRoot)) {
        Write-Error "ERROR: no renesas_uno core installed. Install via Boards Manager: Arduino UNO R4 Boards"
    }
    $Versions = Get-ChildItem -LiteralPath $CoreRoot -Directory | Sort-Object -Property Name
    if ($Versions.Count -eq 0) {
        Write-Error "ERROR: no version directory under $CoreRoot"
    }
    $CoreDir = $Versions[-1].FullName
}
$Ver = Split-Path -Leaf $CoreDir

if ($Ver -ne $PinnedVersion -and -not $AllowAnyVersion) {
    Write-Error ("ERROR: core version {0} found, but this script is pinned to {1}. Re-run with -AllowAnyVersion to try anyway." -f $Ver, $PinnedVersion)
}

Write-Host "[patch] core $CoreDir"

$UsbCpp  = Join-Path $CoreDir 'cores\arduino\usb\USB.cpp'
$TusbTree = Join-Path $CoreDir 'cores\arduino\tinyusb'
$Hcd     = Join-Path $TusbTree 'portable\renesas\rusb2\hcd_rusb2.c'
$MiniCfg = Join-Path $CoreDir 'variants\MINIMA\tusb_config.h'
$MiniMarker = Join-Path $CoreDir 'variants\MINIMA\.r4tinyusb_patched'
$NanoCfg = Join-Path $CoreDir 'variants\NANOR4\tusb_config.h'
$NanoMarker = Join-Path $CoreDir 'variants\NANOR4\.r4tinyusb_patched'
$BakDir  = Join-Path $CoreDir '.r4tinyusb'
$TreeBak = Join-Path $BakDir 'tinyusb.bak'

# ---- Edit definitions: byte-exact segments of stock 1.6.0 USB.cpp, with the
# ---- same effect as scripts/renesas_core.patch; applied in order.
# ---- The TinyUSB sources come pre-patched from scripts/tinyusb-master/ (the
# ---- RUSB2 HCD fixes live in that vendored tree, see its PIN file). --------
$Edits = @(
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'weak_device_cb'
        Old    = @'
const uint8_t *tud_descriptor_device_cb(void) {
'@
        New    = @'
__attribute__((weak)) const uint8_t *tud_descriptor_device_cb(void) {
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'weak_hid_report_cb'
        Old    = @'
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
'@
        New    = @'
__attribute__((weak)) uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'weak_config_cb'
        Old    = @'
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
'@
        New    = @'
__attribute__((weak)) const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'weak_isr_handler'
        Old    = @'
void _usbfs_interrupt_handler(void)
'@
        New    = @'
__attribute__((weak)) void _usbfs_interrupt_handler(void)
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'modernize_tuh_init'
        Old    = @'
    tud_init(BOARD_TUD_RHPORT);
'@
        New    = @'
    {
      tusb_rhport_init_t rh_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
      tud_rhport_init(BOARD_TUD_RHPORT, &rh_init);
    }
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'modernize_tud_init'
        Old    = @'
    tuh_init(BOARD_TUH_RHPORT);
'@
        New    = @'
    {
      tusb_rhport_init_t rh_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_FULL };
      tuh_rhport_init(BOARD_TUH_RHPORT, &rh_init);
    }
'@
    },
)

# ---- Helpers ----------------------------------------------------------------
function Read-CoreFile([string]$Rel) {
    [System.IO.File]::ReadAllText((Join-Path $CoreDir $Rel))
}
function Write-CoreFile([string]$Rel, [string]$Text) {
    [System.IO.File]::WriteAllText((Join-Path $CoreDir $Rel), $Text, (New-Object System.Text.UTF8Encoding($false)))
}

# Exact replace that tolerates LF vs CRLF working trees.
function Apply-Replace([string]$Text, [string]$Old, [string]$New, [string]$Label) {
    foreach ($eol in @("`r`n", "`n")) {
        $o = $Old.Replace("`n", $eol)
        if ($Text.Contains($o)) {
            return @{ Text = $Text.Replace($o, $New.Replace("`n", $eol)); Ok = $true }
        }
    }
    throw "Marker not found in current core: $Label"
}

function Test-Marker([string]$Rel, [string]$Pattern) {
    (Read-CoreFile $Rel).IndexOf($Pattern, [System.StringComparison]::Ordinal) -ge 0
}

# ---- State checks ------------------------------------------------------------
$haveUsb = Test-Marker 'cores\arduino\usb\USB.cpp' '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb'
$haveMini = Test-Marker 'variants\MINIMA\tusb_config.h' 'scripts/config.sh'
$haveNano = Test-Marker 'variants\NANOR4\tusb_config.h' 'scripts/config.sh'
$haveHcd = (Test-Path $Hcd) -and (Test-Marker 'cores\arduino\tinyusb\portable\renesas\rusb2\hcd_rusb2.c' 'rusb->DCPCFG_b.DIR != 1')
$haveStr = Test-Marker 'cores\arduino\usb\USB.cpp' '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb'
$markVerMini = ''
if (Test-Path $MiniMarker) { $markVerMini = (Get-Content $MiniMarker -Raw).Trim() }
$markVerNano = ''
if (Test-Path $NanoMarker) { $markVerNano = (Get-Content $NanoMarker -Raw).Trim() }
$total = 0; foreach ($h in @($haveUsb, $haveMini, $haveNano, $haveHcd, $haveStr)) { if ($h) { $total++ } }

function Invoke-Restore() {
    Write-Host "[patch] restoring core $CoreDir"
    $restored = $false
    foreach ($cfg in @($MiniCfg, $NanoCfg)) {
        if (Test-Path "$cfg.r4tinyusb_orig") {
            Copy-Item -Force "$cfg.r4tinyusb_orig" $cfg
            Remove-Item -Force "$cfg.r4tinyusb_orig"
            Write-Host "[patch] restored $cfg"
            $restored = $true
        }
    }
    if (Test-Path "$UsbCpp.r4tinyusb_orig") {
        Copy-Item -Force "$UsbCpp.r4tinyusb_orig" $UsbCpp
        Remove-Item -Force "$UsbCpp.r4tinyusb_orig"
        Write-Host "[patch] restored $UsbCpp"
        $restored = $true
    }
    if (Test-Path $TreeBak) {
        if (Test-Path $TusbTree) { Remove-Item -Recurse -Force $TusbTree }
        Move-Item $TreeBak $TusbTree
        foreach ($sidecar in @('rusb2\hcd_rusb2.c.r4tinyusb_orig',
                               'portable\renesas\rusb2\hcd_rusb2.c.r4tinyusb_orig') ) {
            $p = Join-Path $TusbTree $sidecar
            if (Test-Path $p) { Remove-Item -Force $p }
        }
        Write-Host "[patch] restored $TusbTree"
        $restored = $true
    }
    foreach ($m in @($MiniMarker, $NanoMarker)) {
        if (Test-Path $m) { Remove-Item -Force $m; $restored = $true }
    }
    if ((Test-Path $BakDir) -and -not (Get-ChildItem -LiteralPath $BakDir -Force)) {
        Remove-Item -Force $BakDir
    }
    if (-not $restored) { Write-Error "[patch] nothing to restore (no backups found)" }
    Write-Host '[patch] restore done.'
}

if ($Restore) { Invoke-Restore; exit 0 }

if ($markVerMini -eq $TargetTusb -and $markVerNano -eq $TargetTusb -and $total -eq 5) {
    Write-Host "[patch] already upgraded to TinyUSB $TargetTusb, skipping."
    exit 0
}
if ($total -ne 0 -or $markVerMini -or $markVerNano) {
    $state = "usb=$([int]$haveUsb) minima=$([int]$haveMini) nano=$([int]$haveNano) hcd=$([int]$haveHcd) str=$([int]$haveStr)"
    Write-Error "[patch] ERROR: core appears PARTIALLY patched (markers: $state). Restore first: re-run with -Restore"
}

# ---- Verify vendored tree (Get-FileHash ships with Windows) ------------------
Write-Host '[patch] verifying vendored tree checksums...'
$manifest = Get-Content (Join-Path $Vendored 'SHA256SUMS')
foreach ($line in $manifest) {
    $line = $line.Trim()
    if (-not $line) { continue }
    $parts = $line -split '\s+'
    $digest = $parts[0]
    $rel = ($parts[1..($parts.Length - 1)] -join ' ').TrimStart('*')
    $f = Join-Path $Vendored ($rel -replace '/', '\')
    if (-not (Test-Path $f)) { Write-Error "[patch] ERROR: $rel missing from vendored tree" }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower()
    if ($actual -ne $digest.ToLower()) { Write-Error "[patch] ERROR: checksum mismatch $rel" }
}
Write-Host '[patch] vendored tree checksums OK'

# ---- Back up (at the core root: a backup under cores/arduino/ would --------
# ---- double-compile, arduino-cli builds every .c file found there) -----------
if (-not (Test-Path $BakDir)) { New-Item -ItemType Directory $BakDir | Out-Null }
Copy-Item -Force $MiniCfg "$MiniCfg.r4tinyusb_orig"
Copy-Item -Force $NanoCfg "$NanoCfg.r4tinyusb_orig"
Copy-Item -Force $UsbCpp "$UsbCpp.r4tinyusb_orig"
if (Test-Path $TreeBak) { Remove-Item -Recurse -Force $TreeBak }
Copy-Item -Recurse -Force $TusbTree $TreeBak
Write-Host "[patch] backups in $BakDir + *.r4tinyusb_orig"

# ---- Replace tree -------------------------------------------------------------
Get-ChildItem -LiteralPath $TusbTree -Force | Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $Vendored -Exclude 'SHA256SUMS','VERSION','PIN' | ForEach-Object {
    Copy-Item -Recurse -Force $_.FullName (Join-Path $TusbTree $_.Name)
}
Write-Host "[patch] replaced $TusbTree with TinyUSB $TargetTusb"

# ---- Install config (MINIMA + NANOR4; stock files are identical) ---------------
Copy-Item -Force $ConfigSrc $MiniCfg
Write-Host "[patch] wrote $MiniCfg"
Copy-Item -Force $ConfigSrc $NanoCfg
Write-Host "[patch] wrote $NanoCfg"

# ---- Apply USB.cpp + hcd edits ---------------------------------------------------
$files = @{}
foreach ($e in $Edits) {
    if (-not $files.ContainsKey($e.File)) { $files[$e.File] = Read-CoreFile $e.File }
}
foreach ($e in $Edits) {
    $r = Apply-Replace $files[$e.File] $e.Old $e.New $e.Label
    $files[$e.File] = $r.Text
    Write-Host ("  patched: {0} ({1})" -f $e.File, $e.Label)
}
foreach ($rel in $files.Keys) { Write-CoreFile $rel $files[$rel] }

# ---- Post-checks ------------------------------------------------------------------
$fail = $false
if (-not (Test-Marker 'cores\arduino\usb\USB.cpp' '__attribute__((weak)) const uint16_t *tud_descriptor_string_cb')) {
    Write-Host '[patch] POSTCHECK FAIL: USB.cpp weak string descriptor'; $fail = $true
}
if (-not (Test-Marker 'cores\arduino\usb\USB.cpp' 'tud_rhport_init(BOARD_TUD_RHPORT')) {
    Write-Host '[patch] POSTCHECK FAIL: USB.cpp tusb_rhport_init'; $fail = $true
}
if (-not (Test-Marker 'cores\arduino\usb\USB.cpp' '__attribute__((weak)) const uint8_t *tud_descriptor_device_cb')) {
    Write-Host '[patch] POSTCHECK FAIL: USB.cpp weak hooks'; $fail = $true
}
if (-not (Test-Marker 'variants\MINIMA\tusb_config.h' 'scripts/config.sh')) {
    Write-Host '[patch] POSTCHECK FAIL: MINIMA tusb_config.h install'; $fail = $true
}
if (-not (Test-Marker 'variants\NANOR4\tusb_config.h' 'scripts/config.sh')) {
    Write-Host '[patch] POSTCHECK FAIL: NANOR4 tusb_config.h install'; $fail = $true
}
if (-not (Test-Marker 'cores\arduino\tinyusb\portable\renesas\rusb2\hcd_rusb2.c' 'rusb->DCPCFG_b.DIR != 1')) {
    Write-Host '[patch] POSTCHECK FAIL: hcd_rusb2.c fixes'; $fail = $true
}
if ($fail) { exit 1 }

[System.IO.File]::WriteAllText($MiniMarker, "$TargetTusb`n", (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText($NanoMarker, "$TargetTusb`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "[patch] done. TinyUSB $TargetTusb installed."
Write-Host '[patch] the config source of truth is src/tusb_config_unor4tinyusb.h'
Write-Host '[patch] change it with ./scripts/config.sh set <Example> | union'
