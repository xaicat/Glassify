# Prepare a release

1. Set the version in CMakeLists.txt, resources/app.rc, resources/app.manifest, packaging/installer.nsi and package/test script defaults. Update the README and changelog.
2. Build Release x64 and run all tests on an interactive Windows desktop. Quit competing transparency controllers first.
3. Refresh controlled screenshots with scripts/test-web-ui.ps1 -Capture. Review all themes and both styles.
4. Run the installer smoke test against a fresh destination. Check recovery, tray behavior and uninstall while running.
5. Build the portable ZIP and installer using the commands in BUILDING.md.
6. Verify package contents and SHA-256 files. Exclude developer tools, settings, logs and test data. Retain licenses and notices.
7. Tag the tested source, create a draft GitHub release, and upload the ZIP, installer, individual checksums and SHA256SUMS.txt.
8. Check uploaded hashes against local files before publishing the draft.

Keep release notes focused on available features, installation and known limitations. Signing, hardware compatibility and performance claims need their own evidence.
