"""Live UART hint/override/config regression; pass the emulator executable."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from cuzebox_api import CUzeBoxApi

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='uart-hint-') as tmp:
    root = Path(tmp)
    for name, peripheral, uart in [('esp', 32, 0), ('ap', 64, 0), ('link', 0, 17),
                                    ('none', 0, 0), ('support', 0, 1), ('conflict', 32, 17)]:
        header = bytearray(512)
        code = bytes([255, 207]) * 31 + bytes([len(name), peripheral])
        header[:6] = b'UZEBOX'
        header[6] = 1
        header[8:12] = len(code).to_bytes(4, 'little')
        header[0x14e:0x152] = zlib.crc32(code).to_bytes(4, 'little')
        header[0x152] = header[0x193] = peripheral
        header[0x19b] = uart
        (root / (name + '.uze')).write_bytes(header + code)
    (root/'config.cfg').write_text('ResidentBootloader=0\nTcpSerialHost="127.0.0.1"\nTcpSerialPort="1"\nTcpSerialAutoReconnect="0"\n')
    def session(initial, check):
        with (root/'output.log').open('w') as log:
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
            process = subprocess.Popen([exe, '--headless', '--paused', '--api-port', '24981', str(root/(initial+'.uze'))], cwd=root, stdout=log, stderr=log, env=env)
            api = CUzeBoxApi(port=24981)
            def command(text):
                result = api.command(text)
                assert result.get('ok'), (text, result)
                return result
            def expect(route, selection):
                state = command('SERIAL_STATUS')
                assert (state['route'], state['route_selection']) == (route, selection), state
            try:
                api.wait_ready()
                check(command, expect)
                command('QUIT')
                process.wait(timeout=15)
                assert process.returncode == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    def first(cmd, expect):
        expect(1, 7)  # New configuration defaults to hints.
        for name, route in [('link', 5), ('ap', 1), ('none', 0), ('support', 0), ('conflict', 0), ('esp', 1)]:
            cmd(f'LOAD_ROM WAIT {root/(name+".uze")}')
            expect(route, 7)
        cmd('SERIAL_SET GAME_OVERRIDE 1')
        cmd('SERIAL_SET ROUTE 6')
        expect(6, 6)
        cmd(f'LOAD_ROM WAIT {root/"link.uze"}')
        expect(5, 7)  # Different game retains global hint.
        cmd(f'LOAD_ROM WAIT {root/"esp.uze"}')
        expect(6, 6)
        cmd('SERIAL_SET GAME_OVERRIDE 0')
        expect(1, 7)
        cmd(f'LOAD_ROM WAIT {root/"link.uze"}')
        cmd('SERIAL_SET ROUTE 7')
        expect(5, 7)
        cmd('RESET')
        expect(5, 7)
    session('esp', first)
    def second(cmd, expect):
        expect(1, 7)  # Saved hint re-resolves for a different startup ROM.
        cmd('SERIAL_SET ROUTE 0')
        expect(0, 0)
    session('esp', second)
    session('link', lambda cmd, expect: expect(0, 0))
    print('UART hint: ROM changes, overrides, reset, and config restart PASS')
