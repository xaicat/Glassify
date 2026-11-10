# Taskbar background transparency

Taskbar customization changes only the background through an optional, unmodified TranslucentTB companion. Icons and labels remain opaque.

## Use it

1. Open Taskbar and choose Set up engine. This downloads the pinned official portable engine and verifies its SHA-256.
2. If setup reports missing Windows runtime components, follow the official TranslucentTB installation instructions, quit that app, then retry. The engine needs the x64 Microsoft.UI.Xaml 2.8 package, version 8.2501.31001.0 or newer. Glassify does not silently install runtime packages.
3. Enable Transparent taskbar background.
4. Choose Background opacity and Apply background. 0% is clear; 100% is a solid dark background. Icons and labels retain their original opacity.
5. Restore turns the feature off and requests the owned companion to exit. Pause and Quit also request restoration.

Do not run a separate TranslucentTB instance or another taskbar customization tool concurrently. Glassify refuses to take over an existing instance. Its engine and settings are separate from an installed Store copy.

## Implementation

Modern Windows taskbar backgrounds use XAML brushes. Lowering the Shell_TrayWnd alpha also fades icons, which cannot satisfy background-only transparency. TranslucentTB supplies the Explorer hook/XAML diagnostics path. This is an explicit optional dependency, not an undocumented claim of a universal native Windows opacity API.

Glassify writes the managed engine's settings.json with a clear appearance and dark tint alpha, disables dynamic appearance overrides, and lets that engine watch Explorer, taskbar recreation, and display changes. It does not restart the companion whenever an auto-hidden taskbar appears.

The companion starts suspended. Its PID and creation time are registered in shared recovery state before its thread resumes. Restoration validates that PID, creation time, executable path, and tray-window class before requesting WM_CLOSE. The helper attempts the same after a controller crash, including a bounded startup retry. It never kills or changes an independently installed TranslucentTB. Core window/placement restoration runs before waiting for companion startup.

Turning the feature off requests asynchronous exit, so the UI stays responsive. An unexpectedly stopped engine reports failure rather than entering a restart loop. Recovery is best effort; preview Windows builds, Explorer failures, and competing tools can still prevent complete restoration.

## Dependency and provenance

- Project and source: https://github.com/TranslucentTB/TranslucentTB/tree/2026.2
- Release: https://github.com/TranslucentTB/TranslucentTB/releases/tag/2026.2
- Archive: TranslucentTB-portable-x64.zip
- SHA-256: 0dbe8e0255c20e131cde536dcd0ae490d45989a7d360e26d0150dff1922ac420
- License: GPL-3.0, retained by the separate companion. Glassify does not link this code or redistribute the companion binaries in its release packages.
- Configuration reference: https://translucenttb.github.io/config.html

Setup downloads directly from the publisher into %LocalAppData%\Glassify\TaskbarEngine, or the configured data directory. scripts/setup-taskbar.ps1 is reviewable source. No download occurs until setup is requested. Its result is written to taskbar-setup-result.txt. Data and the optional engine are retained on uninstall; quit the feature before deleting its folder.

Schema 3's whole-taskbar setting is disabled during migration. A user must explicitly enable the new background engine. This avoids silently switching an old icon-fading preference to a different shell customization mechanism.

## Local evidence and limits

On build 26300.9711, TranslucentTB 2026.2 started, the Shell_TrayWnd layered state stayed unchanged, normal restoration closed the owned companion, and forced parent termination caused the recovery helper to close it. Screen captures from this execution desktop were inconclusive, so they are not published as visual proof. Visible background coverage, auto-hide, multiple monitors, and Explorer restart remain interactive compatibility checks. Icons are preserved by never applying whole-taskbar alpha.
