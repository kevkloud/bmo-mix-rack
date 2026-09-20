# Install this tester build on Windows. Right-click -> Run with PowerShell, or
# from a prompt:  powershell -ExecutionPolicy Bypass -File install.ps1
#
# Two things, in this order:
#   1. remove bundles a previous build installed under a name this one no
#      longer uses (superseded.txt), so the DAW does not list both;
#   2. copy the VST3 bundles into the shared VST3 folder.
#
# There is no quarantine step: that is a macOS problem only. The shared VST3
# folder is under Program Files, which usually means this needs to run
# elevated -- but not always, so the script probes the folder and says so
# rather than assuming it from the Administrator role.

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

# Bundles that could not be removed or written, reported together at the end.
# Nothing below stops on the first one: a tester wants the eight that worked
# and one list of what did not, not an abort partway with no summary.
$stuck  = @()
$failed = @()

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
            # Being able to create a file in the folder does not mean being
            # able to delete a bundle already in it -- a tree another account
            # installed can refuse. Say so and carry on; a leftover superseded
            # bundle makes the DAW list two, which is worth reporting but is
            # not worth abandoning the install over. install.command has said
            # the same thing on this case from the start.
            try {
                Remove-Item -Recurse -Force $victim -ErrorAction Stop
                Write-Host "  - $old.vst3   (now $replacement)"
                $removed++
            } catch {
                Write-Host "  ! $old.vst3   -- could not remove it; delete it by hand"
                $stuck += "$old.vst3"
            }
        }
    }
    if ($removed -gt 0 -or $stuck.Count -gt 0) { Write-Host "" }
}

# -- 2. copy ---------------------------------------------------------------
if (-not (Test-Path $vst3Dir)) { New-Item -ItemType Directory -Force -Path $vst3Dir | Out-Null }

Write-Host "Installing -> $vst3Dir"

# A foreach statement rather than ForEach-Object, for two reasons: $failed is
# assigned in this scope, and inside a catch block $_ is the error record, not
# the pipeline item -- $_.Name there would name the exception, not the bundle.
foreach ($bundle in @(Get-ChildItem -Path (Join-Path $here 'VST3') -Filter '*.vst3')) {
    $name = $bundle.Name
    $src  = $bundle.FullName
    $target = Join-Path $vst3Dir $name

    # The existing bundle is removed first because Copy-Item -Force will not
    # replace a directory tree. That is the dangerous moment: if the remove
    # succeeds and the copy then fails, the DAW is left without a plugin it
    # had before. So the remove is allowed to fail cleanly -- the old copy
    # stays and this bundle is skipped -- and only a failure after the remove
    # is reported as the destructive one it is.
    if (Test-Path $target) {
        try {
            Remove-Item -Recurse -Force $target -ErrorAction Stop
        } catch {
            Write-Host "  ! $name -- cannot replace the installed copy; left as it was"
            $failed += $name
            continue
        }
    }

    try {
        Copy-Item -Recurse -Force $src $vst3Dir -ErrorAction Stop
    } catch {
        Write-Host "  ! $name -- the old copy was removed and the new one could not be written"
        $failed += $name
    }
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) of $($shipping.Count) did not install:"
    foreach ($f in $failed) { Write-Host "  - $f" }
    Write-Host ""
    Write-Host "This folder let the installer create a file but not replace those"
    Write-Host "bundles, which usually means another account installed them. Re-run"
    Write-Host "from an elevated PowerShell:"
    Write-Host "    powershell -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`""
    Write-Host ""
    Write-Host "Anything not listed above did install. Rescan plugins in your DAW."
    exit 1
}

if ($stuck.Count -gt 0) {
    Write-Host "Installed, but these superseded bundles are still in place:"
    foreach ($s in $stuck) { Write-Host "  - $s" }
    Write-Host ""
    Write-Host "Your DAW will list them alongside the ones that replaced them."
    Write-Host "Delete them by hand, or re-run from an elevated PowerShell."
    Write-Host ""
}

Write-Host "Done. Rescan plugins in your DAW."
Write-Host ""
Write-Host "Standalone apps were not installed; they are in Standalone\ if you want them."
