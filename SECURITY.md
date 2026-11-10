# Security

Glassify changes other windows' appearance and may start an optional taskbar companion. It validates process/window identity, records recovery state and avoids secure or inaccessible system surfaces. Restoration is best effort after crashes or external changes.

The interface loads local files through a restricted WebView2 origin. External navigation, popups and permissions are blocked. The native bridge accepts validated commands rather than arbitrary shell input. The optional engine downloads only after explicit setup and is checked against a pinned SHA-256 hash.

For a suspected vulnerability, use GitHub's private vulnerability reporting if enabled. Otherwise open an issue requesting a private reporting channel without exploit details or sensitive data. Include the affected version, Windows build, impact and a minimal reproduction. Do not publish credentials, personal paths or process dumps.

Security fixes target the latest release. Please update before reporting an issue.
