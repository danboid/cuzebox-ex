# Browser build (Emscripten / SDL2)

The browser target uses SDL2 and enables MicroUI by default. Move the pointer
to the top of the canvas to open its menus, or use the GUI shortcut. Pass
`FLAG_MICROUI=0` to build without MicroUI. SDL1 is unsupported for this target.

## Build and serve

Tested here with Emscripten 6.0.12 in Ubuntu 22.04 under WSL and headless Edge
on Windows. CI builds the same SDK version and runs the browser test in Chromium.

Install and activate the Emscripten SDK, then run from the repository root:

```sh
source /path/to/emsdk/emsdk_env.sh
make TSYS=emscripten -j2
python3 -m http.server 8000 --bind 127.0.0.1 --directory _obj_/emscripten
```

Open `http://localhost:8000/cuzebox.html`. Serve the generated HTML, JavaScript
and WebAssembly together; opening the HTML directly as a local file will not
work. The build runs on one browser thread and does not require cross-origin
isolation headers or a socket proxy. WSL can build the same files for a Windows
browser. First builds download and compile Emscripten's SDL2 port.

Use **Load ROM** to import a `.uze` or `.hex` file into the browser filesystem,
then use MicroUI normally. **Enable audio** provides the browser gesture needed
to start audio. Keyboard input goes to the canvas when it has focus. Browser
shortcuts may reserve some key combinations. Gamepads depend on the browser's
Gamepad API and usually require a secure origin (localhost or HTTPS).

SD reads include data appended to a UZE file; uploading does not truncate the
file to its flash-program portion. Keep its original filename when a game
opens its own UZE (for example, Shadowgate looks for `SHADGATE.UZE`). The SD
model resets CRC checking on CMD0 so an application can reinitialize a card
after the resident bootloader used CRC-enabled transfers.

If `MEGADOOM.UZE` reports **CHAINLOAD ERROR 2**, select **CONFIG > DEVICES >
SD TIMING > FAST**, save preferences, and reload the ROM. The same issue occurs
in native builds: the legacy NORMAL preset's 500 ms SD initialization delay
exceeds this game's bootloader API polling budget. FAST uses 50 ms and has
been tested through the menu, flash chainload and gameplay in both builds.
Keep the resident bootloader enabled: MEGADOOM uses its flash and SD services.
SD TIMING and the FAST FLASH checkbox control different behavior.

New preferences select **.UZE HINT** for SD timing. Header byte `0x19A` selects
SLOW (1), NORMAL (2), or FAST (3); unspecified (0) and unknown values use NORMAL.
Manual choices override the hint and existing preferences retain their selection.
For example, packing MEGADOOM with `sd=fast` makes it choose FAST automatically
in HINT mode. See [packrom instructions](../tools/packrom/README.md).

No external game files are needed to build: the embedded resident bootloader
starts by default. To bundle a game, place `gamefile.uze` in the repository root
and build with `FLAG_NOGAMEFILE=0`. For a filesystem-free embedded game,
`FLAG_SELFCONT=1` remains available for games without SD requirements.

## Defaults and storage

- Included: MicroUI, virtual keyboard, controllers, audio, pause/frame advance,
  save states, EEPROM/flash saves and the virtual SD filesystem.
- Excluded: native API/web server, source debugger and traces, full ESP/AT emulation,
  netplay, Remote ROMs, display filters and native video/input capture/replay.
- Whisper console logging defaults off; override `FLAG_PRINTF_WHISPER` if needed.

## UzeNet browser integration

The browser build includes the portable UART-v4 UzeNet endpoint from
`third_party/uzenet`, shared with the native firmware adapter. It models PA7
entry/acknowledgement, flow control and AVR UART divider timing. The limited
provisioned-link bootstrap accepts `+++`, `AT`, `AT+UART_CUR` at 9600, 57600 or
115200 baud (8N1), and `AT+CIPSEND`; other AT commands return ERROR.

The standalone page runs ROMs locally. Online matches require a hosting page
and authenticated WebSocket-to-Room relay, such as the integration in
[UzeNet](https://github.com/weber21w/uzenet-main). The host connects the exported
functions and Module callbacks in `browser_endpoint.c` and `browser_uart.inc`
to that relay. Browser networking cannot use native UDP hole punching.
Wi-Fi provisioning and flashing remain native features.

The bundled Bootloader 5.0.13 includes the SD/chainload fixes used by the browser
build. Its matching source is in `third_party/bootloader5`; root `Bootloader.hex`
and `bootloader_builtin.h` are generated release artifacts.

User files live under `/user` and synchronize to IndexedDB. This includes
preferences, uploaded ROMs, profiles and saves. Pending preference and device
writes are saved periodically; **Save preferences and files** explicitly saves
and waits for browser storage. Hiding the tab also requests a save. Abruptly
killing a browser before an asynchronous save finishes can lose recent changes.
Storage belongs to the browser profile and site origin; clearing site data
removes it. Private browsing or storage restrictions may prevent persistence.

This version imports ROMs through the page; it does not provide a general SD
file upload/download manager. Existing native filesystem paths do not map to
the user's real directories in a browser.

## Browser regression

After building, install Playwright outside the source tree and run:

```sh
node tools/test_web_build.js
```

The script serves the build on loopback, starts a muted headless browser, checks
startup/audio/frame execution, pause/frame advance, a generated HEX upload,
a MicroUI audio checkbox change and IndexedDB persistence after reload. `WEB_BROWSER_CHANNEL=msedge`
selects an installed Edge; otherwise install Playwright's Chromium. The test
writes its screenshot into the ignored browser build directory.

## Pointer coordinates

The browser refreshes SDL's pointer position before mouse-button events. This
keeps clicks aligned after page scrolling, responsive resizing, or status text
moves the canvas beneath a stationary pointer. Relative pointer-lock movement
remains under SDL control. `node tools/test_web_pointer.js _obj_/emscripten`
checks the visible audio checkbox before and after a layout shift, at normal and
CSS-scaled canvas sizes. It requires Playwright and an installed Edge browser
(or WEB_BROWSER_CHANNEL selecting another installed browser).

MicroUI also checks the current position on a press, so a move and click within
one rendered frame cannot activate the previous hover. The small native test
also checks held drags and releases:

```sh
cc tests/gui/pointer_regression.c microui/microui.c -lm -o /tmp/pointer-test
/tmp/pointer-test
```

### Browser controls

Use the page's Fullscreen button; web builds ignore saved DisplayFullscreen
settings and restore the page position on exit. Click the canvas for keyboard
focus. Escape does not quit the web emulator. Empty input configurations get
keyboard/gamepad bindings for player one and a gamepad for player two, while
custom bindings survive reloads. Controller assignments also survive CPU resets
and ROM loading. `tools/test_web_controls.js` checks these behaviors; its
optional HapticDemo.uze argument checks keyboard input through the AVR game.
