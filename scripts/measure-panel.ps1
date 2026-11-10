param([string]$BuildDirectory="$PSScriptRoot\..\out\local",[int]$Seconds=15,[ValidateSet('minimal','glass')][string]$Style='minimal',[ValidateSet('instant','low-memory')][string]$TrayMode='instant')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$build=(Resolve-Path $BuildDirectory).Path
if(Get-Process Glassify,VeilDesk,WinFadeXNext -ErrorAction SilentlyContinue){throw 'Quit all controllers before measuring.'}
$previous=$env:WINFADEX_DATA_DIR
$scratch=Join-Path $root 'out\panel-measurement-data'
New-Item -ItemType Directory -Force $scratch|Out-Null
@{schemaVersion=4;rules=@();mouseControl=$true;uiStyle=$Style;keepInterfaceReady=($TrayMode -eq 'instant')}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$scratch\config.json" -Encoding UTF8
$env:WINFADEX_DATA_DIR=$scratch
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class GlassifyPanelMeasurement{[DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindowW(string c,string t);[DllImport("user32.dll")]static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);public static void Command(int id){var h=FindWindowW("WinFadeX.Next",null);if(h!=IntPtr.Zero)PostMessageW(h,0x111,new IntPtr(id),IntPtr.Zero);}}
'@
function Descendants([int]$parentId){
 $all=Get-CimInstance Win32_Process
 $ids=New-Object 'Collections.Generic.HashSet[int]';$ids.Add($parentId)|Out-Null
 do{$added=$false;foreach($p in $all){if($ids.Contains([int]$p.ParentProcessId)-and $ids.Add([int]$p.ProcessId)){$added=$true}}}while($added)
 @($ids|ForEach-Object{Get-Process -Id $_ -ErrorAction SilentlyContinue})
}
function Measure-Group($name,$processId){
 $initial=@(Descendants $processId)
 $ids=@($initial.Id)
 $cpuStart=($initial|Measure-Object CPU -Sum).Sum
 $watch=[Diagnostics.Stopwatch]::StartNew();$peakPrivate=0;$peakWorking=0
 while($watch.Elapsed.TotalSeconds -lt $Seconds){
  $sample=@(Get-Process -Id $ids -ErrorAction SilentlyContinue)
  $peakPrivate=[Math]::Max($peakPrivate,($sample|Measure-Object PrivateMemorySize64 -Sum).Sum)
  $peakWorking=[Math]::Max($peakWorking,($sample|Measure-Object WorkingSet64 -Sum).Sum)
  Start-Sleep -Milliseconds 500
 }
 $final=@(Get-Process -Id $ids -ErrorAction SilentlyContinue)
 $watch.Stop();$cpuEnd=($final|Measure-Object CPU -Sum).Sum
 [ordered]@{scenario=$name;seconds=$watch.Elapsed.TotalSeconds;processCount=$final.Count;normalizedCpuPercent=100*($cpuEnd-$cpuStart)/$watch.Elapsed.TotalSeconds/[Environment]::ProcessorCount;peakPrivateBytes=$peakPrivate;peakWorkingSetBytes=$peakWorking;processes=@($final|ForEach-Object{@{name=$_.ProcessName;privateBytes=$_.PrivateMemorySize64}})}
}
try{
 $app=Start-Process -FilePath "$build\Glassify.exe" -WindowStyle Normal -PassThru
 Start-Sleep -Seconds 7
 $visible=Measure-Group 'styled-panel-visible-idle' $app.Id
 [GlassifyPanelMeasurement]::Command(255)
 Start-Sleep -Seconds 7
 $hidden=Measure-Group 'tray-hidden' $app.Id
 $report=[ordered]@{version='1.0.0';uiStyle=$Style;trayMode=$TrayMode;measuredAt=(Get-Date).ToUniversalTime().ToString('o');logicalProcessors=[Environment]::ProcessorCount;notes='Production controller, empty rules, mouse hook enabled. Includes controller/helper and all surviving child WebView2 processes. Visible panel is static; excludes DWM/GPU and taskbar companion. Working set sum can double-count shared pages.';scenarios=@($visible,$hidden)}
 $report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$root\out\panel-performance.json" -Encoding UTF8
 $report|ConvertTo-Json -Depth 7
}finally{
 [GlassifyPanelMeasurement]::Command(142)
 if($app){$app.WaitForExit(8000)|Out-Null}
 $env:WINFADEX_DATA_DIR=$previous
}
