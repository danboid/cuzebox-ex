# Local Uzenet network testing

`tools/uzenet_local.py` starts a private Room server and the real Identity
service from a local Uzenet source checkout. It generates separate player
certificates, accounts, emulator configurations and logs in a new working
directory. It does not change the source checkout or use production accounts.

The launcher runs on Linux, including Ubuntu under WSL. Install Python 3,
a native C compiler, OpenSSL development libraries/tools and the SQLite runtime.
Build the Linux emulator with `FLAG_OPENSSL=1`. The launcher expects the
`uzenet-room`, `uzenet-identity` and `uzenet-api` directories in the server checkout.

From Windows PowerShell:

```powershell
.\tools\run_uzenet_local.ps1 `
  -ServerRoot C:\uzenet-master `
  -WorkDir C:\uzenet-tests\session-001 `
  -Rom C:\uzebox-master\demos\SFA2\default\SFA2.uze
```

The wrapper defaults to Ubuntu-22.04 and the Linux emulator at
`_obj_/uzenet/linux/cuzebox`. Override with `-Distribution` and `-Emulator`.
It launches Linux emulator windows through WSLg; it does not launch the native
Windows executable. Use `-Headless` for API-driven testing.

The equivalent Linux command:

```sh
python3 tools/uzenet_local.py \
  --server-root /path/to/uzenet-master \
  --work-dir /path/to/new-session \
  --emulator /path/to/cuzebox \
  --rom /path/to/game.uze
```

The Python launcher defaults to a 115200-baud saved ESP connection. For older
clients such as UzeSpy that start at 9600, supply `--baud 9600`.

Omit the ROM (and emulator argument on Linux) to start only the services and
generate configurations. `--players` / `-Players` selects 1–4 players, default 2.
Each emulator uses its own configuration, working directory and API port.
The ROM is copied into each player's directory; games needing additional SD
assets must have those assets placed there too.

Room listens on loopback, using a private build copy that changes only its bind
addresses. Identity uses a temporary Unix socket. Player certificates are
registered to distinct accounts starting at 1001 in the private SQLite database;
the launcher verifies their lookups through the actual Identity service.
TLS and authenticated UDP remain enabled. This is a test fixture, not account
provisioning for a production server.

The terminal prints the server port, configuration paths and emulator API ports.
`session.json` records the accounts and paths. Keep the terminal open during
play; Ctrl+C stops all processes owned by the launcher. Logs and test data remain
for inspection. Use a fresh working directory for every run; temporary
certificates expire after one day.

Games perform matchmaking themselves: UART v4 **START** with room key zero joins
a compatible waiting public room or creates one. Starting the infrastructure
does not press the game's multiplayer menu buttons. A game's compatibility
value and session parameters must agree across instances.
