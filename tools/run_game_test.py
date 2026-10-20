#!/usr/bin/env python3
"""Run a declarative game test in a private CUzeBox process (stdlib only)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import random
import shutil
import socket
import subprocess
import time

from cuzebox_api import CUzeBoxApi


def number(value, low=0, high=0xffffffff):
    if isinstance(value, bool):
        raise ValueError('boolean is not an integer')
    n = int(value, 0) if isinstance(value, str) else value
    if not isinstance(n, int) or not low <= n <= high:
        raise ValueError(f'integer outside {low}..{high}: {value!r}')
    return n


def keys(obj, allowed, required=()):
    if not isinstance(obj, dict) or set(obj) - set(allowed) or set(required) - set(obj):
        raise ValueError(f'invalid or missing keys: {obj!r}; allowed: {allowed}')


def marker(value):
    if isinstance(value, str) and re.fullmatch(r'[A-Za-z_.$][\w.$]*', value):
        return value
    return number(value, 0, 32767)


def load_manifest(path):
    m = json.loads(path.read_text(encoding='utf-8'))
    keys(m, ('version', 'name', 'rom', 'elf', 'markers', 'inputs', 'fields',
             'assertions', 'observations', 'wall_timeout_seconds', 'fuzz', 'cycle_budget',
             'coverage', 'devices', 'sd_directory', 'sd_image', 'checkpoint_every'),
         ('version', 'rom', 'markers', 'inputs', 'fields', 'assertions', 'observations'))
    if type(m['version']) is not int or m['version'] != 1:
        raise ValueError('unsupported manifest version')
    for key in ('rom', 'elf', 'sd_image'):
        if key in m:
            p = (path.parent / m[key]).resolve()
            if not p.is_file():
                raise ValueError(f'{key} does not exist: {p}')
            m[key] = str(p)
    if 'sd_image' in m:
        size = Path(m['sd_image']).stat().st_size
        if not 0 < size <= 8*1024*1024 or size % 512 or 'sd_directory' in m:
            raise ValueError('SD image must be 512-byte aligned, at most 8 MiB, and exclusive with sd_directory')
    k = m['markers']
    keys(k, ('input', 'observe', 'every', 'timeout_instructions', 'player'), ('input', 'observe'))
    for key in ('input', 'observe'):
        k[key] = marker(k[key])
        if isinstance(k[key], str) and 'elf' not in m:
            raise ValueError('symbol markers require elf')
    k['every'] = number(k.get('every', 1), 1)
    k['timeout_instructions'] = number(k.get('timeout_instructions', 10000000), 1)
    k['player'] = number(k.get('player', 0), 0, 1)
    m['observations'] = number(m['observations'], 1, 4096)
    m['wall_timeout_seconds'] = number(m.get('wall_timeout_seconds', 60), 1, 86400)
    if 'cycle_budget' in m:
        m['cycle_budget'] = number(m['cycle_budget'], 1)
    m['checkpoint_every'] = number(m.get('checkpoint_every', 0), 0, 4096)
    if 'devices' in m:
        keys(m['devices'], ('spiram_pages', 'sd_timing'))
        if 'spiram_pages' in m['devices']:
            m['devices']['spiram_pages'] = number(m['devices']['spiram_pages'], 0, 9)
        if 'sd_timing' in m['devices'] and m['devices']['sd_timing'] not in ('SLOW', 'NORMAL', 'FAST'):
            raise ValueError('invalid SD timing preset')
    if 'sd_directory' in m:
        p = (path.parent/m['sd_directory']).resolve()
        if not p.is_dir():
            raise ValueError('sd_directory must exist')
        m['sd_directory'] = str(p)
    if 'coverage' in m:
        if not isinstance(m['coverage'], list) or not m['coverage']:
            raise ValueError('coverage requires selected changed-code ranges')
        for r in m['coverage']:
            keys(r, ('start', 'words'), ('start', 'words'))
            r['start'] = marker(r['start'])
            r['words'] = number(r['words'], 1, 32768)
            if isinstance(r['start'], str) and 'elf' not in m:
                raise ValueError('coverage symbols require elf')
    if 'fuzz' in m:
        f = m['fuzz']
        keys(f, ('seed', 'masks', 'entries', 'min_hold', 'max_hold'), ('seed', 'masks', 'entries'))
        f['seed'] = number(f['seed'])
        f['entries'] = number(f['entries'], 1, 4096)
        f['min_hold'] = number(f.get('min_hold', 1), 1, 4096)
        f['max_hold'] = number(f.get('max_hold', f['min_hold']), f['min_hold'], 4096)
        if not isinstance(f['masks'], list) or not f['masks']:
            raise ValueError('fuzz masks must be a nonempty list')
        f['masks'] = [number(v, 0, 65535) for v in f['masks']]
        rng = random.Random(f['seed'])
        m['inputs'] = [{'mask': rng.choice(f['masks']), 'updates': rng.randint(f['min_hold'], f['max_hold'])}
                       for _ in range(f['entries'])]
    if not isinstance(m['inputs'], list) or not 1 <= len(m['inputs']) <= 4096:
        raise ValueError('need 1..4096 input entries')
    for entry in m['inputs']:
        keys(entry, ('mask', 'updates'), ('mask', 'updates'))
        entry['mask'] = number(entry['mask'], 0, 65535)
        entry['updates'] = number(entry['updates'], 1)
    fields = m['fields']
    if not isinstance(fields, dict) or not 1 <= len(fields) <= 16:
        raise ValueError('need 1..16 named fields')
    for name, f in fields.items():
        keys(f, ('region', 'address', 'symbol', 'offset', 'type', 'enum'), ('region', 'type'))
        if f['region'] not in ('SRAM', 'SPI') or f['type'] not in ('u8', 'i8', 'u16', 'i16', 'u32', 'i32'):
            raise ValueError(f'invalid field {name}')
        size = int(f['type'][1:]) // 8
        if ('address' in f) == ('symbol' in f):
            raise ValueError('field needs exactly one of address or symbol')
        if 'symbol' in f:
            if 'elf' not in m or f['region'] != 'SRAM' or not isinstance(marker(f['symbol']), str):
                raise ValueError('field symbols require ELF and SRAM')
            f['offset'] = number(f.get('offset', 0), 0, 4095)
        else:
            if 'offset' in f:
                raise ValueError('offset requires symbol')
            f['address'] = number(f['address'], 0, (4096 if f['region'] == 'SRAM' else 0x1000000) - size)
        if 'enum' in f:
            if not isinstance(f['enum'], dict) or not all(isinstance(v, str) for v in f['enum'].values()):
                raise ValueError('enum must map integer strings to labels')
            f['enum'] = {str(number(k, -2147483648)): v for k, v in f['enum'].items()}
    if not isinstance(m['assertions'], list) or not m['assertions']:
        raise ValueError('at least one assertion is required')
    for a in m['assertions']:
        keys(a, ('field', 'op', 'value', 'values', 'within', 'at'), ('field', 'op'))
        if a['field'] not in fields or a['op'] not in ('eq', 'ne', 'lt', 'le', 'gt', 'ge', 'in', 'changes'):
            raise ValueError(f'invalid assertion: {a}')
        required = 'values' if a['op'] == 'in' else 'within' if a['op'] == 'changes' else 'value'
        if set(a) - {'field', 'op', required, 'at'} or required not in a:
            raise ValueError(f'assertion requires {required}: {a}')
        if required == 'values':
            if not isinstance(a[required], list) or not a[required]:
                raise ValueError('in requires a nonempty values list')
            a[required] = [number(v, -2147483648) for v in a[required]]
        else:
            a[required] = number(a[required], 1 if required == 'within' else -2147483648)
        if 'at' in a:
            a['at'] = number(a['at'], 1, m['observations'])
            if a['op'] == 'changes':
                raise ValueError('changes cannot be limited to one observation')
        if a['op'] == 'changes' and a['within'] >= m['observations']:
            raise ValueError('changes needs more observations than its within interval')
    return m


def decode(fields, hex_bytes):
    data = bytes.fromhex(hex_bytes)
    result, offset = {}, 0
    for name, f in fields.items():
        size = int(f['type'][1:]) // 8
        if offset + size > len(data):
            raise ValueError('short observation')
        result[name] = int.from_bytes(data[offset:offset+size], 'little', signed=f['type'][0] == 'i')
        offset += size
    if offset != len(data):
        raise ValueError('unexpected observation width')
    return result


def check(assertions, values, observation, history):
    """Return first failure; history is private to this run, indexed by assertion."""
    for i, a in enumerate(assertions):
        if 'at' in a and a['at'] != observation:
            continue
        actual, op = values[a['field']], a['op']
        expected = a.get('value')
        if op == 'changes':
            previous, last = history.get(i, (actual, observation))
            if previous != actual:
                last = observation
            history[i] = (actual, last)
            good = observation - last < a['within']
        elif op == 'in':
            good = actual in a['values']
        else:
            good = {'eq': actual == expected, 'ne': actual != expected,
                    'lt': actual < expected, 'le': actual <= expected,
                    'gt': actual > expected, 'ge': actual >= expected}[op]
        if not good:
            return dict(assertion=a, actual=actual, observation=observation)
    return None


def write_json(path, obj):
    path.write_text(json.dumps(obj, indent=2) + '\n', encoding='utf-8')


def sha256(path):
    with open(path, 'rb') as f:
        digest = hashlib.sha256()
        for block in iter(lambda: f.read(1024*1024), b''):
            digest.update(block)
        return digest.hexdigest()


def tree_identity(path):
    files = {}
    for p in sorted(path.rglob('*')):
        if p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction()):
            raise ValueError('SD fixtures must not contain symlinks')
        if p.is_file():
            files[p.relative_to(path).as_posix()] = sha256(p)
    return hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()


def run(manifest_path, exe, output, resume=None):
    # Validate completely before starting an emulator or creating output.
    m = load_manifest(manifest_path)
    exe = exe.resolve(strict=True)
    output = output.resolve()
    if 'sd_directory' in m and output.is_relative_to(Path(m['sd_directory'])):
        raise ValueError('output must be outside the SD fixture tree')
    if any('\n' in str(p) or '\r' in str(p) for p in (exe, output)):
        raise ValueError('paths cannot contain newlines')
    output.mkdir(parents=True, exist_ok=False)
    (output/'cartridge').mkdir()
    write_json(output/'source-manifest.json', json.loads(manifest_path.read_text(encoding='utf-8')))
    # Freeze generated inputs in the reproduction recipe, independent of PRNG versions.
    fuzz = m.pop('fuzz', None)
    if fuzz is not None:
        write_json(output/'fuzz.json', fuzz)
    identities = {'emulator': {'path': str(exe), 'sha256': sha256(exe)}}
    for key in ('rom', 'elf', 'sd_image'):
        if key in m:
            source = Path(m[key])
            target = output / ('cartridge' if key == 'rom' else '') / (key + source.suffix)
            shutil.copyfile(source, target)
            identities[key] = {'path': str(source), 'sha256': sha256(target)}
            m[key] = target.relative_to(output).as_posix()
    write_json(output/'manifest.json', m)
    write_json(output/'identity.json', identities)
    if 'sd_directory' in m:
        source = Path(m['sd_directory'])
        identities['sd_directory'] = {'path': str(source), 'sha256': tree_identity(source)}
        shutil.copytree(source, output/'sd')
        # Preserve pristine assets separately; only the private cartridge copy is writable.
        backing = output/'sd' if resume is None else Path(str(Path(resume).resolve())+'.sd')
        if resume is not None:
            saved = json.loads(Path(str(Path(resume).resolve())+'.json').read_text(encoding='utf-8'))
            if tree_identity(backing) != saved['sd_sha256']:
                raise ValueError('checkpoint SD backing identity mismatch')
        for child in backing.iterdir():
            target = output/'cartridge'/child.name
            if target.exists():
                if resume is not None and child.is_file() and sha256(child) == sha256(target):
                    continue
                raise ValueError('SD asset collides with cartridge filename')
            if child.is_dir():
                shutil.copytree(child, target)
            else:
                shutil.copyfile(child, target)
        m['sd_directory'] = 'sd'
        write_json(output/'manifest.json', m)
        write_json(output/'identity.json', identities)
    devices = m.get('devices', {})
    config = 'ResidentBootloader=0\nSerialRoute="0"\nEspEmulationModel="0"\n'
    if 'spiram_pages' in devices:
        config += f"SpiRamPagesMode={devices['spiram_pages']}\n"
    if 'sd_timing' in devices:
        config += f"SdTimingPreset={('SLOW', 'NORMAL', 'FAST').index(devices['sd_timing'])+1}\n"
    (output/'config.cfg').write_text(config, encoding='utf-8')
    (output/'states').mkdir()
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    api = CUzeBoxApi(port=port)
    result = {'status': 'error', 'observations': 0}
    diagnostics = {}
    history = {}
    signature = hashlib.sha256(json.dumps({
        'manifest': {k: v for k, v in m.items() if k not in ('rom', 'elf', 'sd_image', 'sd_directory', 'name')},
        'identities': {k: v['sha256'] for k, v in identities.items()}
    }, sort_keys=True).encode()).hexdigest()
    proc = None
    with (output/'emulator.log').open('wb') as log, (output/'commands.jsonl').open('w', encoding='utf-8') as commands:
        def cmd(command):
            response = api.command(command)
            commands.write(json.dumps({'command': command, 'response': response}) + '\n')
            commands.flush()
            if not response.get('ok'):
                raise RuntimeError(f'{command}: {response}')
            return response

        def collect(name, command):
            try:
                diagnostics[name] = cmd(command)
            except Exception as exc:
                diagnostics[name] = {'error': str(exc)}

        try:
            proc = subprocess.Popen([str(exe), '--headless', '--paused', '--unthrottled',
                                     '--api-port', str(port), str(output/m['rom'])], cwd=output,
                                    env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
                                    stdout=log, stderr=subprocess.STDOUT)
            diagnostics['hello'] = api.wait_ready()
            cmd('PAUSE')
            cmd('EMU_SET save_path states')
            if 'sd_image' in m:
                cmd('LOGICAL SD LOAD ' + str(output/m['sd_image']))
            cmd('SAVESTATE SAVE 0')
            if 'elf' in m:
                cmd('LOAD_SYMBOLS ' + str(output/m['elf']))
            for f in m['fields'].values():
                if 'symbol' in f:
                    symbol = cmd('READ_SYMBOL ' + f['symbol'])
                    if symbol['kind'] != 'data' or symbol.get('region') != 'SRAM':
                        raise ValueError('field symbol must refer to SRAM')
                    f['address'] = number((symbol['addr'] & 0xfff) + f['offset'], 0,
                                          4096 - int(f['type'][1:])//8)
            write_json(output/'resolved-fields.json', m['fields'])
            coverage = []
            for r in m.get('coverage', []):
                start = r['start']
                if isinstance(start, str):
                    symbol = cmd('READ_SYMBOL ' + start)
                    if symbol['kind'] != 'prog':
                        raise ValueError('coverage needs program symbols')
                    start = symbol['addr']
                if start + r['words'] > 32768:
                    raise ValueError('coverage range exceeds flash')
                coverage.append((start, r['words']))
            if coverage:
                cmd('PROFILE CLEAR')
                cmd('PROFILE ON')
            k = m['markers']
            cmd('LOGICAL CONFIG {input} {observe} {every} {timeout_instructions} {player}'.format(**k))
            for f in m['fields'].values():
                cmd(f"LOGICAL RANGE {f['region']} {f['address']} {int(f['type'][1:])//8}")
            for entry in m['inputs']:
                cmd('LOGICAL INPUT {mask} {updates}'.format(**entry))
            deadline = time.monotonic() + m['wall_timeout_seconds']
            previous_cycle = None
            cycle_samples = []
            first = 1
            if resume is not None:
                resume = Path(resume).resolve()
                saved = json.loads(Path(str(resume)+'.json').read_text(encoding='utf-8'))
                if saved['signature'] != signature or saved['sha256'] != sha256(resume):
                    raise ValueError('checkpoint manifest/build identity mismatch')
                cmd('LOGICAL CHECKPOINT LOAD ' + str(resume))
                observation = number(saved['observation'], 1, m['observations'])
                if cmd('LOGICAL STATUS')['records'] != observation:
                    raise ValueError('checkpoint cursor does not match its sidecar')
                if cmd(f'LOGICAL READ {observation-1}')['cycle'] != saved['cycle']:
                    raise ValueError('checkpoint cycle does not match its sidecar')
                first = observation + 1
                result['observations'] = first-1
                history = {int(k): v for k, v in saved['history'].items()}
                previous_cycle = saved['cycle']
                cycle_samples = saved['cycles']
            with (output/'observations.jsonl').open('w', encoding='utf-8') as observations:
                for n in range(first, m['observations']+1):
                    cmd(f"LOGICAL {'START' if n == 1 else 'CONTINUE'} {n}")
                    cmd('RESUME')
                    while True:
                        if time.monotonic() >= deadline:
                            raise TimeoutError('wall-clock deadline exceeded')
                        status = cmd('LOGICAL STATUS')
                        if not status['active']:
                            break
                        time.sleep(.002)
                    if status['records'] != n or status['status'] != 'target reached':
                        result.update(status='fail', reason='logical execution stopped early', logical=status)
                        break
                    record = cmd(f'LOGICAL READ {n-1}')
                    values = decode(m['fields'], record['hex'])
                    observations.write(json.dumps(dict(record, values=values)) + '\n')
                    observations.flush()
                    result['observations'] = n
                    if previous_cycle is not None:
                        elapsed = (record['cycle'] - previous_cycle) & 0xffffffff
                        cycle_samples.append(elapsed)
                        if elapsed > m.get('cycle_budget', 0xffffffff):
                            result.update(status='fail', reason='cycle budget exceeded',
                                          failure={'observation': n, 'update': record['update'],
                                                   'cycles': elapsed, 'budget': m['cycle_budget']})
                            break
                    previous_cycle = record['cycle']
                    failure = check(m['assertions'], values, n, history)
                    if failure:
                        field = m['fields'][failure['assertion']['field']]
                        failure.update(update=record['update'], region=field['region'], address=field['address'],
                                       actual_label=field.get('enum', {}).get(str(failure['actual'])))
                        result.update(status='fail', reason='assertion failed', failure=failure)
                        break
                    if m['checkpoint_every'] and n % m['checkpoint_every'] == 0:
                        checkpoint = output/f'checkpoint-{n}.lct'
                        cmd('LOGICAL CHECKPOINT SAVE ' + str(checkpoint))
                        sd_hash = None
                        if 'sd_directory' in m:
                            shutil.copytree(output/'cartridge', Path(str(checkpoint)+'.sd'))
                            sd_hash = tree_identity(Path(str(checkpoint)+'.sd'))
                        write_json(Path(str(checkpoint)+'.json'), {'signature': signature,
                                   'sha256': sha256(checkpoint), 'observation': n,
                                   'history': history, 'cycle': record['cycle'], 'cycles': cycle_samples,
                                   'sd_sha256': sd_hash})
                else:
                    result['status'] = 'pass'
            write_json(output/'cycles.json', {'intervals': cycle_samples,
                       'maximum': max(cycle_samples, default=0),
                       'mean': sum(cycle_samples)/len(cycle_samples) if cycle_samples else 0})
            if coverage:
                samples = {}
                for start, words in coverage:
                    for base in range(start, start+words, 32):
                        for sample in cmd(f'READ_PROFILE {base} {min(32, start+words-base)}')['samples']:
                            sample['source'] = cmd(f"SOURCE_AT {sample['pc']}")
                            samples[sample['pc']] = sample
                write_json(output/'coverage.json', {'samples': list(samples.values()),
                           'executed_words': sum(s['hits'] > 0 for s in samples.values()),
                           'selected_words': len(samples)})
        except Exception as exc:
            result.update(status='error', reason=str(exc))
        finally:
            if proc is not None and proc.poll() is None:
                collect('pause', 'PAUSE')
                collect('logical', 'LOGICAL STATUS')
                collect('trace', 'LOGICAL EXPORT ' + str(output/'logical.jsonl'))
                collect('registers', 'READ_REGS')
                collect('state', 'GET_STATE')
                collect('movie_identity', 'INPUT_MOVIE STATUS')
                if 'sd_image' in m:
                    collect('sd_image', 'LOGICAL SD SAVE ' + str(output/'sd-final.img'))
                if result['status'] != 'pass':
                    collect('checkpoint', 'LOGICAL CHECKPOINT TAKE 1')
                    collect('persistent_checkpoint', 'LOGICAL CHECKPOINT SAVE ' + str(output/'failure.lct'))
                    collect('savestate', 'SAVESTATE SAVE 1')
                    collect('screenshot', 'SCREENSHOT ' + str(output/'failure.bmp'))
                collect('quit', 'QUIT')
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=5)
            api.close()
            if proc is not None:
                diagnostics['process_exit_code'] = proc.poll()
            write_json(output/'diagnostics.json', diagnostics)
            write_json(output/'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='new directory; never overwritten')
    parser.add_argument('--resume', type=Path, help='successful interval checkpoint with its .json sidecar')
    args = parser.parse_args()
    try:
        result = run(args.manifest.resolve(), args.emulator, args.output, args.resume)
    except (ValueError, OSError, TypeError, KeyError) as exc:
        result = {'status': 'error', 'reason': str(exc)}
    print(json.dumps(result))
    return {'pass': 0, 'fail': 1, 'error': 2}[result['status']]


if __name__ == '__main__':
    raise SystemExit(main())
