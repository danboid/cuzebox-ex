#!/usr/bin/env python3
"""Run development regressions with retained logs and a machine-readable summary."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path, help='existing full debugger/API build')
    parser.add_argument('--output', required=True, type=Path, help='new artifact directory')
    parser.add_argument('--full', action='store_true', help='also run replay and manifest/fuzz/minimizer tests')
    parser.add_argument('--network', action='store_true', help='also build/run ESP tests; requires bash and C compiler')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    exe, output = args.emulator.resolve(), args.output.resolve()
    if not exe.is_file():
        parser.error(f'emulator not found: {exe}')
    try:
        output.mkdir(parents=True, exist_ok=False)
    except OSError as exc:
        parser.error(str(exc))
    python = [sys.executable, '-X', 'utf8']
    checks = [('source-audit', python + ['tools/source_audit.py']),
              ('headless', python + ['tools/test_headless_api.py', str(exe)]),
              ('logical', python + ['tools/test_logical_api.py', str(exe)])]
    if args.full:
        checks += [('movie', python + ['tools/test_input_movie_api.py', str(exe)]),
                   ('game-runner', python + ['tools/test_game_test_runner.py', str(exe)])]
    if args.network:
        checks.append(('network', ['bash', 'tests/esp_at/run.sh']))
    results = []
    for name, command in checks:
        print(f'Running {name}...', flush=True)
        start = time.monotonic()
        with (output/f'{name}.log').open('w', encoding='utf-8') as log:
            try:
                code = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT,
                                      timeout=1800, check=False).returncode
                error = None
            except (OSError, subprocess.TimeoutExpired) as exc:
                code, error = 2, str(exc)
                log.write(error + '\n')
        results.append({'name': name, 'exit_code': code, 'seconds': round(time.monotonic()-start, 2),
                        'error': error, 'log': f'{name}.log'})
        summary = {'status': 'pass' if all(r['exit_code'] == 0 for r in results) else 'fail',
                   'emulator': str(exe), 'tests': results, 'complete': len(results) == len(checks)}
        (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
        print(f'{name}: {"PASS" if code == 0 else "FAIL"} ({results[-1]["seconds"]}s)', flush=True)
    print(f'Logs and summary: {output}')
    return 0 if summary['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
