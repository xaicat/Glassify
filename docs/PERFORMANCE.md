# Performance

Instant reopen retains the prepared hidden WebView2 page for tray access. Low memory closes it after 3.5 seconds hidden and rebuilds it on opening. Choose the tradeoff in Settings > Tray behavior.

## Initial release measurements

Version 1.0.0, Windows build 26300.9711, 12 logical processors, WebView2 Runtime 157.0.4341.42. Minimal style, empty rules, mouse hook enabled. CPU is normalized across all logical processors. Private memory sums Glassify, its recovery helper and surviving browser children.

| Mode and state | Sample | Average CPU | Peak private memory | Processes |
| --- | --- | --- | --- | --- |
| Instant reopen, visible | 10.40 s | 0.000% | 160.53 MiB | 8 |
| Instant reopen, hidden | 10.37 s | 0.050% | 160.14 MiB | 8 |
| Low memory, visible | 10.35 s | 0.000% | 162.73 MiB | 8 |
| Low memory, hidden | 10.25 s | 0.000% | 4.69 MiB | 2 |

Raw reports: [Instant reopen](verification/1.0.0-instant-panel.json) and [Low memory](verification/1.0.0-low-memory-panel.json).

These short samples vary with desktop activity and browser servicing. Zero means no detected process CPU counter increase during that sample. DWM/GPU work, frame pacing, power draw, the optional taskbar engine and workloads with many transparent windows are not included.

## Reopening

The styled workflow checks that three hidden intervals retain the same document in Instant reopen mode. Low memory is checked separately for disposal and reconstruction. The initial browser surface stays hidden until navigation and the rendered-model acknowledgement complete.

The [reopen report](verification/1.0.0-reopen.json) records time to visible ready web state, including localhost debugging reconnect overhead. It does not measure optical first paint or GPU frame time. Cold application launch still needs browser startup.

Use Minimal, opacity without tint and Low memory when resident memory matters more than immediate reopening. Idle process measurements do not guarantee smoothness across every GPU and app.
