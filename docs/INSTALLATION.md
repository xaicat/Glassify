# Installation

## Requirements

Windows 10 or 11 x64 and an ordinary user account. The styled panel needs Microsoft WebView2 Runtime; native fallback controls work without it. No Node.js, service or separate Visual C++ runtime installation is needed for the supplied binaries.

## Installer

1. Download Glassify-1.0.1-windows-x64-setup.exe and SHA256SUMS.txt from the release page.
2. Run Get-FileHash .\Glassify-1.0.1-windows-x64-setup.exe -Algorithm SHA256 and compare the result.
3. Quit any running Glassify copy through Quit and restore.
4. Run Setup. The initial binaries are unsigned; Windows may display an unrecognized-app warning.
5. Choose an installation folder. The default is %LocalAppData%\Programs\Glassify.
6. Desktop shortcut and launch-at-sign-in are optional and initially unchecked.
7. Install and open Glassify from Start.

The installer adds an Installed apps entry for your account and does not require administrator access.

## Portable ZIP

1. Download Glassify-1.0.1-windows-x64.zip and verify its checksum.
2. Extract the entire ZIP with Extract All.
3. Run Glassify.exe from the extracted folder.
4. Keep the helper, WebView2Loader.dll, web assets and notices together. Do not copy only the controller or run it inside the ZIP.

Portable means no installation. Both formats store preferences in %LocalAppData%\Glassify. An absolute WINFADEX_DATA_DIR environment variable can select a separate data folder.

## Remove

Quit and restore before deleting a portable folder. For an installed copy, use Windows Settings > Apps > Installed apps > Glassify > Uninstall. The uninstaller retains preferences and the optional taskbar engine. To remove those too, quit Glassify and delete its dedicated data folder after backing up anything you need.

## First use

Enable Global transparency and apply focused/background opacity. Turn Global off to use per-app rules. Focus minimization is off by default; Ctrl+Alt+F toggles it. Taskbar background control requires one-time engine setup. Details are in USER-GUIDE.md and TASKBAR.md.
