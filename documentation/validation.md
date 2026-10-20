# Development validation

Build the full debugger/API emulator first (`make -j2` on Linux), then run:

```sh
python tools/validate.py --emulator ./cuzebox --output _obj_/validation
```

On Windows, pass the path to your built `cuzebox.exe`. Python 3 and Git must be
available. The output directory must be new; previous evidence is never replaced.
The command runs source auditing, headless API checks, hardware diagnostics/failure
capture checks, and logical execution and
checkpoint checks. Each check has a log, elapsed time and exit code in
`summary.json`. Checks continue after failures; the overall exit code is nonzero
if any check fails. An interrupted run has `complete: false` in its last summary.
Each subprocess has a 30-minute outer deadline in addition to individual API
timeouts. This command is a development smoke suite, not a replacement for the
existing platform, rendering, CPU and storage CI regressions.

For replay, seeded inputs, failure minimization, budgets and private SD fixtures:

```sh
python tools/validate.py --emulator ./cuzebox --output _obj_/validation-full --full
```

Add `--network` to compile and run the ESP AT/SoftAP suite. This requires Bash
and a native C compiler; use Linux or WSL for that option. It tests emulated
network behavior, not a live Uzenet server or physical ESP8266.

## Tests owned by game projects

Keep game manifests, structure offsets, input sequences and gameplay assertions
in the game's repository. Invoke the emulator's generic runner from there:

```sh
python /path/to/cuzebox/tools/run_game_test.py tests/boot.json --emulator /path/to/cuzebox/cuzebox --output /path/to/new-results
```

ROM, ELF and SD paths in a manifest resolve relative to that manifest. The output
directory must be new. See [manifest format](game-test-manifests.md) and
[advanced options](game-test-advanced.md) for assertions and checkpoint resume.
Python harnesses can import `run_game_test` and `cuzebox_api` by adding the
emulator's `tools` directory to `PYTHONPATH`; no game-specific imports or fixtures
are required by emulator CI. The emulator's own synthetic cartridge tests verify
the generic machinery independently of any game's data layout.

## CI

The Linux job in `.github/workflows/build.yml` runs the default command after
building. **Run workflow → extended** additionally runs `--full --network`.
Both output directories are uploaded as the `development-validation` artifact
even when a test fails. Existing CI replay checks remain enabled on pushes.
