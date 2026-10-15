# RGB30 and standalone ROCKNIX integration

The RGB30 uses an AArch64 Linux build. `TARGET_RGB30=1` selects a lean
gameplay preset while keeping the native SDL2 controller interface, MicroUI,
save states, and virtual keyboard. It disables the debugger, API/web server,
display filters, recording/replay, netplay, ESP emulation, and remote ROMs by
default. Explicit command-line feature overrides still work.

The intended delivery is a **standalone emulator integrated into ROCKNIX**.
This does not require a PortMaster installation. ROCKNIX already has an
[Uzebox system](https://rocknix.org/systems/uzebox/); its existing Uzem core can
remain available alongside CUzeBox EX.

## Build locally

On Linux, install or provide GNU Make, an AArch64 GNU C compiler, binutils,
pkg-config, and **target** SDL2/ALSA development libraries. On a Debian/Ubuntu
cross-build host these are typically `make gcc-aarch64-linux-gnu
binutils-aarch64-linux-gnu pkg-config libsdl2-dev:arm64 libasound2-dev:arm64`.
ARM64 package availability depends on the host's multiarch repository setup.
The scripts check/use existing dependencies; they do not install packages.

```sh
sh tools/build_rgb30.sh --doctor
sh tools/build_rgb30.sh
```

From Windows with a suitably configured WSL distribution:

```powershell
./tools/build_rgb30.ps1 -Doctor
./tools/build_rgb30.ps1
# To select another installed distribution:
./tools/build_rgb30.ps1 -Distribution Ubuntu
```

Output: `_obj_/rgb30/cuzebox`. The script also writes
`_obj_/rgb30/runtime-dependencies.txt` with shared-library and symbol-version
requirements. Objects are kept separate from desktop builds. When changing
feature flags, force a rebuild, for example:

```sh
sh tools/build_rgb30.sh -B FLAG_DEBUGGER=1 FLAG_API_SERVER=1
```

The equivalent Make target is `make TARGET_RGB30=1` (or `make rgb30`). On a
native AArch64 machine use `CROSS_COMPILE=`. For a custom SDK supply its
`CCOMP`, `PKG_CONFIG`, `PKG_CONFIG_LIBDIR`, and `PKG_CONFIG_SYSROOT_DIR` as
appropriate. `EXTRA_CFLAGS` and `EXTRA_LDFLAGS` accept SDK flags without
replacing the emulator's feature defines.

An Ubuntu-built binary is useful for cross-build verification, but its libc
and SDL requirements may differ from a particular ROCKNIX release. Build the
distributed emulator with the ROCKNIX SDK rather than bundling Ubuntu
libraries into the firmware.

## Integrate into ROCKNIX

The files in `packaging/rocknix/` are an integration starting point, not a
finished firmware package. The paths below follow ROCKNIX's `next` branch;
check them against the release being targeted. Follow ROCKNIX's
[build instructions](https://rocknix.org/contribute/modify/).

1. Commit and publish the emulator changes. Copy `package.mk.in` to
   `projects/ROCKNIX/packages/emulators/standalone/cuzebox-ex/package.mk` in a
   ROCKNIX checkout. Replace `@SOURCE_COMMIT@` with that full commit hash and
   `@SOURCE_SHA256@` with the SHA-256 of its GitHub source archive. The recipe
   builds against ROCKNIX's compiler/sysroot and installs the binary, launcher,
   initial configuration, and license.
2. In `projects/ROCKNIX/packages/virtual/emulators/package.mk`, add
   `cuzebox-ex` to the RK3566 `PKG_EMUS` list. Register the alternative before
   `add_es_system uzebox`:

   ```sh
   add_emu_core uzebox cuzebox-ex cuzebox-ex false
   ```

   Keep the existing Uzem registration. `false` leaves the existing default
   intact; users can select CUzeBox EX in the system/game emulator settings.
3. In `config/emulators/uzebox.conf`, include `.hex` alongside `.uze` in
   `SYSTEM_EXTENSION` (and uppercase equivalents if desired).
4. In the emulator dispatch in
   `projects/ROCKNIX/packages/rocknix/sources/scripts/runemu.sh`, add:

   ```sh
   cuzebox-ex)
     set_kill set "-TERM cuzebox-ex"
     RUNTHIS='${RUN_SHELL} /usr/bin/start_cuzebox.sh "${ROMNAME}"'
     ;;
   ```

The launcher uses `/storage/.config/cuzebox-ex` as the working directory so
preferences, EEPROM, and other relative output paths are writable. It seeds
`config.cfg` from `/usr/config/cuzebox-ex` only on first launch and supplies
ROCKNIX's SDL controller database when present. It resolves the ROM path
before changing directories, including paths containing spaces. ROMs belong
in the firmware's `roms/uzebox` directory.

Initial settings enable fullscreen, game-only output, controller menus, and
the virtual keyboard. Aspect ratio is preserved on the RGB30's square screen.
The API and on-screen system messages start disabled. Resident bootloader
behavior supports `.uze`; loading `.hex` uses the ISP-style full flash load.

## Controls and shutdown

- **L1 + R1 + Start:** open/close the controller menu.
- **Start + Select:** quit through normal emulator shutdown, flushing saves.
- Menu navigation uses the existing controller cursor and face-button controls;
  the virtual keyboard allows text entry without a physical keyboard.
- `SIGTERM` and `SIGINT` also request normal shutdown in this preset. Avoid
  `SIGKILL` for the normal exit path because it bypasses save flushing.

## Validation before distribution

Run `python3 tools/test_rocknix_launcher.py` on Linux/WSL for the launcher
regressions. `tools/test_handheld_shutdown.py <executable>` verifies SIGTERM
and SIGINT save flushing with a Linux executable built with `TARGET_RGB30`
defined and API/debugger support enabled. Cross compilation alone does not
establish device compatibility.
Test the final ROCKNIX image on the RGB30: frontend ROM launch, `.uze` and
`.hex`, fullscreen/aspect ratio, sound and sustained frame rate, controller
mapping, menu/quit chords, and preference/EEPROM/save-state persistence across
relaunches. Also verify that frontend-triggered termination returns cleanly.
The package recipe still needs its published source pin and an actual ROCKNIX
SDK/image build before it is ready to submit upstream.
