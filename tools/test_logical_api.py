#!/usr/bin/env python3
"""Real AVR marker execution, condition/timeout, buffer export and checkpoint tests."""
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


def main():
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='logical-api-') as tmp:
        root = Path(tmp)
        (root/'config.cfg').write_text('ResidentBootloader=0\nSerialRoute=0\n')
        # INC r16; STS 0x100,r16; RJMP 0. Input at 0, observation at 3.
        code = struct.pack('<4H', 0x9503, 0x9300, 0x0100, 0xcffc)
        header = bytearray(512); header[:6] = b'UZEBOX'; header[6] = 1
        struct.pack_into('<I', header, 8, len(code))
        (root/'test.uze').write_bytes(header+code)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0)); port = probe.getsockname()[1]
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        proc = subprocess.Popen([exe,'--headless','--paused','--unthrottled','--api-port',str(port),str(root/'test.uze')],
                                cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        api = CUzeBoxApi(port=port)
        try:
            api.wait_ready()
            def cmd(text):
                r=api.command(text); assert r.get('ok'), (text,r); return r
            def run():
                cmd('RESUME')
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    result=cmd('LOGICAL STATUS')
                    if not result['active']:
                        assert cmd('RUN_STATUS')['paused']; return result
                    time.sleep(.005)
                raise AssertionError('logical run hung')
            cmd('LOGICAL CONFIG 0 3 1 10000 0')
            cmd('LOGICAL RANGE SRAM 0x100 1')
            cmd('LOGICAL INPUT 8 2'); cmd('LOGICAL INPUT 0 100')
            cmd('LOGICAL START 3')
            assert run()['records']==3
            assert [cmd(f'LOGICAL READ {i}')['hex'] for i in range(3)]==['01','02','03']
            cmd('LOGICAL CHECKPOINT TAKE 0')
            checkpoint=root/'logical checkpoint.lct'
            cmd(f'LOGICAL CHECKPOINT SAVE {checkpoint}')
            cmd('LOGICAL CONTINUE 6'); assert run()['updates']==6
            expected=[cmd(f'LOGICAL READ {i}') for i in range(6)]
            cmd('LOGICAL CHECKPOINT RESTORE 0')
            cmd('LOGICAL CONTINUE 6'); run()
            assert [cmd(f'LOGICAL READ {i}') for i in range(6)]==expected
            cmd(f'LOGICAL CHECKPOINT LOAD {checkpoint}')
            cmd('LOGICAL CONTINUE 6'); run()
            assert [cmd(f'LOGICAL READ {i}') for i in range(6)]==expected
            before=cmd('READ_REGS')
            original=checkpoint.read_bytes()
            corrupt=root/'corrupt.lct'
            for mutation in ('truncate', 'crc', 'count', 'boolean', 'identity'):
                data=bytearray(original)
                if mutation=='truncate': data=data[:-1]
                elif mutation=='crc': data[-1]^=1
                elif mutation=='identity': data[8]^=1
                else:
                    struct.pack_into('<I',data,24+(22 if mutation=='boolean' else 11)*4,0xffffffff)
                    struct.pack_into('<I',data,16,zlib.crc32(data[24:]))
                corrupt.write_bytes(data)
                assert not api.command(f'LOGICAL CHECKPOINT LOAD {corrupt}').get('ok'),mutation
                assert cmd('READ_REGS')==before,mutation
            out=root/'observations with spaces.jsonl'
            cmd(f'LOGICAL EXPORT {out}')
            lines=[json.loads(line) for line in out.read_text().splitlines()]
            assert len(lines)==7 and lines[0]['ranges'][0]['address']==256
            assert not api.command(f'LOGICAL EXPORT {out}').get('ok')
            cmd('LOGICAL CLEAR')
            cmd('LOGICAL CONFIG 0 3 2 100 0')
            cmd('LOGICAL CONDITION SRAM 0x101 255 1')
            cmd('LOGICAL INPUT 0 1'); cmd('LOGICAL START 0')
            assert run()['status']=='timeout'
            assert cmd('LOGICAL STATUS')['updates']==0
            cmd('LOGICAL CLEAR')
            assert not api.command('LOGICAL CONFIG 99999 3 1 100 0').get('ok')
            assert not api.command('LOGICAL RANGE SRAM 4095 2').get('ok')
            cmd('LOGICAL CONFIG 0 3 2 10000 0'); cmd('LOGICAL INPUT 0 100')
            cmd('LOGICAL START 4'); assert run()['updates']==2
            # Real controller latch: hold B across two logical updates, then
            # release. The observation reads the actual AVR PINA result.
            words=[0xe01c,0xb911,0x9503,0x9300,0x0100,0xe014,0xb912,
                   0xe010,0xb912,0xb120,0x9320,0x0101,0xcff5]
            # Delay each update across video-frame scheduling boundaries.
            words=words[:5]+[0xef3f,0xef4f,0x954a,0xf7f1,0x953a,0xf7d9]+words[5:]
            words[-1]=0xcfef
            struct.pack_into('<I',header,8,len(words)*2)
            (root/'controller.uze').write_bytes(header+struct.pack('<19H',*words))
            cmd('LOAD_ROM WAIT '+str(root/'controller.uze'))
            cmd('LOGICAL CONFIG 2 18 1 2000000 0')
            cmd('LOGICAL RANGE SRAM 0x100 2')
            cmd('LOGICAL INPUT 1 2'); cmd('LOGICAL INPUT 0 1')
            cmd('LOGICAL START 3'); run()
            latched=[cmd(f'LOGICAL READ {i}')['hex'] for i in range(3)]
            assert [bytes.fromhex(x)[0] for x in latched]==[1,2,3],latched
            assert [bytes.fromhex(x)[1]&1 for x in latched]==[0,0,1],latched
            cmd('LOAD_ROM WAIT '+str(root/'test.uze'))
            cmd('LOGICAL CONFIG 0 3 1 10000 0'); cmd('LOGICAL INPUT 0 5000')
            cmd('LOGICAL START 0'); full=run()
            assert full['status']=='buffer full' and full['records']==4096,full
            cmd('QUIT'); proc.wait(timeout=10)
        finally:
            api.close()
            if proc.poll() is None: proc.terminate(); proc.wait(timeout=10)
    print('Logical API: AVR markers, state capture, hit interval, condition, timeout, export and checkpoint PASS')


if __name__=='__main__': main()
