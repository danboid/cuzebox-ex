"""Cross-tool regression: pass packrom, header_test, and optionally a game HEX."""
from pathlib import Path
import subprocess
import sys
import tempfile

pack, reader = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    if len(sys.argv) > 3:
        hexfile = str(Path(sys.argv[3]).resolve())
    else:
        hexfile = str(root / 'fixture.hex')
        Path(hexfile).write_text(':02000000FFCF30\n:00000001FF\n')
    def run(properties, good=True):
        prop = root / 'game.properties'
        out = root / 'game.uze'
        prop.write_text(properties)
        out.unlink(missing_ok=True)
        result = subprocess.run([pack, hexfile, str(out), str(prop)], capture_output=True)
        assert (result.returncode == 0) == good, result.stderr.decode(errors='replace')
        if good:
            subprocess.run([reader, str(out)], check=True)
            return out.read_bytes()
        assert not out.exists()
    old = run('lightgun=support\nmultitap=support\nesp8266=default\n')
    assert old[0x152] == 44 and old[0x193] == 32 and old[0x19B] == 0
    supported = run('esp8266=default\nuzelink=support\n')
    assert supported[0x19B] == 1
    default = run('esp8266=support\nuzelink=default\n')
    assert default[0x19B] == 0x11 and default[0x193] == 0
    for esp in ('esp8266', 'esp8266_ap'):
        for props in (f'{esp}=default\nuzelink=default\n', f'uzelink=default\n{esp}=default\n'):
            run(props, False)
    run('uzelink=garbage\n', False)
    run('uzelink=default\n')
    subprocess.run([pack, str(root/'game.uze'), str(root/'out.hex'), str(root/'out.properties'), str(root/'out.dat'), str(root/'out.raw')], check=True, capture_output=True)
    assert 'uzelink=default' in (root/'out.properties').read_text()
    (root/'patch.properties').write_text('esp8266=default\n')
    result = subprocess.run([pack, str(root/'game.uze'), str(root/'patched.uze'), str(root/'patch.properties')], capture_output=True)
    assert result.returncode != 0 and not (root/'patched.uze').exists()
    # Unknown UART bits survive emulator export for future connection types.
    future = bytearray(default)
    future[0x19B] = 0xFF
    (root/'future.uze').write_bytes(future)
    subprocess.run([reader, str(root/'future.uze')], check=True)
    print('Packrom UART properties, conflicts, extraction: PASS')
