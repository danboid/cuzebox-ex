#!/usr/bin/env python3
"""Actual AVR fault detection, bounded capture, restoration and failure paths."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from cuzebox_api import CUzeBoxApi


def ldi(value):
    return 0xe000 | ((value & 0xf0) << 4) | (value & 15)  # r16


def out(address):
    return 0xb800 | ((address & 0x30) << 5) | 0x100 | (address & 15)


def main():
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='diagnostics-') as tmp:
        root = Path(tmp)
        (root/'config.cfg').write_text('ResidentBootloader=0\nSerialRoute=0\n')
        def rom(name, words):
            code = struct.pack('<'+'H'*len(words), *words)
            header = bytearray(512)
            header[:6] = b'UZEBOX'; header[6] = 1
            struct.pack_into('<I', header, 8, len(code))
            struct.pack_into('<I', header, 334, zlib.crc32(code))
            path = root/(name+'.uze')
            path.write_bytes(header+code)
            return path
        collision = rom('collision', [ldi(0x50), out(0x2c), ldi(1), out(0x2d),
                                      ldi(0x55), out(0x2e), out(0x2e), 0xcfff])
        unread = rom('unread', [ldi(0x50), out(0x2c), ldi(1), out(0x2d),
                                out(0x2e), *([0]*40), out(0x2e), 0xcfff])
        stack = rom('stack', [0x9478, ldi(0x10), out(0x3e), 0xcfff])  # SEI; SPH
        safe_stack = rom('safe-stack', [0x94f8, ldi(0x10), out(0x3e),
                                       ldi(0x80), out(0x3f), ldi(0xff), out(0x3d), 0xcfff])
        sync = rom('sync', [ldi(1), out(4), out(5), ldi(0), out(5),
                            ldi(1), out(5), ldi(0), out(5), 0xcfff])
        # Controller latch pulses; gives exact serialized packets without replacing movie hooks.
        pad = rom('pad', [ldi(4), out(1), out(2), ldi(0), out(2), 0xcffa])
        counter = rom('counter', [0x9503, 0x9300, 0x0100, 0xcffc])
        watchdog = rom('watchdog', [ldi(8),0x9300,0x0060,0xcfff])
        uart = rom('uart', [ldi(24),0x9300,0x00c1,ldi(6),0x9300,0x00c2,
                            ldi(0x55),0x9300,0x00c6,*([0]*300),
                            0x9100,0x00c0,ldi(0x55),0x9300,0x00c6,
                            *([0]*300),0x9100,0x00c0,0xcffd])
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        proc = subprocess.Popen([exe, '--headless', '--paused', '--unthrottled', '--api-port', str(port), str(collision)],
                                cwd=root, env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        api = CUzeBoxApi(port=port)
        try:
            api.wait_ready(20)
            def cmd(command):
                result = api.command(command)
                assert result.get('ok'), (command, result)
                return result
            def run(frames=1):
                cmd(f'RUN_FRAMES {frames}')
                api.wait_run_complete(15)
            def load(path):
                cmd('LOAD_ROM WAIT '+str(path))
            def records():
                status = cmd('DIAGNOSTICS STATUS')
                return [cmd(f'DIAGNOSTICS READ {i}') for i in range(status['first_event'], status['events'])]
            def bundle():
                status = cmd('DIAGNOSTICS STATUS')
                assert status['bundle_path_hex'], status
                path = Path(bytes.fromhex(status['bundle_path_hex']).decode())
                manifest = json.loads((path/'manifest.json').read_text())
                assert manifest['complete'], manifest
                assert not status['armed'] and not status['pending']
                assert (path/'before.state').stat().st_size > 24
                assert (path/'failure.state').stat().st_size > 24
                assert (path/'flash.bin').stat().st_size == 65536
                return path, manifest
            assert cmd('DIAGNOSTICS STATUS')['enabled'] == 0
            run()
            assert not records()
            cmd('DIAGNOSTICS ENABLE')
            load(collision); run()
            event = next(r for r in records() if r['kind'] == 'spi_write_collision')
            assert event['pc_word'] == 6, event
            assert cmd('DIAGNOSTICS STATUS')['instructions'] == 0
            for path, kind in [(unread, 'spi_unread_receive'), (stack, 'stack_high_write_with_interrupts'),
                               (sync, 'unusual_sync_interval')]:
                load(path); run()
                assert any(r['kind'] == kind for r in records()), (kind, records())
            load(safe_stack); run()
            assert not any(r['kind'] == 'stack_high_write_with_interrupts' for r in records())
            load(uart)
            cmd('SERIAL_SET route 6'); cmd('SERIAL_SET uart_profile 2')
            run()
            assert any(r['kind']=='uart_overrun' for r in records()),records()
            cmd('SERIAL_SET route 0')
            cmd('DIAGNOSTICS DISABLE')
            load(collision)
            output = root/'failure output'
            cmd('DIAGNOSTICS TRIGGERS 1')
            cmd('DIAGNOSTICS ARM '+str(output))
            cmd('RESUME')
            deadline = time.monotonic()+15
            while cmd('DIAGNOSTICS STATUS')['armed'] and time.monotonic() < deadline:
                time.sleep(.01)
            first, manifest = bundle()
            assert manifest['trigger']['kind'] == 'spi_write_collision'
            assert manifest['trigger']['pc_word'] == 6
            assert cmd('GET_STATE')['paused']
            execution = [json.loads(s) for s in (first/'execution.jsonl').read_text().splitlines()]
            assert execution[-1]['pc_word'] == 6
            cmd('DIAGNOSTICS RESTORE '+str(first/'before.state'))
            before = cmd('READ_REGS')
            corrupt = root/'corrupt.state'
            data = bytearray((first/'before.state').read_bytes()); data[-1] ^= 1
            corrupt.write_bytes(data)
            assert not api.command('DIAGNOSTICS RESTORE '+str(corrupt))['ok']
            assert cmd('READ_REGS') == before
            load(pad)
            cmd('DIAGNOSTICS TRIGGERS 480')
            cmd('DIAGNOSTICS ARM '+str(output))
            run(3)
            cmd('DIAGNOSTICS TRIGGER 42')
            second, manifest = bundle()
            assert first != second and first.exists()
            assert manifest['trigger']['detail'] == 42
            assert manifest['execution_dropped'] > 0 and manifest['input_dropped'] > 0
            assert len((second/'execution.jsonl').read_text().splitlines()) == 4096
            assert len((second/'inputs.jsonl').read_text().splitlines()) == 1024
            assert not api.command('DIAGNOSTICS RESTORE '+str(first/'before.state'))['ok']  # wrong ROM
            load(counter)
            cmd('WATCH GUARD 0 SRAM W 0x101 0x102')
            cmd('DIAGNOSTICS ARM '+str(output))
            cmd('RESUME')
            deadline = time.monotonic()+15
            while cmd('DIAGNOSTICS STATUS')['armed'] and time.monotonic() < deadline:
                time.sleep(.01)
            _, manifest = bundle()
            assert manifest['trigger']['kind'] == 'guarded_memory_access'
            assert manifest['trigger']['detail'] == 0x100
            cmd('WATCH CLEAR ALL')
            load(watchdog)
            cmd('DIAGNOSTICS ARM '+str(output))
            cmd('RESUME')
            deadline = time.monotonic()+15
            while cmd('DIAGNOSTICS STATUS')['armed'] and time.monotonic() < deadline:
                time.sleep(.01)
            _, manifest = bundle()
            assert manifest['trigger']['kind'] == 'watchdog_reset'
            # Losing the output directory must disarm, pause, and report failure.
            bad = root/'removed'
            cmd('DIAGNOSTICS ARM '+str(bad)); bad.rmdir()
            cmd('DIAGNOSTICS TRIGGER 9')
            assert 'incomplete' in cmd('DIAGNOSTICS STATUS')['status']
            assert cmd('GET_STATE')['paused']
            for value in ['-1', '512', 'garbage', '0x100000000']:
                assert not api.command('DIAGNOSTICS MASK '+value)['ok']
            assert not api.command('DIAGNOSTICS READ 999999')['ok']
            cmd('DIAGNOSTICS DISARM'); cmd('DIAGNOSTICS CLEAR')
            assert cmd('DIAGNOSTICS STATUS')['events'] == 0
            print('PASS: real AVR diagnostics, disabled path, safe stack sequence, automatic guard/collision capture, bounded rings, restore/CRC/identity, unique bundles and failed output')
        finally:
            api.close(); proc.terminate(); proc.wait(timeout=10)
        from run_game_test import run as run_game
        manifest = {'version':1,'rom':'counter.uze',
                    'markers':{'input':0,'observe':3,'timeout_instructions':1000},
                    'inputs':[{'mask':0,'updates':2}],'observations':1,
                    'fields':{'counter':{'region':'SRAM','address':'0x100','type':'u8'}},
                    'assertions':[{'field':'counter','op':'eq','value':99}]}
        (root/'assertion.json').write_text(json.dumps(manifest))
        result = run_game(root/'assertion.json',Path(exe),root/'harness',failure_capture=True)
        assert result['status']=='fail',result
        bundles=list((root/'harness/failures').glob('*/manifest.json'))
        assert len(bundles)==1
        assert json.loads(bundles[0].read_text())['trigger']['kind']=='assertion_failure'
        print('PASS: watchdog capture and game-harness assertion bundle')


if __name__ == '__main__':
    main()
