#!/usr/bin/env python3
"""Exercise manifests and failure bundles against a real AVR counter cartridge."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile

from run_game_test import check, decode, load_manifest, run
from minimize_game_test import minimize


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='game-test-runner-') as tmp:
        root = Path(tmp)
        code = struct.pack('<4H', 0x9503, 0x9300, 0x0100, 0xcffc)
        header = bytearray(512)
        header[:6] = b'UZEBOX'
        header[6] = 1
        struct.pack_into('<I', header, 8, len(code))
        (root/'counter.uze').write_bytes(header+code)
        manifest = {'version': 1, 'rom': 'counter.uze',
                    'markers': {'input': 0, 'observe': 3, 'timeout_instructions': 1000},
                    'inputs': [{'mask': 0, 'updates': 20}], 'observations': 5,
                    'fields': {'counter': {'region': 'SRAM', 'address': '0x100', 'type': 'u8',
                                           'enum': {'3': 'third'}}},
                    'assertions': [{'field': 'counter', 'op': 'le', 'value': 5},
                                   {'field': 'counter', 'op': 'eq', 'value': 5, 'at': 5},
                                   {'field': 'counter', 'op': 'changes', 'within': 2}]}
        path = root/'test.json'

        def execute(name, m):
            path.write_text(json.dumps(m), encoding='utf-8')
            result = run(path, exe, root/name)
            assert (root/name/'result.json').exists()
            return result

        passed = execute('pass with spaces', manifest)
        assert passed == {'status': 'pass', 'observations': 5}, passed
        extended = copy.deepcopy(manifest)
        extended.update(checkpoint_every=2, cycle_budget=5, coverage=[{'start': 0, 'words': 4}])
        extended['fuzz'] = {'seed': 42, 'masks': [0, 1, 8], 'entries': 20}
        extended['devices'] = {'spiram_pages': 1, 'sd_timing': 'FAST'}
        (root/'sd.img').write_bytes(bytes(range(256))*4)
        extended['sd_image'] = 'sd.img'
        ext_result = execute('extended', extended)
        assert ext_result['status'] == 'pass', ext_result
        resume = run(root/'extended/manifest.json', exe, root/'resumed', root/'extended/checkpoint-2.lct')
        assert resume['status'] == 'pass', resume
        original_trace = (root/'extended/logical.jsonl').read_text().splitlines()[1:]
        resumed_trace = (root/'resumed/logical.jsonl').read_text().splitlines()[1:]
        assert original_trace == resumed_trace
        assert (root/'resumed/sd-final.img').read_bytes() == (root/'sd.img').read_bytes()
        coverage = json.loads((root/'extended/coverage.json').read_text())
        assert coverage['executed_words'] == 3, coverage
        assert json.loads((root/'extended/cycles.json').read_text())['maximum'] == 5
        directory = copy.deepcopy(manifest)
        (root/'assets').mkdir()
        (root/'assets/DATA.BIN').write_bytes(b'fixture')
        directory.update(sd_directory='assets', checkpoint_every=2)
        assert execute('directory', directory)['status'] == 'pass'
        assert (root/'directory/cartridge/DATA.BIN').read_bytes() == b'fixture'
        continued = run(root/'directory/manifest.json', exe, root/'directory-resumed',
                        root/'directory/checkpoint-2.lct')
        assert continued['status'] == 'pass', continued
        costly = copy.deepcopy(manifest)
        costly['cycle_budget'] = 4
        budget = execute('budget', costly)
        assert budget['reason'] == 'cycle budget exceeded', budget
        broken = copy.deepcopy(manifest)
        broken['assertions'] = [{'field': 'counter', 'op': 'lt', 'value': 3}]
        failed = execute('fail', broken)
        assert failed['status'] == 'fail' and failed['observations'] == 3, failed
        assert failed['failure']['actual_label'] == 'third', failed
        diagnostics = json.loads((root/'fail/diagnostics.json').read_text())
        for key in ('savestate', 'screenshot', 'checkpoint', 'trace'):
            assert diagnostics[key].get('ok'), diagnostics
        assert len(list((root/'fail/states').rglob('*.czs'))) == 2, 'missing saved states'
        assert (root/'fail/failure.bmp').stat().st_size > 54
        # The copied manifest and ROM can reproduce the same first failure.
        repeated = run(root/'fail/manifest.json', exe, root/'repeat')
        assert repeated == failed, (repeated, failed)
        broken['inputs'][0]['mask'] = 8
        path.write_text(json.dumps(broken))
        reduced = minimize(path, exe, root/'minimized', max_trials=12)
        assert reduced['active_updates'] == 0, reduced
        exhausted = copy.deepcopy(manifest)
        exhausted['inputs'][0]['updates'] = 2
        early = execute('early', exhausted)
        assert early['status'] == 'fail' and early['logical']['status'] == 'inputs complete', early
        timeout = copy.deepcopy(manifest)
        timeout['markers']['input'] = 4
        timeout['markers']['observe'] = 4
        stopped = execute('timeout', timeout)
        assert stopped['status'] == 'fail' and stopped['logical']['status'] == 'timeout', stopped
        timeout['markers']['timeout_instructions'] = 0xffffffff
        timeout['wall_timeout_seconds'] = 1
        wall = execute('wall timeout', timeout)
        assert wall['status'] == 'error' and wall['reason'] == 'wall-clock deadline exceeded', wall
        stagnant = copy.deepcopy(manifest)
        stagnant['fields']['counter']['address'] = 257
        stagnant['assertions'] = [{'field': 'counter', 'op': 'changes', 'within': 2}]
        progress = execute('progress', stagnant)
        assert progress['status'] == 'fail' and progress['observations'] == 3, progress
        invalid = copy.deepcopy(manifest)
        invalid['assertions'][0]['op'] = 'eval'
        path.write_text(json.dumps(invalid))
        try:
            load_manifest(path)
            raise AssertionError('invalid operation accepted')
        except ValueError:
            pass
        path.write_text(json.dumps(manifest))
        try:
            run(path, exe, root/'fail')
            raise AssertionError('existing bundle overwritten')
        except FileExistsError:
            pass
        assert decode({'signed': {'type': 'i16'}, 'unsigned': {'type': 'u32'}}, 'feff78563412') == {
            'signed': -2, 'unsigned': 0x12345678}
        for op, value in [('eq', 3), ('ne', 2), ('lt', 4), ('le', 3), ('gt', 2), ('ge', 3)]:
            assert check([{'field': 'x', 'op': op, 'value': value}], {'x': 3}, 1, {}) is None
        assert check([{'field': 'x', 'op': 'in', 'values': [2, 3]}], {'x': 3}, 1, {}) is None
    print('Game test runner: pass, first failure, reproduction, progress, timeout, bundles and validation PASS')


if __name__ == '__main__':
    main()
