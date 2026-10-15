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
- Excluded: native API/web server, source debugger and traces, ESP/networking,
  netplay, Remote ROMs, display filters and native video/input capture/replay.
- Whisper console logging defaults off; override `FLAG_PRINTF_WHISPER` if needed.

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
