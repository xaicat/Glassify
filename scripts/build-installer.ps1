param(
    [string]$MakeNSIS = 'makensis',
    [string]$Version = '1.0.0'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath("$PSScriptRoot\..")
$stage = Join-Path $root "dist\Glassify-$Version"
if (-not (Test-Path -LiteralPath "$stage\Glassify.exe")) { throw 'Build the portable package first.' }
$output = Join-Path $root "dist\Glassify-$Version-windows-x64-setup.exe"
$uninstall = Join-Path $root 'out\uninstall-files.nsh'
$lines = @()
foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('$','$$')
    $lines += 'Delete "$INSTDIR\' + $relative + '"'
}
foreach ($directory in Get-ChildItem -LiteralPath $stage -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending) {
    $relative = $directory.FullName.Substring($stage.Length + 1).Replace('$','$$')
    $lines += 'RMDir "$INSTDIR\' + $relative + '"'
}
[IO.File]::WriteAllLines($uninstall,$lines,(New-Object Text.UTF8Encoding $false))
& $MakeNSIS "/DVERSION=$Version" "/DSTAGE=$stage" "/DOUTPUT=$output" "/DUNINSTALL_FILES=$uninstall" "$root\packaging\installer.nsi"
if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed' }
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($output))" | Set-Content -LiteralPath "$output.sha256" -Encoding ASCII
Write-Output $output
