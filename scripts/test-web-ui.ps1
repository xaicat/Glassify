param([string]$BuildDirectory="$PSScriptRoot\..\out\local", [switch]$Capture)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath("$PSScriptRoot\..")
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
$scratch=Join-Path $root ('out\web-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $scratch | Out-Null
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;using System.IO;using System.Net.WebSockets;using System.Text;using System.Threading;using System.Runtime.InteropServices;
public static class GlassifyWebTest {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c,string t);
 public static IntPtr FindTestWindow(){return FindWindowW("WinFadeX.Next.WebTest",null);}
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] static extern bool GetLayeredWindowAttributes(IntPtr h,out uint color,out byte alpha,out uint flags);
 public static int Alpha(IntPtr h){uint c,f;byte a;return GetLayeredWindowAttributes(h,out c,out a,out f)?a:255;}
 public static void Send(ClientWebSocket s,string message){var b=Encoding.UTF8.GetBytes(message);using(var t=new CancellationTokenSource(15000))s.SendAsync(new ArraySegment<byte>(b),WebSocketMessageType.Text,true,t.Token).GetAwaiter().GetResult();}
 public static string Receive(ClientWebSocket s){using(var m=new MemoryStream())using(var t=new CancellationTokenSource(15000)){var b=new byte[65536];WebSocketReceiveResult r;do{r=s.ReceiveAsync(new ArraySegment<byte>(b),t.Token).GetAwaiter().GetResult();if(r.MessageType==WebSocketMessageType.Close)throw new Exception("DevTools connection closed");m.Write(b,0,r.Count);if(m.Length>16000000)throw new Exception("DevTools response exceeds limit");}while(!r.EndOfMessage);return Encoding.UTF8.GetString(m.ToArray());}}
}
'@
$listener=New-Object Net.Sockets.TcpListener ([Net.IPAddress]::Loopback),0
$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$previous=@{}
foreach($key in @('WINFADEX_DATA_DIR','WINFADEX_TEST_INSTANCE','WINFADEX_TEST_TARGET_PID','GLASSIFY_WEB_TEST','WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
$env:WINFADEX_DATA_DIR=$scratch
$env:WINFADEX_TEST_INSTANCE='WebTest'
$env:WINFADEX_TEST_TARGET_PID="$PID"
$env:GLASSIFY_WEB_TEST='1'
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--remote-debugging-port=$port"
'{"schemaVersion":4,"rules":[],"mouseControl":false}' | Set-Content -LiteralPath "$scratch\config.json" -Encoding UTF8
$form=New-Object Windows.Forms.Form
$form.Text='Glassify controlled UI fixture';$form.Width=420;$form.Height=250
$form.ShowInTaskbar=$true;$form.Show();[Windows.Forms.Application]::DoEvents()
$socket=$null;$app=$null;$controller=[IntPtr]::Zero;$sequence=0
function Invoke-CDP($method,$parameters){
    $script:sequence++
    $id=$script:sequence
    $payload=@{id=$id;method=$method;params=$parameters}|ConvertTo-Json -Depth 20 -Compress
    [GlassifyWebTest]::Send($socket,$payload)
    do{$response=[GlassifyWebTest]::Receive($socket)|ConvertFrom-Json}while($response.id -ne $id)
    if($response.error){throw ($response.error|ConvertTo-Json -Compress)}
    $response.result
}
function Evaluate($expression){
    [Windows.Forms.Application]::DoEvents()
    $r=Invoke-CDP 'Runtime.evaluate' @{expression=$expression;returnByValue=$true}
    if($r.exceptionDetails){throw ($r.exceptionDetails|ConvertTo-Json -Depth 6 -Compress)}
    $r.result.value
}
function Await-JS($expression,$message){
    $deadline=[DateTime]::UtcNow.AddSeconds(12)
    do{if(Evaluate $expression){return};Start-Sleep -Milliseconds 80}while([DateTime]::UtcNow -lt $deadline)
    throw $message
}
function Capture-Page($name){
    if(-not $Capture){return}
    Start-Sleep -Milliseconds 180
    $result=Invoke-CDP 'Page.captureScreenshot' @{format='png';captureBeyondViewport=$false}
    [IO.File]::WriteAllBytes((Join-Path $root "docs\images\$name.png"),[Convert]::FromBase64String($result.data))
}
try{
    $app=Start-Process -FilePath "$build\Glassify.exe" -ArgumentList '--background' -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(12)
    do{$controller=[GlassifyWebTest]::FindTestWindow();if($controller -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $deadline)
    if($controller -eq [IntPtr]::Zero){throw 'Web test controller did not start'}
    [GlassifyWebTest]::ShowWindow($controller,4)|Out-Null
    $target=$null;$deadline=[DateTime]::UtcNow.AddSeconds(25)
    do{
        [Windows.Forms.Application]::DoEvents()
        try{$targets=Invoke-RestMethod "http://127.0.0.1:$port/json/list" -TimeoutSec 1;$target=$targets|Where-Object {$_.type -eq 'page' -and $_.url -like 'https://glassify.local/*'}|Select-Object -First 1}catch{}
        if($target){break};Start-Sleep -Milliseconds 100
    }while([DateTime]::UtcNow -lt $deadline)
    if(-not $target){throw 'WebView2 page did not start. Install Microsoft WebView2 Runtime.'}
    $socket=New-Object Net.WebSockets.ClientWebSocket
    $socket.ConnectAsync([Uri]$target.webSocketDebuggerUrl,[Threading.CancellationToken]::None).GetAwaiter().GetResult()|Out-Null
    Await-JS 'window.glassify && !!window.glassify.getState()' 'Frontend did not receive native state'
    if((Evaluate 'document.documentElement.dataset.theme') -ne 'light'){throw 'Light theme is not the default'}
    Await-JS 'window.glassify.getPage() === "global"' 'Global controls are not the opening page'
    if(Evaluate 'window.glassify.getState().config.focusMaximized'){throw 'Focus minimization must default to off'}
    Evaluate 'document.getElementById("global-active-number").value=80;document.getElementById("global-inactive-number").value=80;document.querySelector("[data-action=saveGlobal]").click();'|Out-Null
    Await-JS 'window.glassify.getState().config.defaults.activeOpacity === 80' 'Global opacity was not saved'
    Evaluate 'var g=document.querySelector("[data-toggle=globalTransparency]");g.checked=true;g.dispatchEvent(new Event("change",{bubbles:true}));'|Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do{[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 60}while([GlassifyWebTest]::Alpha($form.Handle) -ne 204 -and [DateTime]::UtcNow -lt $deadline)
    if([GlassifyWebTest]::Alpha($form.Handle) -ne 204){throw 'Global switch did not affect an existing window'}
    $second=New-Object Windows.Forms.Form
    try{
        $second.Text='Glassify new global test window';$second.Show();[Windows.Forms.Application]::DoEvents()
        $deadline=[DateTime]::UtcNow.AddSeconds(5)
        do{[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 60}while([GlassifyWebTest]::Alpha($second.Handle) -ne 204 -and [DateTime]::UtcNow -lt $deadline)
        if([GlassifyWebTest]::Alpha($second.Handle) -ne 204){throw 'Global switch did not cover a newly opened app window'}
    }finally{$second.Close();$second.Dispose()}
    Capture-Page 'global'
    Evaluate 'var g=document.querySelector("[data-toggle=globalTransparency]");g.checked=false;g.dispatchEvent(new Event("change",{bubbles:true}));'|Out-Null
    Await-JS '!window.glassify.getState().config.globalTransparency' 'Global switch did not turn off'
    [GlassifyWebTest]::PostMessageW($controller,0x312,[IntPtr]10,[IntPtr]::Zero)|Out-Null
    Await-JS 'window.glassify.getState().config.globalTransparency' 'Global toggle shortcut did not turn on'
    [GlassifyWebTest]::PostMessageW($controller,0x312,[IntPtr]10,[IntPtr]::Zero)|Out-Null
    Await-JS '!window.glassify.getState().config.globalTransparency' 'Global toggle shortcut did not turn off'
    [GlassifyWebTest]::PostMessageW($controller,0x312,[IntPtr]9,[IntPtr]::Zero)|Out-Null
    Await-JS 'window.glassify.getState().config.focusMaximized' 'Focus toggle shortcut did not turn on'
    [GlassifyWebTest]::PostMessageW($controller,0x312,[IntPtr]9,[IntPtr]::Zero)|Out-Null
    Await-JS '!window.glassify.getState().config.focusMaximized' 'Focus toggle shortcut did not turn off'
    Evaluate 'document.querySelector("[data-page=windows]").click()'|Out-Null
    Await-JS 'document.querySelectorAll("[data-window]").length > 0' 'Controlled window not listed'
    Evaluate 'document.querySelector("[data-window]").click()'|Out-Null
    Await-JS '!!document.getElementById("opacity-active")' 'Window selection did not reach native editor'
    Evaluate 'for(const key of ["opacity-active","opacity-inactive"]){let e=document.getElementById(key);e.value=85;e.dispatchEvent(new Event("input",{bubbles:true}));}'|Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(6)
    do{[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 80}while([GlassifyWebTest]::Alpha($form.Handle) -ne 217 -and [DateTime]::UtcNow -lt $deadline)
    if([GlassifyWebTest]::Alpha($form.Handle) -ne 217){throw 'HTML sliders did not apply real opacity'}
    Evaluate 'document.querySelector("[data-action=saveApp]").click()'|Out-Null
    Await-JS 'window.glassify.getState().rules.length === 1' 'HTML save did not persist native rule'
    Evaluate 'let e=document.getElementById("opacity-active");e.value=60;e.dispatchEvent(new Event("input",{bubbles:true}));'|Out-Null
    Await-JS 'window.glassify.getState().previewActive' 'Preview did not start'
    Evaluate 'document.querySelector("[data-action=undo]").click()'|Out-Null
    Await-JS 'document.getElementById("opacity-active-number").value === "85" && !window.glassify.getState().previewActive' 'Undo did not synchronize the selected editor'
    Capture-Page 'windows'
    foreach($page in @('presets','focus','automation','taskbar','settings')){
        Evaluate ('document.querySelector("[data-page='+$page+']").click()')|Out-Null
        Await-JS ('window.glassify.getPage() === "'+$page+'"') 'Page navigation failed'
        Capture-Page $page
    }
    if($Capture){
        foreach($style in @('glass')){
            Evaluate ('document.querySelector("[data-style-choice='+$style+']").click()')|Out-Null
            Await-JS ('document.documentElement.dataset.style === "'+$style+'"') 'Interface style did not apply'
            Evaluate 'document.querySelector("[data-page=global]").click()'|Out-Null
            Capture-Page ('global-'+$style)
            Evaluate 'document.querySelector("[data-page=settings]").click()'|Out-Null
        }
        Evaluate 'document.querySelector("[data-style-choice=minimal]").click()'|Out-Null
        Await-JS 'document.documentElement.dataset.style === "minimal"' 'Minimal style did not restore'
    }
    if($Capture){
        foreach($theme in @('vscode','ember','dark')){
            Evaluate ('document.querySelector("[data-theme-choice='+$theme+']").click()')|Out-Null
            Await-JS ('document.documentElement.dataset.theme === "'+$theme+'"') 'Color theme did not apply'
            Capture-Page ('settings-'+$theme)
            Evaluate 'document.querySelector("[data-page=global]").click()'|Out-Null
            Capture-Page ('global-'+$theme)
            Evaluate 'document.querySelector("[data-page=settings]").click()'|Out-Null
        }
    }
    Evaluate 'document.querySelector("[data-theme-choice=light]").click()'|Out-Null
    Await-JS 'document.documentElement.dataset.theme === "light"' 'Light theme did not apply'
    $config=Get-Content -LiteralPath "$scratch\config.json" -Raw|ConvertFrom-Json
    if($config.uiTheme -ne 'light'){throw 'Light theme was not persisted'}
    Capture-Page 'settings-light'
    Evaluate 'document.querySelector("[data-page=windows]").click()'|Out-Null
    Capture-Page 'windows-light'
    $outline=Evaluate 'let b=document.querySelector(".nav-item");b.focus();getComputedStyle(b).outlineStyle'
    if($outline -eq 'dotted'){throw 'Dotted focus indicator returned'}
    # Unknown bridge commands and out-of-range defaults must not mutate configuration.
    Evaluate 'chrome.webview.postMessage({action:"saveDefaults",active:0,inactive:85,tint:0,strength:12})'|Out-Null
    Start-Sleep -Milliseconds 150
    $config=Get-Content -LiteralPath "$scratch\config.json" -Raw|ConvertFrom-Json
    if($config.defaults.activeOpacity -lt 30){throw 'Invalid UI input bypassed native validation'}
    Evaluate 'document.querySelector("[data-page=focus]").click();document.querySelector("[data-focus-setting=focusDelayMs]").value="600";document.querySelector("[data-focus-setting=focusDelayMs]").dispatchEvent(new Event("change",{bubbles:true}));'|Out-Null
    Await-JS 'window.glassify.getState().config.focusDelayMs === 600' 'Focus delay setting did not reach engine'
    $node=(Get-Command node -ErrorAction SilentlyContinue).Source
    $playwright=if($env:GLASSIFY_PLAYWRIGHT_PATH){$env:GLASSIFY_PLAYWRIGHT_PATH}else{Join-Path $root '.tools\browser-tools\node_modules\playwright'}
    if($node -and (Test-Path -LiteralPath $playwright)){
        & $node (Join-Path $root 'scripts\audit-styled-ui.cjs') "http://127.0.0.1:$port"
        if($LASTEXITCODE -ne 0){throw 'Playwright audit failed'}
    }
    # Warm tray reopening must preserve the existing document, not rebuild it.
    Evaluate 'window.__trayMarker = "glassify-warm-document"'|Out-Null
    $reopenTimes=@()

    foreach($delay in @(200,4200,4200)){
        Evaluate 'document.querySelector("[data-action=hide]").click()'|Out-Null
        $socket.Dispose();$socket=$null
        Start-Sleep -Milliseconds $delay
        $reopenWatch=[Diagnostics.Stopwatch]::StartNew()
        [GlassifyWebTest]::ShowWindow($controller,9)|Out-Null
        $target=$null;$deadline=[DateTime]::UtcNow.AddSeconds(25)
        do{
            try{$target=Invoke-RestMethod "http://127.0.0.1:$port/json/list" -TimeoutSec 1|Where-Object {$_.type -eq 'page' -and $_.url -like 'https://glassify.local/*'}|Select-Object -First 1}catch{}
            if($target){break};Start-Sleep -Milliseconds 100
        }while([DateTime]::UtcNow -lt $deadline)
        if(-not $target){throw 'Styled panel did not reopen from tray'}
        $socket=New-Object Net.WebSockets.ClientWebSocket
        $socket.ConnectAsync([Uri]$target.webSocketDebuggerUrl,[Threading.CancellationToken]::None).GetAwaiter().GetResult()|Out-Null
        Await-JS 'window.glassify && !!window.glassify.getState() && document.querySelectorAll(".nav-item").length === 7' 'Restored panel is empty'
        Await-JS 'window.glassify.getState().interfaceReady && document.visibilityState === "visible"' 'Restored interface was not presented'
        if((Evaluate 'window.__trayMarker') -ne 'glassify-warm-document'){throw 'Instant reopen rebuilt the document'}
        $reopenWatch.Stop();$reopenTimes+=$reopenWatch.Elapsed.TotalMilliseconds

        [GlassifyWebTest]::ShowWindow($controller,6)|Out-Null
        [GlassifyWebTest]::ShowWindow($controller,9)|Out-Null
        Await-JS 'document.visibilityState === "visible"' 'Minimized panel did not become visible'
    }
    # Low-memory mode is explicit and still has a themed, ready-gated cold restore.
    Evaluate 'document.querySelector("[data-page=settings]").click();let t=document.querySelector("[data-tray-mode]");t.value="false";t.dispatchEvent(new Event("change",{bubbles:true}));'|Out-Null
    Await-JS '!window.glassify.getState().config.keepInterfaceReady' 'Low-memory option did not save'
    Evaluate 'document.querySelector("[data-action=hide]").click()'|Out-Null
    $socket.Dispose();$socket=$null
    Start-Sleep -Milliseconds 4500
    [GlassifyWebTest]::ShowWindow($controller,9)|Out-Null
    $target=$null;$deadline=[DateTime]::UtcNow.AddSeconds(25)
    do{
        try{$target=Invoke-RestMethod "http://127.0.0.1:$port/json/list" -TimeoutSec 1|Where-Object {$_.type -eq 'page' -and $_.url -like 'https://glassify.local/*'}|Select-Object -First 1}catch{}
        if($target){break};Start-Sleep -Milliseconds 80
    }while([DateTime]::UtcNow -lt $deadline)
    if(-not $target){throw 'Low-memory panel did not reopen'}
    $socket=New-Object Net.WebSockets.ClientWebSocket
    $socket.ConnectAsync([Uri]$target.webSocketDebuggerUrl,[Threading.CancellationToken]::None).GetAwaiter().GetResult()|Out-Null
    Await-JS 'window.glassify && !!window.glassify.getState() && window.glassify.getState().interfaceReady' 'Cold restore did not wait for the themed interface'
    if(Evaluate 'window.__trayMarker === "glassify-warm-document"'){throw 'Low-memory mode retained the old document'}
    Write-Output ('Warm reopen to visible state including CDP reconnect (ms): '+($reopenTimes -join ', '))
    [ordered]@{measuredAt=(Get-Date).ToUniversalTime().ToString('o');warmReopenMs=@($reopenTimes);sameDocumentRetained=$true;lowMemoryDocumentRecreated=$true;method='ShowWindow to visible ready web state, including localhost CDP reconnect; not optical first-paint or frame-time measurement'}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $root 'out\reopen-ui-latest.json') -Encoding UTF8
    Write-Output 'PASS: real global/per-app controls, themes, Focus/Global shortcuts, warm tray document retention, low-memory disposal and ready-gated restore.'

}finally{
    if($socket){$socket.Dispose()}
    if($controller -eq [IntPtr]::Zero){$controller=[GlassifyWebTest]::FindTestWindow()}
    if($controller -ne [IntPtr]::Zero){[GlassifyWebTest]::PostMessageW($controller,0x111,[IntPtr]142,[IntPtr]::Zero)|Out-Null}
    if($app){$app.WaitForExit(8000)|Out-Null}
    $form.Close();$form.Dispose()
    foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}
