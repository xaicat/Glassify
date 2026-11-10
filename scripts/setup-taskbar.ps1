param([string]$DataDirectory = "$env:LOCALAPPDATA\Glassify")
$ErrorActionPreference = 'Stop'
$version = '2026.2'
$expected = '0dbe8e0255c20e131cde536dcd0ae490d45989a7d360e26d0150dff1922ac420'
$root = [IO.Path]::GetFullPath($DataDirectory)
if (-not [IO.Path]::IsPathRooted($DataDirectory)) { throw 'An absolute data directory is required.' }
New-Item -ItemType Directory -Force $root | Out-Null
$archive = Join-Path $root 'TranslucentTB-download.zip'
$destination = Join-Path $root 'TaskbarEngine'
$url = "https://github.com/TranslucentTB/TranslucentTB/releases/download/$version/TranslucentTB-portable-x64.zip"
try {
    if (Get-Process TranslucentTB -ErrorAction SilentlyContinue) { throw 'Quit TranslucentTB before setting up the engine.' }
    $winui=@(Get-AppxPackage Microsoft.UI.Xaml.2.8 | Where-Object { $_.Architecture -eq 'X64' -and [version]$_.Version -ge [version]'8.2501.31001.0' })
    if(-not $winui.Count){throw 'The taskbar engine needs Windows runtime components. Install the official TranslucentTB package from its Microsoft Store page, quit it, then retry setup. See docs/TASKBAR.md.'}
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Taskbar engine checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    @"
TranslucentTB $version. Unmodified optional companion, downloaded directly from its publisher.
Source and GPL-3.0 license: https://github.com/TranslucentTB/TranslucentTB/tree/$version
Binary source: $url
SHA256: $expected
Glassify does not redistribute this engine or link its GPL code into the native app.
The engine uses Explorer hooks and XAML diagnostics. Windows preview builds may be unsupported.
"@ | Set-Content -LiteralPath (Join-Path $destination 'SOURCE-AND-LICENSE.txt') -Encoding UTF8
    $version | Set-Content -LiteralPath (Join-Path $destination 'engine-version.txt') -Encoding ASCII
    'ready' | Set-Content -LiteralPath (Join-Path $root 'taskbar-setup-result.txt') -Encoding ASCII
} catch {
    $_.Exception.Message | Set-Content -LiteralPath (Join-Path $root 'taskbar-setup-result.txt') -Encoding UTF8
    throw
} finally {
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
}
