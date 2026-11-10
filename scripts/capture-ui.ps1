param(
    [string]$BuildDirectory = "$PSScriptRoot\..\out\local",
    [string]$OutputDirectory = "$PSScriptRoot\..\docs\images"
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$scratch = [IO.Path]::GetFullPath("$PSScriptRoot\..\out\capture-data")
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
'{"schemaVersion":2,"rules":[],"mouseControl":false}' | Set-Content -LiteralPath "$scratch\config.json" -Encoding UTF8
$previousData = $env:WINFADEX_DATA_DIR
$previousInstance = $env:WINFADEX_TEST_INSTANCE
$previousPid = $env:WINFADEX_TEST_TARGET_PID
$env:WINFADEX_DATA_DIR = $scratch
$env:WINFADEX_TEST_INSTANCE = 'Capture'
$env:WINFADEX_TEST_TARGET_PID = "$PID"
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Threading;
public static class WfxCapture {
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowW(string c,string t);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int n);
    [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr h,int id);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,ChildProc p,IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect r);
    [DllImport("gdi32.dll")] static extern int SaveDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern bool RestoreDC(IntPtr dc,int saved);
    [DllImport("gdi32.dll")] static extern bool SetViewportOrgEx(IntPtr dc,int x,int y,IntPtr previous);
    [DllImport("user32.dll")] static extern int MapWindowPoints(IntPtr from,IntPtr to,ref Rect rect,uint count);
    delegate bool ChildProc(IntPtr h,IntPtr l);
    static IntPtr Window() { return FindWindowW("WinFadeX.Next.Capture",null); }
    public static void Wait() {
        var deadline=DateTime.UtcNow.AddSeconds(8);
        while((Window()==IntPtr.Zero || GetDlgItem(Window(),142)==IntPtr.Zero) && DateTime.UtcNow<deadline) Thread.Sleep(20);
        if(Window()==IntPtr.Zero) throw new Exception("Capture instance did not start.");
    }
    public static void Save(int page,string file) {
        var h=Window(); ShowWindow(h,4);
        SendMessageW(h,0x111,new IntPtr(300+page),IntPtr.Zero);
        Rect r;GetClientRect(h,out r);
        using(var b=new Bitmap(r.Right,r.Bottom)) using(var g=Graphics.FromImage(b)) {
            var dc=g.GetHdc(); bool ok=PrintWindow(h,dc,1);
            // Capture controls explicitly: native PrintWindow coverage varies by Windows build.
            EnumChildWindows(h,(child,data)=>{
                if(!IsWindowVisible(child))return true;
                Rect bounds;GetWindowRect(child,out bounds);MapWindowPoints(IntPtr.Zero,h,ref bounds,2);
                int saved=SaveDC(dc);SetViewportOrgEx(dc,bounds.Left,bounds.Top,IntPtr.Zero);
                SendMessageW(child,0x317,dc,new IntPtr(0x1c));RestoreDC(dc,saved);return true;
            },IntPtr.Zero);
            g.ReleaseHdc(dc);
            if(!ok) throw new Exception("Native client capture failed.");
            b.Save(file);
        }
    }
    public static void Quit() { var h=Window();if(h!=IntPtr.Zero)PostMessageW(h,0x111,new IntPtr(142),IntPtr.Zero); }
}
'@
try {
    $app = Start-Process -FilePath "$build\Glassify.exe" -ArgumentList '--background' -WindowStyle Hidden -PassThru
    [WfxCapture]::Wait()
    $names = @('windows','presets','automation','settings','taskbar')
    for ($page=0; $page -lt $names.Count; $page++) {
        [WfxCapture]::Save($page,(Join-Path $output "$($names[$page]).png"))
    }
    Write-Output "Captured five isolated client views in $output"
} finally {
    [WfxCapture]::Quit()
    if ($app) { $app.WaitForExit(5000) | Out-Null }
    $env:WINFADEX_DATA_DIR = $previousData
    $env:WINFADEX_TEST_INSTANCE = $previousInstance
    $env:WINFADEX_TEST_TARGET_PID = $previousPid
}
