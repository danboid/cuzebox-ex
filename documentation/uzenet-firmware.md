# Custom Uzenet firmware emulation

UNTool can also install a supported image through the simulated
[ESP8266 ROM loader](esp-rom-loader.md), with persistent flash and PKI storage.

The optional **UZENET UART V4** ESP firmware profile uses the portable core of
the custom ESP8266 firmware snapshot dated 2026-10-20. The existing legacy and
ESP-AT profiles remain selectable. This models the firmware's GPIO, UART and
network behavior; it does not execute an ESP8266 firmware binary.

Select the ESP module serial route, an enabled ESP emulation model, and the
Uzenet firmware profile in the serial settings. The web serial settings offer
the same selection. API clients can use `SERIAL_SET AT_FIRMWARE 2`; profile 0
is legacy and profile 1 is ESP-AT. `CONFIG SAVE` persists the selection as
`EspAtFirmwareProfile="2"`.

## Cartridge interface

PA7 models ESP GPIO0 with an external pull-up. A stable high level for at least
100 ms, followed by a 20–100 ms low pulse and release, requests entry. The
adapter samples the line every 5 ms. Entry waits for an idle AT/UART boundary;
games should drain startup output before requesting it.

The firmware acknowledges by pulling PA7 low for 41 ms, releases it, and waits
before sending the UART v4 ENTERED frame. After acknowledgement, AVR low means
READY and high means PAUSE. Output uses the firmware's baud-dependent batches.
The AVR UART retains its normal emulated transmission timing.

The vendored core implements framing/CRC, capabilities, deferred stock output,
transaction retries, endpoint/game services, names/profiles, checks and barriers.
EXIT sends its reply before returning to AT operation. Keep READY asserted
through the exit worker after receiving that acknowledgement.

The firmware worker advances from emulated cycles even when the cartridge is
busy or has paused UART reception. Four local profile slots persist in
`uzenet-profiles.bin` in the emulator's working directory. With `--config PATH`, profiles instead persist in
`PATH.uzenet-profiles.bin`. This isolates player names when instances share a
working directory. Other per-ROM files retain their normal directory rules.

## Network prerequisites and current validation limits

Room matchmaking uses the existing provisioned transparent TLS connection.
The adapter consumes Room tickets and peer offers internally and uses the
portable authenticated UDP endpoint for relay/direct traffic. Configure the
server, saved transparent connection and certificate paths through the existing
ESP settings; this profile does not provision credentials automatically.

Build with `FLAG_OPENSSL=1` and OpenSSL headers/libraries available to enable
TLS, HMAC and secure random generation. Without OpenSSL, UART services remain
available but starting the network backend returns a backend error. The default
build leaves this dependency disabled. For a Windows static OpenSSL build, pass
its include and library directories through `EXTRA_CFLAGS` and
`EXTRA_LDFLAGS`, including `-lcrypt32 -lws2_32` after the OpenSSL libraries.
Use a separate object directory when changing dependencies.

The local development Windows build at
`_obj_/uzenet/windows-tls/cuzebox.exe` includes OpenSSL 3.5.8, built from its
official source archive with its published SHA-256 checksum verified. Dependencies
are isolated under `_obj_/uzenet/openssl`; no system compiler installation was
changed. OpenSSL's license is retained there alongside its headers and libraries.

Live network sessions are not restored by savestates. Reset/restore disconnects
the custom endpoint; reconnect and start a new session. Exact input-movie replay
does not capture this external network transport.

Validated locally: adapter tests with/without OpenSSL, loopback authenticated
UDP traffic, rejection of altered/replayed packets, ticket-secret filtering,
and a synthetic AVR cartridge exercising actual GPIO entry, acknowledgement,
UART v4 requests, EXIT and stock AT recovery. Two complete Linux emulator
instances also connected to a temporary local Room server with mutual TLS and
completed 32 exchanges, checking both players' changing inputs on every update.
The Room fixture substitutes only its identity-service test hook; TLS, certificate
binding and UDP handling use its real implementation. The upstream firmware's
separate two-/four-peer endpoint suite passed as well.

A separate game-owned SFA2 integration test also passed on 2026-10-20 using the
packaged cartridge, normal controller input, real PA7/UART v4 at 115200 baud,
and the local Room server. Two instances completed matchmaking, name exchange,
character selection, loading barriers, Ken-versus-Zangief combat and an agreed
pause-menu exit to the title screen. All 1,109 logical updates had identical
canonical game state; the final music and sound-effect underrun counters were
zero. Combat included movement, attacks and damage, rather than just reaching
the fight screen. This exercised a play-and-quit session, not an entire match
through a knockout.

The game-specific harness and its instructions belong to SFA2:
`tools/test_room_emulator.py` and `tools/README-room-test.md`. Local evidence is
in `C:/uzebox-master/demos/SFA2/build/network-room/20261020-115151/result.json`,
including exact ROM/emulator hashes. It uses separate working directories and
temporary certificates and explicit `--config` paths without changing the normal
game configuration. Both players authenticated separately, completed all 1,109
updates, and returned to the title screen with no audio underruns.

Hardware interoperability and Internet latency/loss remain outside this local
validation. The Windows TLS build passed the development smoke suite; the
two-emulator Room and SFA2 tests ran on Linux/WSL. The SFA2 test uses the real Identity service and a private accounts database,
with the two certificates verified as different accounts (1001 and 1002).
Production account provisioning is not covered; the smaller synthetic Room
fixture still uses the identity-service test hook.

```sh
python3 tools/test_uzenet.py
python3 tools/test_uzenet_avr.py ./cuzebox
python3 tools/test_uzenet_room.py ./cuzebox /path/to/uzenet-source
```

These tests need a native C compiler, SDL2 development files, OpenSSL development
files, Python 3, and (for the cartridge test) avr-gcc/avr-objcopy. See
`third_party/uzenet/README.md` for source provenance.

## Player identities and automatic matchmaking

In **CONFIG > DEVICES**, select the ESP module and **UZENET UART V4** firmware.
The identity controls select a PKI slot (0–7), client certificate, private key,
and optional server CA file. **APPLY IDENTITY & RESTART ESP** validates that the
certificate and key match, saves the selected identity, and restarts the ESP.
Restart ends any current network session. A blank CA field preserves the
existing server trust settings. TLS support must be compiled in.

`--config "path/to/player.cfg"` selects a separate saved configuration, including
ESP settings, for this process. Relative paths are resolved from the working
directory; use absolute certificate paths when launching from other directories.
Without this switch, existing `config.cfg` and profile behavior is unchanged.
For two players, use different certificates associated with different accounts;
merely selecting different slot numbers does not create distinct accounts.

Games already have a single match-or-create request: UART v4 **START** with room
key zero requests public matchmaking. The server joins a compatible waiting room
or creates one for subsequent players. Game identity, compatibility value, room
key and session parameters must match; this does not join an already running
incompatible game. SFA2 already uses this flow. The game still handles pending
readiness, cancellation and errors using the firmware protocol.

See [Local network testing](uzenet-local-testing.md) for a launcher that starts
private Room and Identity services and creates distinct test accounts.

UzeSpy connection validation (2026-10-20): the rebuilt legacy AT/transparent
client authenticated as UZESPY00/account 1001, switched from 9600 to 115200,
and remained ready for 300 frames without UART errors. This exposed and fixed
ESP pin reset incorrectly clearing the AVR USART configuration. The reset
regression now checks that host baud, framing and TX/RX enables survive module
restart. Evidence: `C:/uzenet-master/build/uzespy-room/20261020-124211/result.json`.
Radio/updater service behavior and hardware are outside this connection test.
