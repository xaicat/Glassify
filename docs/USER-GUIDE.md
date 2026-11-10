# Glassify user guide

## Global controls

Global is the opening page. Turn on Make all apps transparent to affect existing and newly opened eligible app windows. Set Focused window and Background windows, then Apply global opacity. Ctrl+Alt+G toggles this mode. Saved app rules remain intact but enabled rules use the global appearance while this mode is on. Excluded and disabled apps stay unchanged. Temporary mouse/window overrides take priority. Switching Global on or off clears existing temporary overrides. Turn Global off to return to app-specific settings. Pause effects restores all managed windows.

## Window controls

Open Windows and select an eligible window. Pick a window gives you three seconds to switch to another app. Refresh reloads discovery. A clear empty state appears if no compatible window is available; elevated, secure, unresponsive, or incompatible surfaces cannot be controlled.

Focused opacity applies while working in that window. Background opacity applies when another window has focus. Lower means more transparent; app values range from 30% to 100%. Text and controls fade with the whole app. Optional tint adds a click-through color layer and also affects content.

Slider and tint edits create a timed preview. Preview 15s restarts it; Undo or timeout restores the previous appearance. Save for app persists values for matching future windows. Keep window affects only the individual open window until it closes, Glassify exits, or a preset clears the override. Always opaque excludes the app from automatic settings and shortcuts.

Saved apps can be selected for editing or removed. Removing a rule allows automatic defaults to apply if Automation is enabled. A disabled matching rule stays outside automatic defaults.

## Presets and automation

Presets offer twelve coordinated opacity/tint choices. They update enabled saved app rules and the automatic default. Excluded apps stay opaque; temporary window overrides are cleared.

Automation controls automatic defaults, Ctrl+Alt+wheel adjustment, minimize-to-tray, close-to-tray, and power-saving behavior. Save defaults changes the fallback appearance for eligible apps without an explicit rule. Automatic defaults and focus mode are initially off.

## Focus

Focus > Minimize other windows is off by default. Ctrl+Alt+F toggles it on or off. Turning it off restores the windows Glassify minimized. Leave it off when you need multiple maximized windows together.

Enable Focus to make room when a normal app is maximized or borderless full screen. Choose maximized/full-screen or full-screen-only, a settling delay, same-display or all-display scope, and restoration when switching away. The default is 350 ms and same display. Use Apps to leave alone for executable filenames, separated by commas. Saved transparency exclusions are also respected.

Menus and Glassify's panel do not cancel an active valid session. Already minimized windows are not added to the restore set. Manually restored windows stay available for the current session. Same-scope transfers avoid bringing every window back before minimizing it again. Show Desktop is not immediately undone.

Restore now or Ctrl+Alt+M returns tracked windows and suspends the session until you choose another eligible app. Pause and Quit restore tracked placements too. Exclusive full-screen games remain unsupported.

## Taskbar

The taskbar feature changes its background while keeping icons solid. Icons and labels retain full opacity. In Taskbar, Set up engine downloads a pinned, hash-verified TranslucentTB companion. Enable, choose 0% to 100% background opacity, and Apply background. Restore disables it and requests the owned engine to exit.

This engine uses Explorer hooks/XAML diagnostics and its own GPL-3.0 license. It is optional, separately downloaded, and can be incompatible with preview Windows builds. See TASKBAR.md for runtime dependencies, ownership and recovery. Do not run another taskbar customization tool at the same time.

## Appearance, tray and shortcuts

Settings offers Light (default), VS Code-inspired charcoal, Midnight and orange-accented Ember themes. Choose Minimal or Glass independently. Glass adds a frosted treatment inside this panel; it does not add blur to other apps. Minimal uses the fewest visual effects. Explanations live in hover help and collapsed How it works sections. Selection and keyboard focus use solid indications. The HTML/CSS panel uses Microsoft WebView2 Runtime; native fallback is available if missing or with --native. If the panel cannot open, native controls include Reload interface to retry.

Hide to tray keeps native effects running. Instant reopen retains the hidden web interface and its memory by default. Settings > Tray behavior > Low memory closes it after 3.5 seconds hidden. Double-click the tray icon or press Ctrl+Alt+H to reopen it. Closing/minimizing follows your Automation preferences. Quit and restore stops the controller and requests owned effects to restore.

| Control | Action |
| --- | --- |
| Ctrl+Alt+P | Pause/resume; configurable |
| Ctrl+Alt+Up / Down | Adjust focused window by 5 points |
| Ctrl+Alt+scroll | Adjust window under pointer |
| Ctrl+Alt+T | Toggle focused window effects |
| Ctrl+Alt+Left / Right | Previous/next preset |
| Ctrl+Alt+H | Show/hide panel |
| Ctrl+Alt+F | Toggle Focus mode |
| Ctrl+Alt+G | Toggle global transparency |
| Ctrl+Alt+M | Restore/suspend focus session |

Settings includes import/export, data folder, startup and recovery controls. The data folder defaults to %LocalAppData%\Glassify. An invalid or future config stays unchanged until a valid import. Read INSTALLATION.md for upgrades and uninstall, and PERFORMANCE.md for measured costs and limits.
