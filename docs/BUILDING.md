# Build and test

## Requirements

- Windows x64, CMake 3.24+ and a C++20 compiler.
- Visual Studio 2022 with Desktop development with C++ and a Windows SDK, or LLVM-MinGW and Ninja.
- NSIS 3 for the installer. Version 3.13 was used for the initial release.
- WebView2 Runtime for styled desktop tests.

JsonCpp and the WebView2 SDK header/loader are vendored. Configuration and compilation do not download dependencies.

## MSVC

From a Visual Studio developer PowerShell:

```powershell
cmake --preset msvc
cmake --build --preset msvc
ctest --preset msvc
```

The preset builds Release x64 under out/msvc. For LLVM-MinGW, configure a Ninja Release build with clang++ and windres from that toolchain.

## Tests

Core tests cover rules, configuration and JSONC editing. Desktop tests exercise window transactions, the native UI and the actual WebView2 interface. They require an interactive Windows desktop with DWM. Quit other transparency controllers before running them. Test settings and target windows are isolated.

Run desktop workflows on an interactive Windows session. Build success alone does not establish visual compatibility.

Optional browser audits use Node.js and Playwright. Install Playwright under .tools/browser-tools, put Node on PATH, or set GLASSIFY_PLAYWRIGHT_PATH to the installed module directory. The styled workflow detects it automatically. No Node dependency ships with Glassify.

## Packaging

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -BuildDirectory out/msvc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installer.ps1 -MakeNSIS 'C:\path\to\NSIS\makensis.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-installer.ps1 -TestStyledPanel
```

Use a fresh package stage; package.ps1 refuses to reuse one. Both package scripts write SHA-256 siblings. The installer smoke test uses a fresh directory, checks registration/shortcuts, runs the installed UI when requested and verifies graceful uninstall with preferences retained. Do not run it over an existing registered installation.

## Source layout

src/core contains rules/configuration; src/platform handles windows, overlays, focus and taskbar control; src/recovery contains the restoration protocol/helper; src/app hosts the panel and tray. web contains local HTML/CSS/JavaScript. tests, scripts, resources and packaging contain the development and distribution tools.

Use clang-format for authored C++ and Prettier for web source. Keep vendored source unchanged unless updating its pinned dependency and notices.
