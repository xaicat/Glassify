# Interface development

Glassify hosts local HTML, CSS and JavaScript in WebView2. The native C++ controller owns configuration and window effects. The interface sends named, validated actions and receives state snapshots.

Light is the default. VS Code-inspired charcoal, Midnight and Ember are additional color themes. Minimal and Glass are separate surface styles. Keep control labels short, explanations optional and keyboard focus visible. Primary control pages should fit at 1100 by 760; narrow windows must remain usable with scrolling.

Instant reopen preloads and retains a hidden page. The initial browser surface stays hidden until navigation succeeds and the frontend acknowledges its rendered model. Low memory closes the page after 3.5 seconds hidden. Do not reintroduce an empty about:blank frame during reopening.

Navigation, popups and permissions are restricted. The author link opens a fixed GitHub URL through a native action; arbitrary URLs or shell commands are not accepted by the bridge. DevTools is enabled only by the styled test environment.

Run scripts/test-web-ui.ps1 -Capture to refresh controlled screenshots. Optional Playwright checks cover all pages, four themes, two styles and three widths, plus overflow, focus, icon bounds, setup state and tray behavior. Review rendered pages as well as source checks.
