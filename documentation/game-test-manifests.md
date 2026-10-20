# Declarative game tests

`tools/run_game_test.py` runs a JSON manifest against a private, headless CUzeBox
process. It uses only Python's standard library and the existing logical marker
API. No new emulator hooks are enabled during normal gameplay.

This is intended for game developers and coding agents: specify meaningful fields
and invariants once, then run the same test after each cartridge build. It does
not interpret game state or execute arbitrary Python expressions from manifests.

## Run a test

```sh
python tools/run_game_test.py game-test.json --emulator /path/to/cuzebox --output _obj_/test-001
```

Use `cuzebox.exe` on Windows. Python and the emulator must run on the same OS so
they agree on paths. The output directory must not already exist. The runner
never attaches to an existing emulator or changes its configuration. It starts
with resident bootloader and serial routing disabled in a private working
directory. The cartridge starts from reset unless `--resume` selects a checkpoint.
Optional SD fixtures and device settings are described in
[advanced game tests](game-test-advanced.md). External network peers are not captured.

The single JSON object printed to stdout is also saved as `result.json`:

* Exit **0**, `status: "pass"`: all requested observations and assertions passed.
* Exit **1**, `status: "fail"`: first failed assertion, instruction timeout, input
  exhaustion, or another early logical stop.
* Exit **2**, `status: "error"`: invalid manifest, API/setup failure, or wall-clock
  deadline. This is not reported as a game assertion failure.

Invalid manifests are rejected before launching the emulator. Unknown keys and
assertion operators are errors, so misspelled checks cannot silently pass.

## Manifest version 1

This example is illustrative: replace symbols, SRAM addresses, and button masks
with values from your game. ROM and ELF paths are relative to the manifest.

```json
{
  "version": 1,
  "name": "player survives opening",
  "rom": "build/game.uze",
  "elf": "build/game.elf",
  "markers": {
    "input": "Game_InputSample",
    "observe": "Game_UpdateDone",
    "every": 1,
    "timeout_instructions": 10000000,
    "player": 0
  },
  "observations": 180,
  "wall_timeout_seconds": 60,
  "inputs": [
    {"mask": 0, "updates": 60},
    {"mask": 8, "updates": 1},
    {"mask": 0, "updates": 119}
  ],
  "fields": {
    "health": {"region": "SRAM", "address": "0x200", "type": "u8"},
    "pose": {
      "region": "SRAM", "address": "0x201", "type": "u8",
      "enum": {"0x82": "idle", "0xC0": "stunned"}
    },
    "clock": {"region": "SRAM", "address": "0x202", "type": "u16"}
  },
  "assertions": [
    {"field": "health", "op": "gt", "value": 0},
    {"field": "pose", "op": "in", "values": ["0x82", "0xC0"]},
    {"field": "clock", "op": "changes", "within": 30},
    {"field": "pose", "op": "eq", "value": "0x82", "at": 180}
  ]
}
```

Markers accept AVR **word** addresses (integers or `"0x..."`) or program symbols
loaded from the optional ELF. Fields use explicit raw SRAM indices or decoded SPI
addresses, or `"symbol": "player"` and optional `"offset": 2` in place of `address`
for an ELF SRAM data symbol. Offsets are bytes; symbol kind and region are checked.
Keep structure offsets alongside the game's build definitions when layouts change.
SRAM indices are AVR data addresses masked with `0xfff`. A field must fit entirely
inside the selected region. The emulator verifies the actual configured SPI size.

Types are little-endian `u8`, `i8`, `u16`, `i16`, `u32`, and `i32`. Optional `enum`
maps numeric strings to diagnostic labels; it does not constrain values by itself.
Use an `in` assertion for an allowed set. Numeric strings use Python base-0 syntax
(`"0x82"`, `"130"`); JSON integer literals are also accepted.

Assertions support `eq`, `ne`, `lt`, `le`, `gt`, `ge` with `value`, `in` with
`values`, and `changes` with `within`. All apply at every observation unless `at`
selects one **one-based observation number**. `changes` cannot use `at`: the first
sample establishes a baseline, and failure occurs after `within` consecutive
observation intervals without a change. A change on the deadline observation
passes. The total observation count must exceed this interval. This detects
stalled counters; it does not prove a particular amount or direction of progress.

`every`, instruction timeout, and player default to 1, 10000000, and 0. The
wall-clock limit defaults to 60 seconds and covers execution after setup; API
requests additionally have socket timeouts. A wall-clock error may exceed that
limit by an outstanding request and diagnostic collection time. Bounds are 4096
observations, 4096 input entries, and 16 scalar fields. Inputs use active-high
SNES masks held for logical input advances, not video frames.

The runner stops at each observation boundary, reads the atomic buffered record,
and checks assertions before continuing. The first failure leaves the machine
at that boundary; no later logical update executes. These host round trips cost
wall time but do not execute extra AVR instructions. For high-throughput capture
without assertions, use the non-stopping API directly. See
[logical development tests](logical-development-tests.md) for marker placement,
controller sampling, address rules, and observation versus update counters.

## Failure bundles and reproduction

Every run retains:

* `manifest.json`, the normalized manifest with bundle-relative ROM/ELF paths;
  `source-manifest.json`, the original manifest; copied ROM and optional ELF.
* `identity.json`, SHA-256 hashes of the emulator, ROM, and ELF and original paths.
* `result.json`, machine-readable status and first failing assertion, actual value,
  label, logical update, observation number, memory region, and address.
* `observations.jsonl`, raw marker records plus decoded named values;
  `logical.jsonl`, the native export including emulator behavior ID and ROM CRC.
* `commands.jsonl`, API requests/responses; `emulator.log`; and
  `diagnostics.json`, registers, machine status, movie behavior identity, and
  explicit errors for diagnostic artifacts that could not be collected.
* `states/savestates/rom/slot_0.czs`, the initial machine state (directory name
  follows the copied ROM's basename).

Failures additionally request `failure.bmp`, a final machine savestate in slot 1,
and a persistent `failure.lct` logical checkpoint. Screenshot, export, and snapshot failures
are recorded in diagnostics without replacing the original test failure.
If the emulator crashes or cannot start, some artifacts will be unavailable.

Reproduce the test with the same emulator build:

```sh
python tools/run_game_test.py _obj_/test-001/manifest.json --emulator /path/to/cuzebox --output _obj_/test-002
```

The copied manifest and cartridge are the reset-based reproduction recipe. A
plain `.czs` file restores machine state for inspection, **not** the logical input
cursor. `.lct` checkpoints persist both. Set `checkpoint_every` to save successful
interval checkpoints with assertion history; `--resume` restores these in a new
process. See [checkpoint details](game-test-advanced.md). Checkpoints retain the
native savestate ABI restrictions and require the same build in the runner.
Observation timestamps may vary across changed builds; logical fields are the
useful comparison.

Bundles contain game code and observed game memory. They remain local and are
never uploaded by the runner. No instruction or device tracing is implicitly
enabled; the saved traces are logical observations and API exchanges.

## Validation

```sh
python tools/test_game_test_runner.py /path/to/cuzebox
```

The integration suite runs a real AVR counter cartridge. It checks successful
assertions, exact first-failure location, enum diagnostics, signed decoding,
copied-bundle reproduction, stalled progress, instruction and wall timeouts,
input exhaustion, savestate/screenshot creation, invalid operators, and refusal
to overwrite an existing bundle. It runs on Windows and Linux.
