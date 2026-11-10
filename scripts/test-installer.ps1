param([string]$Version='1.0.0',[switch]$TestStyledPanel)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$testPrefix=Join-Path $root ('out\installer-smoke-' + [Guid]::NewGuid().ToString('N'))
$resolved=[IO.Path]::GetFullPath($testPrefix)
if (-not $resolved.StartsWith((Join-Path $root 'out\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Test prefix escaped workspace.' }
if (Test-Path -LiteralPath $testPrefix) { throw 'Use a fresh installer smoke directory.' }
if (Test-Path 'HKCU:\Software\Glassify') { throw 'An installation is already registered; use an isolated Windows test account.' }
if (Get-Process Glassify,VeilDesk,WinFadeXNext -ErrorAction SilentlyContinue) { throw 'Quit all controllers first.' }
$setup=Join-Path $root "dist\Glassify-$Version-windows-x64-setup.exe"
$result=Start-Process -FilePath $setup -ArgumentList "/S /D=$testPrefix" -Wait -PassThru -WindowStyle Hidden
if($result.ExitCode -ne 0){throw "Setup exited $($result.ExitCode)"}
foreach($name in @('Glassify.exe','GlassifyRecovery.exe','WebView2Loader.dll','web\index.html','web\styles.css','web\app.js','scripts\setup-taskbar.ps1','Uninstall.exe','LICENSE','NOTICE')) {
    if(-not(Test-Path -LiteralPath (Join-Path $testPrefix $name))){throw "Missing installed $name"}
}
$registration=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify'
if($registration.DisplayVersion -ne $Version){throw 'Incorrect uninstall version'}
$startMenu=Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\Glassify\Glassify.lnk'
if(-not(Test-Path -LiteralPath $startMenu)){throw 'Start menu shortcut missing'}
if(Test-Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run") {
    $entry=(Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name Glassify -ErrorAction SilentlyContinue)
    if($entry){throw 'Silent installation unexpectedly enabled startup'}
}
# Run the installed controller with isolated settings and close through its normal IPC.
if($TestStyledPanel){
    & powershell -NoProfile -STA -ExecutionPolicy Bypass -File "$root\scripts\test-web-ui.ps1" -BuildDirectory $testPrefix
    if($LASTEXITCODE -ne 0){throw 'Installed styled panel test failed'}
}
$previous=$env:WINFADEX_DATA_DIR
try {
    $env:WINFADEX_DATA_DIR=Join-Path $root 'out\installer-data'
    New-Item -ItemType Directory -Force $env:WINFADEX_DATA_DIR | Out-Null
    '{"schemaVersion":3,"rules":[],"mouseControl":false}' | Set-Content -LiteralPath "$env:WINFADEX_DATA_DIR\config.json" -Encoding UTF8
    $settingsHash=(Get-FileHash -LiteralPath "$env:WINFADEX_DATA_DIR\config.json").Hash
    'Keep unrelated files when uninstalling.' | Set-Content -LiteralPath "$testPrefix\keep-me.txt"
    $app=Start-Process -FilePath "$testPrefix\Glassify.exe" -ArgumentList '--background' -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 2
    if($app.HasExited){throw 'Installed app failed to remain running'}
    $helper=@(Get-Process GlassifyRecovery -ErrorAction Stop | Where-Object {$_.Path -eq "$testPrefix\GlassifyRecovery.exe"})
    if($helper.Count -ne 1){throw 'Installed recovery helper did not start'}
    # Uninstall while running validates its graceful shutdown route.
    $result=Start-Process -FilePath "$testPrefix\Uninstall.exe" -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
    if($result.ExitCode -ne 0){throw "Uninstaller exited $($result.ExitCode)"}
    if(-not $app.WaitForExit(8000)){throw 'Uninstaller did not stop the running app'}
    $deadline=[DateTime]::UtcNow.AddSeconds(8)
    while((Test-Path -LiteralPath "$testPrefix\Uninstall.exe") -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
    if(Test-Path -LiteralPath "$testPrefix\Uninstall.exe"){throw 'Uninstaller did not remove itself'}
    if(-not(Test-Path -LiteralPath "$testPrefix\keep-me.txt")){throw 'Uninstaller removed an unrelated file'}
    if((Get-FileHash -LiteralPath "$env:WINFADEX_DATA_DIR\config.json").Hash -ne $settingsHash){throw 'Preferences changed during uninstall'}
} finally { $env:WINFADEX_DATA_DIR=$previous }
if(Test-Path -LiteralPath "$testPrefix\Glassify.exe"){throw 'Controller still installed'}
if(Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify'){throw 'Uninstall registration retained'}
if(Test-Path -LiteralPath $startMenu){throw 'Start menu shortcut retained'}
Write-Output 'PASS: per-user install, version/shortcuts, startup off, installed helper, graceful running-app uninstall, registration cleanup.'
