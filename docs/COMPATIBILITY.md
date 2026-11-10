# Compatibility

Glassify targets ordinary Windows 10/11 x64 desktop windows. Initial local validation used Windows build 26300.9711 and WebView2 Runtime 157.0.4341.42. Compatibility varies by app and Windows build.

Whole-window opacity includes text and controls. Secure/system surfaces, inaccessible elevated processes, unresponsive/cloaked windows, private-DC rendering and exclusive full-screen games are excluded. Apps may reset their own appearance; Glassify yields to detected conflicts rather than repeatedly forcing changes.

Focus mode supports normal maximized and borderless full-screen windows. It respects pre-existing minimized state, manual restoration, display scope and app exclusions. Cross-monitor DPI, virtual desktops and physical-input behavior need broader hardware testing.

Taskbar support depends on the optional TranslucentTB engine and its runtime requirements. It changes background brushes without lowering whole-taskbar alpha. Preview Windows builds, Explorer changes, auto-hide and secondary taskbars can differ. See TASKBAR.md for dependencies and limitations.

Recovery checks identity and ownership before restoring changes. It cannot guarantee restoration after every external change, system failure or shutdown. Avoid competing transparency/taskbar tools.

Instant reopen keeps the hidden browser interface resident. Low memory releases it and requires a short reconstruction on opening. Neither idle CPU samples nor native alpha checks establish GPU frame pacing or power consumption.
