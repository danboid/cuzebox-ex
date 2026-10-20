# Codebase review — 2026-10-15

This pass reviewed the active `cuzebox-ex-main` source tree, corrected concrete
logic and persistence defects, improved parsing and build reliability, and
measured a small capture optimization. Windows, Linux and browser builds and
the checks listed below passed locally. No commit or push was made.

## Scope and method

The starting snapshot contained 307 source/distribution files and approximately
107,000 lines of C/C++/headers/scripts, including generated assets. The pass
combined a project inventory, compiler diagnostics, focused manual inspection of
core/peripheral timing, storage, capture, GUI, build and parser paths, and the
existing subsystem regressions. This is broad coverage, not a proof that every
line or hardware configuration is correct. The source audit checks accidental
artifacts and credential patterns; it is not a semantic correctness test.

The parent directory is the actual Git root and contains existing user changes;
the nested project is a source snapshot. Changes for this review are confined to
that nested tree. Local comparison files and build/test outputs are under its
ignored `_obj_/review/` directory. The older GitHub upload ZIP was not refreshed
and does not contain these changes.

## Corrections and cleanup

| Area | Problem and resulting behavior |
|---|---|
| SEI / RETI | Both decoders could enter an interrupt before executing the required following instruction. They now update peripheral clocks normally but defer interrupt sampling until that instruction completes. No new persistent delay counter or snapshot layout was needed. |
| Interrupt priority | SPI incorrectly outranked watchdog and Timer1. The implemented sources now follow vector-address priority. |
| Flash decoding | Updating only the second word of CALL/JMP could leave the preceding decoded instruction stale. Partial updates now recompile that predecessor, including flash wrap. |
| EEPROM | Write-only programming used OR instead of AND; the changed flag compared data with an address; EEMPE expiration was scheduled from the wrong value; duration used the previous mode. These are corrected, and an active programming operation protects its busy/mode bits from further EECR writes. |
| Movie CRC / reads | A constant 64-byte nibble table replaces the bit loop. Preflight and playback reuse bounded read buffers instead of allocating every event. CRCs and file bytes are unchanged. Read errors at END, event seeking and write failure handling are checked. |
| Savestates | A shared preflight checks lengths with wide arithmetic, nested SPI sizes/layout and VFAT before CPU mutation. SPI allocation capacity is checked before copying. Movie preflight validates its timer trailer. Legacy ordinary states without the trailer and the older ESP block remain supported. |
| Cheats | Save read an uninitialized selection flag. It now applies the selected editor explicitly. File saving/loading preserves 16/32-bit values, comparisons, width and signedness. Failed writes keep the dirty flag, and reusing the current path avoids copying a string onto itself. |
| ELF readers | Section offset arithmetic could wrap on 32-bit hosts; section names could run outside their string table. Both readers check wide bounds and terminators. The DWARF section loader was expanded into readable steps. |
| Build dependencies | Feature/compiler/link changes could reuse incompatible objects; protocol `.inc` changes could be missed. A configuration stamp and include dependencies trigger rebuilds while preserving no-op builds. |
| Test maintenance | API/web/ESP harness stubs had fallen behind production interfaces. They were updated so those regressions run again. New core, storage, cheats, build-dependency and ELF checks are included in the build workflow. |

Interrupt and EEPROM expectations were checked against the
[ATmega644 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/doc2593.pdf),
including sections 5.8/5.8.1 and 6.5.3. The previously tested
`OUT SPH / OUT SREG / OUT SPL` sequence remains correct; this review does not
claim that it reproduced the reported Uzem stack-corruption path.

## Measured optimization

Three Linux x86-64/WSL trials processed 32 MiB per algorithm with the normal
size-optimized, LTO-enabled test build. Times are process CPU seconds:

| Trial | Original bit loop | Nibble table | Ratio |
|---|---:|---:|---:|
| 1 | 0.347725 | 0.142887 | 2.43× |
| 2 | 0.342061 | 0.153986 | 2.22× |
| 3 | 0.345100 | 0.155643 | 2.22× |

The ratio of medians is approximately **2.24×**, or 55% less CPU time in this
CRC microbenchmark. Empty, known, random, non-default initial and incremental
CRC inputs match the reference implementation. Buffer tests verify reuse after
a larger chunk and enforce the existing maximum before allocation.

