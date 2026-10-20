#!/usr/bin/env python3
"""Compare exported logical observations without comparing instruction timing."""
import json
from pathlib import Path
import sys


def compare(expected_path, actual_path):
    def read(path):
        with Path(path).open(encoding='utf-8') as source:
            rows=[json.loads(line) for line in source if line.strip()]
        if not rows or rows[0].get('format')!='CUZEBOX_LOGICAL_1':
            raise ValueError(f'{path}: unsupported logical trace')
        if rows[0].get('records')!=len(rows)-1:
            raise ValueError(f'{path}: incomplete observation export')
        return rows[0], rows[1:]
    eh, expected=read(expected_path); ah, actual=read(actual_path)
    if eh['ranges']!=ah['ranges']:
        print('Observation layouts differ; supply a project-specific field mapping.'); return 1
    for i,(a,b) in enumerate(zip(expected,actual)):
        if a['update']!=b['update']:
            print(f'Record {i}: expected update {a["update"]}, got {b["update"]}'); return 1
        left,right=bytes.fromhex(a['hex']),bytes.fromhex(b['hex'])
        width=sum(r['length'] for r in eh['ranges'])
        if len(left)!=width or len(right)!=width: raise ValueError('Invalid record length')
        for offset,(x,y) in enumerate(zip(left,right)):
            if x!=y:
                for r in eh['ranges']:
                    if offset<r['length']:
                        print(f'Update {a["update"]}, {r["region"]} ${r["address"]+offset:X}: expected ${x:02X}, got ${y:02X}')
                        return 1
                    offset-=r['length']
    if len(expected)!=len(actual):
        print(f'Observation count differs: {len(expected)} expected, {len(actual)} actual'); return 1
    if eh.get('status')!=ah.get('status'):
        print(f'Terminal reasons differ: {eh.get("status")} / {ah.get("status")}'); return 1
    if eh.get('status') in ('timeout','buffer full','invalid memory range','running'):
        print(f'Both runs stopped with {eh["status"]}; comparison is incomplete.'); return 1
    print(f'{len(expected)} logical observations match (PC/cycle timing ignored).'); return 0


if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('Usage: compare_logical_traces.py expected.jsonl actual.jsonl')
    raise SystemExit(compare(*sys.argv[1:]))
