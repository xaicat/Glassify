param(
    [string]$BuildDirectory = "$PSScriptRoot\..\out\local",
    [string]$CMake = 'cmake',
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath("$PSScriptRoot\..")
$cmakeSource = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($cmakeSource, 'project\(Glassify VERSION ([0-9.]+)')
if (-not $versionMatch.Success) { throw 'Cannot determine project version' }
$version = $versionMatch.Groups[1].Value
$destination = Join-Path $root "dist\Glassify-$version"
if (Test-Path -LiteralPath $destination) {
    throw 'The package destination already exists. Move it aside before packaging to avoid stale files.'
}
& $CMake --install $BuildDirectory --config $Configuration --prefix $destination
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed' }
$archive = Join-Path $root "dist\Glassify-$version-windows-x64.zip"
Compress-Archive -LiteralPath $destination -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
Write-Output $archive
Write-Output $hash
