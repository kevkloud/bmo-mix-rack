# Install this tester build on Windows. Right-click -> Run with PowerShell, or
# from an elevated prompt:  powershell -ExecutionPolicy Bypass -File install.ps1
#
# Two things, in this order:
#   1. remove bundles a previous build installed under a name this one no
#      longer uses (superseded.txt), so the DAW does not list both;
#   2. copy the VST3 bundles into the shared VST3 folder.
#
# There is no quarantine step: that is a macOS problem only. The shared VST3
# folder is under Program Files, so this needs to run elevated.

$ErrorActionPreference = 'Stop'

$here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$vst3Dir = Join-Path $env:CommonProgramFiles 'VST3'

if (-not (Test-Path (Join-Path $here 'VST3'))) {
    Write-Host "No VST3 folder beside this script. Run it from the unzipped package."
    exit 1
}

# Whether the plug-in folder can actually be written is the question, so ask
# it directly rather than through the Administrator role. The two are not the
# same: Program Files is writable without elevation on a machine whose admin
# group has been granted it, and refusing there sends someone off to open an
# elevated prompt they never needed. Asking the real question is also what the
# macOS script does for the quarantine step, for the same reason.
function Test-Writable([string]$dir) {
    if (-not (Test-Path $dir)) {
        try   { New-Item -ItemType Directory -Force -Path $dir -ErrorAction Stop | Out-Null; return $true }
        catch { return $false }
    }
    $probe = Join-Path $dir (".bmo-write-test-" + [guid]::NewGuid().ToString("N"))
    try {
        New-Item -ItemType File -Path $probe -ErrorAction Stop | Out-Null
        Remove-Item -Force $probe -ErrorAction SilentlyContinue
        return $true
    } catch { return $false }
}

if (-not (Test-Writable $vst3Dir)) {
    Write-Host "Cannot write to $vst3Dir."
    Write-Host ""
    Write-Host "It is under Program Files, so this usually means the installer"
    Write-Host "needs to run elevated. Right-click install.ps1 and choose 'Run"
    Write-Host "with PowerShell' from an admin account, or from an elevated"
    Write-Host "PowerShell run:"
    Write-Host "    powershell -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`""
    exit 1
}

# What this build ships, by base name. Used to install and as the guard on
# removal below.
$shipping = @(Get-ChildItem -Path (Join-Path $here 'VST3') -Filter '*.vst3' |
              ForEach-Object { $_.BaseName } | Sort-Object)

Write-Host "Installing $($shipping.Count) plugins."
Write-Host ""

# -- 1. superseded ---------------------------------------------------------
$supersededFile = Join-Path $here 'superseded.txt'
if (Test-Path $supersededFile) {
    $removed = 0
    foreach ($line in Get-Content $supersededFile) {
        if ($line -match '^\s*#' -or $line -notmatch '\|') { continue }

        $parts       = $line -split '\|'
        $old         = $parts[0].Trim()
        $replacement = $parts[1].Trim()
        if ([string]::IsNullOrWhiteSpace($old)) { continue }

        # Never remove something this build also ships under that name.
        if ($shipping -contains $old) { continue }

        $victim = Join-Path $vst3Dir "$old.vst3"
        if (Test-Path $victim) {
            if ($removed -eq 0) { Write-Host "Removing superseded bundles" }
            Write-Host "  - $old.vst3   (now $replacement)"
            Remove-Item -Recurse -Force $victim
            $removed++
        }
    }
    if ($removed -gt 0) { Write-Host "" }
}

# -- 2. copy ---------------------------------------------------------------
if (-not (Test-Path $vst3Dir)) { New-Item -ItemType Directory -Force -Path $vst3Dir | Out-Null }

Write-Host "Installing -> $vst3Dir"
Get-ChildItem -Path (Join-Path $here 'VST3') -Filter '*.vst3' | ForEach-Object {
    $target = Join-Path $vst3Dir $_.Name
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Copy-Item -Recurse -Force $_.FullName $vst3Dir
}

Write-Host ""
Write-Host "Done. Rescan plugins in your DAW."
Write-Host ""
Write-Host "Standalone apps were not installed; they are in Standalone\ if you want them."
