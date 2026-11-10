param(
    [string]$BuildDirectory = "$PSScriptRoot\..\out\local",
    [ValidateRange(10, 3600)][int]$Seconds = 300,
    [string]$Output = "$PSScriptRoot\..\out\idle-measurement.json"
)
$ErrorActionPreference = 'Stop'
if (Get-Process Glassify -ErrorAction SilentlyContinue) {
    throw 'Quit the running controller before measuring an isolated baseline.'
}
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$previousData = $env:WINFADEX_DATA_DIR
$env:WINFADEX_DATA_DIR = [IO.Path]::GetFullPath("$PSScriptRoot\..\out\idle-data")
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WinFadeXMeasurement {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowW(string c, string t);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static void Quit() {
        var h = FindWindowW("WinFadeX.Next", null);
        if (h != IntPtr.Zero) PostMessageW(h, 0x111, new IntPtr(142), IntPtr.Zero);
    }
}
'@
try {
    $controller = Start-Process -FilePath "$build\Glassify.exe" -ArgumentList '--background' -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 2
    $processes = @(Get-Process Glassify,GlassifyRecovery -ErrorAction Stop)
    $ids = @($processes.Id)
    $cpuStart = ($processes | Measure-Object -Property CPU -Sum).Sum
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $peakPrivate = 0
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Seconds ([Math]::Min(10, $Seconds))
        $sample = @(Get-Process -Id $ids -ErrorAction Stop)
        $bytes = ($sample | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
        $peakPrivate = [Math]::Max($peakPrivate, $bytes)
    }
    $watch.Stop()
    $final = @(Get-Process -Id $ids -ErrorAction Stop)
    $cpuEnd = ($final | Measure-Object -Property CPU -Sum).Sum
    $report = [ordered]@{
        measuredAt = (Get-Date).ToUniversalTime().ToString('o')
        durationSeconds = $watch.Elapsed.TotalSeconds
        logicalProcessors = [Environment]::ProcessorCount
        trackedWindows = 0
        notes = 'Isolated empty configuration, settings hidden. Does not measure DWM/GPU or enrolled-window cost.'
        totalCpuPercent = 100 * ($cpuEnd - $cpuStart) / $watch.Elapsed.TotalSeconds / [Environment]::ProcessorCount
        peakCombinedPrivateBytes = $peakPrivate
        processes = @($final | ForEach-Object { @{ name=$_.ProcessName; privateBytes=$_.PrivateMemorySize64; handles=$_.HandleCount } })
    }
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Output -Encoding UTF8
    $report | ConvertTo-Json -Depth 4
} finally {
    [WinFadeXMeasurement]::Quit()
    $env:WINFADEX_DATA_DIR = $previousData
}
