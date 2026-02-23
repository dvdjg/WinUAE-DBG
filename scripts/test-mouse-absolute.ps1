# Test script for absolute mouse mode
# Run: powershell -ExecutionPolicy Bypass -File scripts\test-mouse-absolute.ps1
# Requires: WinUAE compiled with win32_absolute_mouse default = true

$ErrorActionPreference = "Stop"
$bin = Join-Path $PSScriptRoot "..\bin\winuae-gdb.exe"
$config = Join-Path $PSScriptRoot "..\bin\winuae.ini"  # or a .uae path

if (-not (Test-Path $bin)) {
    Write-Error "WinUAE not found: $bin. Run build.bat first."
}

Write-Host "Starting WinUAE (absolute mouse default)..."
Write-Host "1. Move Windows cursor over the Amiga display area."
Write-Host "2. Amiga cursor should follow 1:1 (no runaway, no offset sum)."
Write-Host "3. Use -noabsolute_mouse for legacy relative mode."
Write-Host ""

# Add -winmouselog for debug output
& $bin
