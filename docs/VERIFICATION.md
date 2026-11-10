# Release verification

## Glassify 1.0.0

Local validation on 10 November 2026 used Windows x64 build 26300.9711, WebView2 Runtime 157.0.4341.42, Clang 23.1.5/LLVM-MinGW and CMake 4.4.5. The release binaries use an optimized x64 build. NSIS 3.13 produces the per-user installer.

- All four CTest suites passed in 51.20 seconds: window transactions, native UI, styled UI and core rules/configuration.
- Command-line tests cover trailing whitespace in quit/background requests.
- Controlled existing and newly opened windows, global/per-app opacity, save/Undo, Focus/Global command dispatch and rejected bridge input passed.
- Playwright checked seven pages, four themes, both styles and three widths. Checks cover overflow, compact default pages, icon bounds, setup state and visible keyboard focus.
- Instant reopen retained the same page across three hidden intervals. Low memory released it and reconstructed a ready-gated page.
- The installed build passed the styled workflow and full browser matrix. Per-user version/shortcuts, startup off by default, helper startup, graceful uninstall while running, retained preferences/unrelated files and registration cleanup passed.
- Screenshots use controlled fixtures. Tests use isolated settings and target-process restrictions; normal app launch does not enable DevTools.
- Separate process-resource samples are documented in PERFORMANCE.md. They do not establish GPU, power or frame-time performance.

Desktop tests require an interactive Windows session. The release was validated locally with LLVM-MinGW; hosted MSVC checks are not included in this release verification. The optional taskbar engine still needs broader Windows-build, multi-monitor and auto-hide coverage. See COMPATIBILITY.md for scope and limitations.
