# Configuration

Default data: %LocalAppData%\Glassify. Glassify writes schema 4. Unsupported future versions are rejected without overwriting the file. Compatibility readers accept older settings where possible; original files are preserved.

```json
{
  "schemaVersion": 4,
  "uiTheme": "light",
  "uiStyle": "minimal",
  "keepInterfaceReady": true,
  "globalTransparency": false,
  "autoApply": false,
  "mouseControl": true,
  "focusMaximized": false,
  "focusFullscreenOnly": false,
  "focusSameMonitor": true,
  "focusRestoreOnSwitch": true,
  "focusDelayMs": 350,
  "focusExclusions": [],
  "minimizeToTray": true,
  "closeToTray": true,
  "taskbarEnabled": false,
  "taskbarOpacity": 20,
  "powerSaving": true,
  "hotkeyModifiers": 3,
  "hotkeyKey": 80,
  "presetId": "custom",
  "defaults": { "activeOpacity": 95, "inactiveOpacity": 85, "tint": 0, "tintStrength": 12 },
  "rules": []
}
```

uiTheme is light (default), vscode, dark (Midnight) or ember. uiStyle is minimal or glass. keepInterfaceReady defaults to true for fast tray reopening. False releases the browser after 3.5 seconds hidden. globalTransparency is opt-in and uses defaults across eligible apps, taking priority over enabled app appearances while respecting exclusions and disabled apps. Missing optional fields use their defaults. App opacity is 30 to 100, tint strength 0 to 40, and taskbar background opacity 0 to 100. Taskbar settings require the optional engine; they never lower whole-taskbar alpha. focusDelayMs is 150 to 1500. focusExclusions accepts at most 64 executable filenames, matched case-insensitively. The Focus page offers the common delay choices; imported settings can use other values within the range.

A rule uses id, executable, enabled, exclude, activeOpacity, inactiveOpacity, tint, tintStrength, and the opacity-only mode field retained for compatibility. filenameOnly, windowClass, and wildcard titlePattern remain readable; the normal panel saves exact executable paths. Tint indices: 0 None, 1 Blue, 2 Violet, 3 Rose, 4 Amber, 5 Green, 6 Slate, 7 Night.

With Global off, enabled app rules take priority over automatic fallback defaults. With Global on, the shared defaults take priority over enabled app appearances. Exclusions return no effect. A matching disabled app stays outside automatic defaults. Wheel and keyboard overrides are temporary per-window settings and are not serialized. Presets clear temporary overrides and update enabled non-excluded app rules plus defaults.

Limits: 512 saved rules, unique IDs, 256 concurrent recovery records, bounded JSON size/depth, strict types, duplicate-key rejection, and supported schema versions. Unreadable/future configs stay unchanged and become read-only until a valid import.

WINFADEX_DATA_DIR is an absolute data override. WINFADEX_TEST_INSTANCE scopes an isolated development instance and WINFADEX_TEST_TARGET_PID restricts its targets. GLASSIFY_WEB_TEST permits the styled panel in such a test instance. Ordinary launch does not enable a DevTools port. --native explicitly selects native fallback controls. --background starts in the tray; --restore pauses and restores an existing controller; --quit requests graceful exit.

The WebView2 subfolder is a browser profile/cache. TaskbarEngine contains the optional separate companion downloaded by explicit setup. Both are private runtime data. Do not commit them, config files, logs, or real-user screenshots.
