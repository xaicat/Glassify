# Contributing

Contributions are welcome: bug fixes, compatibility reports, accessibility work, tests and documentation.

## Development

Read the [build guide](docs/BUILDING.md) and [architecture](docs/ARCHITECTURE.md). Use a branch for one focused change. Explain the problem, resulting behavior and validation in the pull request.

Run the relevant tests and format C++ with .clang-format. Changes to rule precedence, configuration, window identity or restoration need regression coverage. Test window effects with controlled fixtures and an isolated WINFADEX_DATA_DIR, not personal apps.

## Reports

Include the Glassify version, Windows build, affected application, reproduction steps and restoration behavior. A successful API call alone does not establish visual compatibility. Review screenshots and logs for personal data before sharing them.

Report security issues using the process in [SECURITY.md](SECURITY.md).

## Project boundaries

- Record recovery state before changing a foreign window.
- Validate window and process identity before applying or restoring changes.
- Preserve external changes and excluded surfaces.
- Keep the native engine separate from the local interface.
- Keep Light/VS Code/Midnight/Ember themes independent from Minimal/Glass styles.
- Prefer compact controls, short labels, visible keyboard focus and optional help.
- Do not add telemetry, a network service or automatic updates without a project discussion.
- Retain required license and dependency notices.

Do not commit credentials, local settings, build output, developer tools or screenshots of personal desktops. Contributions use the repository's license. Be respectful, provide reproducible reports and keep reviews focused on the work.
