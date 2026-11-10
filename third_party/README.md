# Third-party dependencies

## JsonCpp 1.9.8

Source: https://github.com/open-source-parsers/jsoncpp/tree/1.9.8

Archive SHA-256: 822b68a364ca7a6ae1d9a222c1e5f2bc3aca1666ddde297348b0841418f44feb

Public headers and src/lib_json are retained unmodified. The library is statically linked under its MIT license option; see jsoncpp/LICENSE.

## Microsoft WebView2 SDK 1.0.4314.35

Package: https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg

Package SHA-256: 90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4

The header and x64 loader are unmodified. See ../LICENSES/WebView2.txt and webview2/NOTICE.txt. MinGW UUID declarations live in src/app/webview_uuids.hpp rather than modifying the vendor header. The loader is loaded only from the application folder. The separately installed WebView2 Runtime has Microsoft's own terms.

## NSIS

NSIS builds the per-user installer. Required installer/compression notices are in ../LICENSES/NSIS.txt. The compiler itself is a development tool and is not bundled.

## Optional taskbar engine

TranslucentTB 2026.2 is a separate GPL-3.0 companion. Glassify neither links its source nor bundles its executable. Explicit setup downloads the official portable archive and verifies its pinned SHA-256. Its authors retain their copyright and license. See ../docs/TASKBAR.md.
