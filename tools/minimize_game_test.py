#!/usr/bin/env python3
"""Bounded delta debugging of a failing manifest's inputs, preserving its failure class."""
import argparse
import copy
import json
from pathlib import Path

from run_game_test import load_manifest, run, write_json


def signature(result):
    if result.get('status') != 'fail':
        return None
    failure = result.get('failure', {})
    # Never accept input exhaustion or a transport/setup error as an assertion reproduction.
    if result.get('reason') == 'assertion failed':
        return (result['reason'], json.dumps(failure['assertion'], sort_keys=True))
    if result.get('reason') == 'cycle budget exceeded':
        return (result['reason'], failure['budget'])
    return None


def pack(masks):
    inputs = []
    for mask in masks:
        if inputs and inputs[-1]['mask'] == mask:
            inputs[-1]['updates'] += 1
        else:
            inputs.append({'mask': mask, 'updates': 1})
    return inputs


def minimize(manifest, emulator, output, max_trials=64, mode='neutral'):
    m = load_manifest(manifest.resolve())
    m.pop('fuzz', None)
    m['checkpoint_every'] = 0
    total = sum(e['updates'] for e in m['inputs'])
    if total > 65536:
        raise ValueError('minimization is bounded to 65536 logical input updates')
    masks = [e['mask'] for e in m['inputs'] for _ in range(e['updates'])]
    output.mkdir(parents=True, exist_ok=False)
    trial = 0

    def evaluate(candidate):
        nonlocal trial
        test = copy.deepcopy(m)
        test['inputs'] = pack(candidate)
        path = output/f'trial-{trial}.json'
        write_json(path, test)
        result = run(path, emulator, output/f'run-{trial}')
        trial += 1
        return signature(result)

    expected = evaluate(masks)
    if expected is None:
        raise ValueError('baseline must reproduce an assertion or cycle-budget failure')
    original = sum(v != 0 for v in masks)
    width = max(1, len(masks)//2)
    while width and trial < max_trials:
        improved = False
        for start in range(0, len(masks), width):
            end = min(start+width, len(masks))
            if not any(masks[start:end]):
                continue
            candidate = masks[:]
            if mode == 'neutral':
                candidate[start:end] = [0]*(end-start)
            else:
                candidate = masks[:start] + masks[end:] + [0]*(end-start)
            if evaluate(candidate) == expected:
                masks = candidate
                improved = True
            if trial >= max_trials:
                break
        if not improved or width == 1:
            width //= 2
    m['inputs'] = pack(masks)
    write_json(output/'minimized.json', m)
    summary = {'trials': trial, 'budget_exhausted': trial >= max_trials,
               'original_active_updates': original, 'active_updates': sum(v != 0 for v in masks),
               'mode': mode, 'failure_signature': expected}
    write_json(output/'result.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--max-trials', type=int, default=64)
    parser.add_argument('--mode', choices=('neutral', 'delete'), default='neutral')
    args = parser.parse_args()
    if args.max_trials < 2:
        parser.error('max-trials must be at least 2')
    print(json.dumps(minimize(args.manifest, args.emulator, args.output.resolve(), args.max_trials, args.mode)))


if __name__ == '__main__':
    main()