This does not establish a whole-emulator, handheld or gameplay speedup. The
earlier real-ROM capture measurements in the handoff remain separate historical
evidence. No new compression mode or whole-emulator performance claim is made.

## Validation completed locally

| Configuration / subsystem | Checks |
|---|---|
| Windows MinGW and Linux full builds | Build and all 17 application regression targets: preferences, lean preferences, headless devices, screenshot, screenshot tool, controller mapping, shortcuts, file dialog, ROM loading, whisper, source stepping, Timer1, movie recording, playback controls, interrupts/EEPROM, storage and cheats (whisper checks both enabled and disabled). |
| Windows and Linux APIs | Headless operation, input movie API and source stepping API. |
| Linux compile-time headless | Build; headless and input movie API tests. |
| Linux lean gameplay | Build with debugger/API, filters, capture/replay, ESP, netplay, remote ROMs and virtual keyboard disabled. API-driven runtime tests use the full/headless configurations. |
| Handheld shutdown simulation | Linux host build with the RGB30 signal-handler code enabled: SIGINT/SIGTERM save flushing. This is not physical RGB30 validation. |
| Browser | Emscripten 6.0.12 build; headless, muted Edge test for startup, frames, audio, pause/step, HEX upload, MicroUI audio checkbox and IndexedDB reload. |
| Storage/core host checks | Bootloader, writable SD, SD initialization/timing/payload/fault/trace/replay/history/analysis, SPI registers and packrom SD/header patching. |
| Debug/audio/network host checks | Source/DWARF readers, debug memory gates and timing, audio capture/analysis/scope, UART logic capture, serial links, ESP AT/network/MQTT/legacy states, API multi-client, embedded web server, web debugger bridge and web source stepping. |
| Build/packaging | Platform selection, flag/include/no-op rebuild checks, ROCKNIX launcher, source audit, bootloader header and embedded web-asset drift. |
| Malformed ELF | Both readers on Windows (32-bit toolchain) and Linux; Linux address/undefined-behavior sanitizers passed. |

The new CPU regression exercises both decoder implementations in the same
native test, including pending/new IRQs, multi-cycle following instructions,
repeated SEI, CLI, RETI chains, SREG writes, vector priority, EEPROM modes/window
expiry and flash operand updates. Storage tests reject overflow and inconsistent
SPI metadata before CPU mutation. GUI tests use the real rendering/input paths.

Local evidence includes `_obj_/review/windows-complete2.log`,
`linux-complete.log`, `windows-api.log`, `final-checks2.log`, `browser-test.log`
and the individual logs under `checks/`. Early failed runs were corrected and
rerun: the new cheat fixture initially lacked a save path, and a handheld-only
shutdown test was initially invoked against a build without its signal handler.
The lean build intentionally has no API and rejects `--headless`; it was build
validated. An exploratory GCC analyzer/LTO link was interrupted because of its
cost and is not counted as a completed analyzer check. Remote CI was not run.

## Compatibility and remaining limits

- Interrupt/EEPROM corrections can desynchronize older input movies even when
  their CZINPUT v1 ABI checks pass. Re-record affected movies with this core.
  CRC optimization itself changes no checksum or container encoding.
- State snapshots remain native ABI structures. This work strengthens specific
  boundaries; it does not provide a portable schema, validate every raw field,
  or guarantee transactional rollback on every allocation failure.
- Native macOS, physical RGB30/controllers/MIDI/serial and real remote-service
  behavior were not tested. Network tests use controlled local harnesses.
- Some pre-existing unused-function and bounded-string truncation diagnostics
  remain in broader compiler configurations. The tree is not warning-free.
- Large GUI/main/ESP modules and dense parser helpers still merit gradual
  extraction as features change. This pass improves touched logic and ownership
  without a mass formatting rewrite. A future profiling pass should use fixed
  ROM/state workloads before changing interpreter, trace or rendering paths.
- Compact/Full Debug capture selection was implemented in a subsequent pass;
  see [capture modes](input-capture-modes.md). Portable states and a core-behavior
  compatibility identifier remain separate design work.
