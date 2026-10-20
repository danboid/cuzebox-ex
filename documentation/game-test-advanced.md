# Advanced game development tests

These options extend [manifest version 1](game-test-manifests.md). Old manifests
still work. All features are opt-in; normal gameplay has no added sampling work.

## Symbol fields

```json
"fields": {
  "screen": {"region": "SRAM", "symbol": "game", "type": "u8"},
  "clock": {"region": "SRAM", "symbol": "game", "offset": 9, "type": "u16"}
}
```

The runner loads the ELF, verifies that each symbol names SRAM, resolves its raw
address and checks the entire field fits. `resolved-fields.json` records the
resolved layout. `address` and `symbol` are mutually exclusive. Byte offsets
within structures remain explicit; this does not infer C member layouts.

Keep game-specific manifests and layout offsets in the game project; see
[external game tests](validation.md#tests-owned-by-game-projects).

## Cycle budgets

```json
"cycle_budget": 600000
```

After the first observation, compute the unsigned 32-bit cycle difference
between successive observation boundaries. A difference over the budget fails
before continuing execution. `cycles.json` contains every measured interval,
the mean and maximum. This includes interrupts and waits between the chosen
boundaries. It is a per-update cost only when the observation marker occurs once
per update. The initial reset-to-first-observation interval is excluded.
Intervals must be shorter than one 32-bit cycle wrap; longer intervals alias.
Host polling time is not counted. Set a budget from a known-good run with suitable
headroom, then keep it in the game's test suite to catch regressions.

## Repeatable fuzz inputs

```json
"inputs": [],
"fuzz": {"seed": 42, "masks": [0, 1, 8, 16], "entries": 1000,
         "min_hold": 1, "max_hold": 4}
```

`fuzz` replaces `inputs` with seeded choices from the allowed button masks and
inclusive hold range. The original seed/options are written to `fuzz.json`; the
bundle's normalized manifest contains the actual generated inputs and removes
`fuzz`. Thus reproduction does not depend on a future Python PRNG implementation.
Assertions remain the game-specific validity oracle. Choose enough generated
updates for the requested observations; exhaustion is a failure, never a pass.
Use multiple seed values in CI to explore more sequences. No fuzzer understands
which button combinations are valid for your game unless you restrict `masks`.

## Failure minimization

```sh
python tools/minimize_game_test.py failure/manifest.json --emulator /path/to/cuzebox --output _obj_/reduced --max-trials 64
```

The baseline must reproduce an assertion or cycle-budget failure. Bounded delta
debugging tries increasingly small input chunks, accepting a candidate only if
the same assertion definition or cycle-budget failure recurs. Errors, timeouts,
and input exhaustion cannot substitute for the original failure. By default a
removed input becomes neutral input, preserving logical timing. `--mode delete`
shifts subsequent inputs earlier and appends neutral updates to keep the total
test duration; use this when timing changes are acceptable.

Every trial retains its ordinary run bundle. `minimized.json` is the reduced test
and `result.json` states the trial count, whether the budget was exhausted, and
the number of active input updates removed. This is a bounded reduction, not a
proof of the globally shortest reproduction. Limit: 65536 expanded updates.
The final manifest references the original ROM/ELF/SD fixtures; trial bundles
contain private copies. Checkpoint creation is disabled during minimization.

## Selected changed-code coverage

```json
"coverage": [{"start": "game_update", "words": 16},
             {"start": "0x1200", "words": 24}]
```

Select the functions or AVR word ranges affected by your change. The runner
enables the existing instruction profiler, then exports per-word hits and cycles
with ELF source path/line mappings to `coverage.json`. Zero-hit words remain
visible. Overlapping ranges are deduplicated. Data words and the second words of
two-word instructions can legitimately have zero hits; this is not a statement
that every zero-hit word is an untested instruction. Unmapped words are reported
explicitly by `SOURCE_AT`. The runner does not automatically parse Git diffs or
infer branch coverage. After checkpoint resume, profile hits describe only the
resumed suffix; machine checkpoints do not persist profiler counters.

## SD fixtures and device configuration

```json
"devices": {"spiram_pages": 1, "sd_timing": "FAST"},
"sd_directory": "fixtures/card"
```

SPI RAM uses the existing `SpiRamPagesMode` values 0..9 (0 = ROM hint). SD timing
is `SLOW`, `NORMAL` or `FAST`. These settings are written into the private startup
configuration before the cartridge boots. Host SD fixtures are copied beside the
ROM in `cartridge/`, with pristine originals retained in `sd/`. Symlinks are
rejected. A fixture cannot replace the copied cartridge. Existing emulated SD
host-file policy still applies: new-file creation is disabled by default.

Alternatively use `"sd_image": "fixtures/card.img"` for a raw sector image.
It must be a nonempty multiple of 512 bytes and at most 8 MiB. The debugger loads
it into a private in-memory backing store; reads beyond its end return 0xff and
writes beyond its end are ignored. No original image is modified. The final
private image is exported as `sd-final.img`, and checkpoints include its current
bytes. `sd_directory` and `sd_image` cannot be combined. The SD protocol's card
identity/capacity reporting remains the emulator's existing model; select a
fixture whose filesystem accesses stay within the image. VFAT host-file role
diagnostics do not describe raw-image contents.

Direct API commands, while paused with logical execution stopped:

```text
LOGICAL SD LOAD /path/to/card.img
LOGICAL SD SAVE /path/to/new-output.img
```

`LOAD` never writes back to its source; `SAVE` refuses to overwrite files. ROM
reset/load or `LOGICAL CLEAR` releases the image. Movies and netplay cannot start
while a logical raw image is mounted. Other devices/remote peers are not captured.

## Persistent checkpoints

```json
"checkpoint_every": 20
```

After each successful interval, the runner saves `checkpoint-N.lct` plus a JSON
sidecar containing assertion history, last cycle, prior interval samples, and
hashes tying it to this manifest and emulator/ROM/ELF build. For host SD fixtures,
`checkpoint-N.lct.sd/` preserves the private backing tree after flushing pending
host writes. Keep all these files together.

```sh
python tools/run_game_test.py previous/manifest.json --emulator /path/to/cuzebox --output _obj_/resumed --resume previous/checkpoint-20.lct
```

The runner validates file/build/test identities before continuing. Resume keeps
the original absolute observation target and progress-assertion history. The
decoded `observations.jsonl` contains the resumed suffix; `logical.jsonl` retains
all checkpointed records plus the suffix. A checkpoint already at the final
observation has no remaining test work. `failure.lct` is for direct debugger
inspection; the runner resumes only successful interval checkpoints with sidecars.

Direct API:

```text
PAUSE
LOGICAL CHECKPOINT SAVE /path/to/new-checkpoint.lct
LOGICAL CHECKPOINT LOAD /path/to/checkpoint.lct
LOGICAL CONTINUE 100
RESUME
```

Files contain the machine blob, input sequence/cursor, marker configuration,
condition/hit/timeout counters, observation buffer, and optional raw SD bytes.
The format uses explicit little-endian logical fields, CRC32 integrity checks,
ROM and behavior identifiers, bounded allocations, and complete validation before
machine restoration. Native machine savestate ABI/host restrictions still apply;
the file is not guaranteed portable between platforms/builds. Invalid, truncated,
wrong-ROM, or wrong-behavior files are rejected. Output files are created
exclusively. Host SD directory trees are managed by the runner sidecar workflow,
not embedded in `.lct` files. Raw SD data is included in both file and in-memory
logical checkpoints. No network-peer state is included.

Checkpointing is disabled by default (`checkpoint_every: 0`) because full machine,
observation, and storage snapshots cost memory, disk space, and wall time.

## Regression checks

For the combined validation command, CI and additional real-game cases, see
[Development validation](validation.md).

```sh
python tools/test_game_test_runner.py /path/to/cuzebox
python tools/test_logical_api.py /path/to/cuzebox
make TSYS=linux OBD=_obj_/review/linux _obj_/review/linux/logical_storage_regression
# Run in a new scratch directory: this harness writes fixture.img, storage.lct, final.img.
/absolute/path/to/_obj_/review/linux/logical_storage_regression
```

The suites cover first failures, minimization, seeded-input reproduction, budgets,
profile hits, resume in a new emulator process, private SD directory copies,
raw-image read/write bounds and rollback, and corrupt checkpoint rejection without
CPU mutation. The storage regression also verifies that ESP default/reset cleanup
preserves POSIX descriptor zero, guarding the API-disconnect bug found during the
real-game smoke test. Existing movie/replay and ESP suites remain applicable.
