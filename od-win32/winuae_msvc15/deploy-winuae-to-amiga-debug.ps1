# Deploy freshly built winuae-gdb*.exe to every Amiga Debug extension folder on this machine.
# Called from winuae_msvc.vcxproj PostBuild (CopyToAmigaDebug).
param(
	[Parameter(Mandatory = $true)][string]$BuiltExe,
	[Parameter(Mandatory = $true)][string]$BinSubdir
)
$exe = $BuiltExe.Trim()
if (-not [System.IO.Path]::IsPathRooted($exe)) {
	$exe = [System.IO.Path]::GetFullPath((Join-Path -Path (Get-Location).Path -ChildPath $exe))
} else {
	$exe = [System.IO.Path]::GetFullPath($exe)
}
if (-not (Test-Path -LiteralPath $exe)) {
	Write-Host "deploy-winuae-to-amiga-debug: skip, built exe not found: $exe"
	exit 0
}
$roots = @(
	[System.IO.Path]::Combine($env:USERPROFILE, '.cursor', 'extensions'),
	[System.IO.Path]::Combine($env:USERPROFILE, '.vscode', 'extensions')
)
$n = 0
foreach ($extRoot in $roots) {
	if (-not (Test-Path -LiteralPath $extRoot)) { continue }
	Get-ChildItem -LiteralPath $extRoot -Directory -Filter 'bartmanabyss.amiga-debug-*' -ErrorAction SilentlyContinue | ForEach-Object {
		$destDir = [System.IO.Path]::Combine($_.FullName, 'bin', $BinSubdir)
		$dest = [System.IO.Path]::Combine($destDir, 'winuae-gdb.exe')
		if (Test-Path -LiteralPath $destDir) {
			Copy-Item -LiteralPath $exe -Destination $dest -Force
			Write-Host "deploy-winuae-to-amiga-debug: $dest"
			$n++
		}
	}
}
if ($n -eq 0) {
	Write-Host "deploy-winuae-to-amiga-debug: WARNING - no bartmanabyss.amiga-debug-*/bin/$BinSubdir under .cursor or .vscode extensions. Amiga Debug may still run an old winuae-gdb.exe."
	exit 0
}
