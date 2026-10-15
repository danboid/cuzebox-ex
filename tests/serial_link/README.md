# Serial link regression tests

`run.sh` is the self-contained CUzeBox regression suite. It checks the raw TCP
client completion path, diagnostic counters/CSV logging, a deliberate service-gap
event, 9600-baud UART frame timing, the TX-complete interrupt-enable gate, and
source guardrails for lockstep-safe local pairing.

For the attached Catacombs build, use:

```sh
./tests/serial_link/run_catacombs_pair.sh \
  /path/to/CatacombsOfTheDamned.hex \
  /path/to/CatacombsOfTheDamned.elf
```

The script resolves the game-state SRAM symbols from the ELF, builds a headless
CUzeBox core, starts server and client processes on localhost, and requires both
to progress from UZELINK discovery through gameplay tick 1000. It fails on a
link-error state or a permanent gameplay stall and reports the longest observed
video-frame interval between simulation ticks after the ready barrier. Active
gameplay also fails if that interval exceeds eight video frames.
