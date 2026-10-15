"""Compare matched old/new Timer1 harnesses with rendering and audio disabled."""
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile


def main():
    binaries = [Path(arg).resolve() for arg in sys.argv[1:3]]
    if len(binaries) != 2:
        raise SystemExit("usage: benchmark_timer1.py BASELINE CURRENT")
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    samples = [{"CTC": [], "normal": []} for _ in binaries]
    expected_cycles = None
    with tempfile.TemporaryDirectory(prefix="timer1-bench-") as cwd:
        for run in range(5):
            for index in ([0, 1] if run % 2 == 0 else [1, 0]):
                output = subprocess.check_output(
                    [str(binaries[index]), "--benchmark"], cwd=cwd, text=True, timeout=30)
                for mode, seconds, cycles in re.findall(
                        r"(CTC|normal) CPU: ([0-9.]+) s, cycles ([0-9]+)", output):
                    if int(cycles) not in (288000000, 288000001):
                        raise RuntimeError("CPU benchmark workload changed: " + output)
                    if expected_cycles is None:
                        expected_cycles = int(cycles)
                    if int(cycles) != expected_cycles:
                        raise RuntimeError("Baseline/current cycle counts differ: " + output)
                    samples[index][mode].append(float(seconds))
    result = {}
    for mode in ("CTC", "normal"):
        old = statistics.median(samples[0][mode])
        new = statistics.median(samples[1][mode])
        result[mode] = {"baseline_seconds": old, "current_seconds": new,
                        "change_percent": round(100 * (new / old - 1), 2),
                        "baseline_samples": samples[0][mode], "current_samples": samples[1][mode]}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
