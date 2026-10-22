# Integration validation, 2026-10-22

This update starts from upstream main at
`d62c1f2bdced837db42538daf1d9f46cf368f102`. Comparing upstream tracked files
against the current native working source found no missing files. Existing
SPI timing, interlace, replay/debugger, SD, Linux and handheld code remains
in the tree; this is an integration rather than a replacement snapshot.

The update adds the UzeNet firmware adapter and identity selection, ESP ROM
loader simulation, isolated configuration paths, guest speed hints, controller
and haptic UI improvements, and browser UzeNet integration. Native and browser
adapters share the pinned portable firmware sources. The embedded Bootloader
5.0.13 has matching rebuildable source under `third_party/bootloader5`.

Validation completed:

- Linux native build with OpenSSL and Emscripten browser build.
- SPI timing, interrupt delays, Timer1 and interlace regressions.
- Controller mapping, UzeNet identity and configuration isolation regressions.
- UzeNet adapter checks with and without TLS, ESP AT/SoftAP and PKI tests.
- AVR speed marker encoding, native pacing and browser pacing-state checks.
- Browser keyboard controls, fullscreen/layout and scaled pointer coordinates
  in Microsoft Edge.
- Bootloader source rebuild reproduces the included HEX file.

This is not exhaustive behavioral equivalence testing. Windows native and
Firefox were not validated in this publication checkout. Browser UzeNet needs
a host page and authenticated relay; standalone browser builds still support
offline play. Speed hints remove pacing limits but cannot guarantee a given
frame rate on a CPU-limited host.
