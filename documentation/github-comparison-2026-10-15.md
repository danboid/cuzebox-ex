# GitHub comparison — 2026-10-15

Compared the active local project with a fresh download of
[`weber21w/cuzebox-ex` main at `aad98fe701b8a64f1a44e7d3ecb70165adcbccb1`](https://github.com/weber21w/cuzebox-ex/commit/aad98fe701b8a64f1a44e7d3ecb70165adcbccb1).
That commit is dated 2026-10-14 and merges the file-dialog fixes. GitHub advertised
one branch (`main`) and no tags when checked.

## Result

No missing upstream source-code changes were found. The current GitHub source
matches the preserved original snapshot used for the earlier reconciliation.
There are no newer source commits to merge into the active local project.

All 226 files in the current GitHub tree were compared with the local project
and the preserved original snapshot, normalizing CRLF/LF for text comparison:

| Local comparison | Count |
|---|---:|
| Byte-identical | 11 |
| Identical after line-ending normalization | 149 |
| Different due to existing local changes | 65 |
| Absent locally | 1 |

The absent file is `CONTRIBUTING.md`, which contains only a newline upstream.
Its omission was already recorded in the handoff; it contains no contribution
instructions or code to recover. No file was restored over existing local work.

The only GitHub file without a matching original-snapshot file is the generated
`tools/__pycache__/api_test_runner.cpython-313.pyc`. The Python source is present
and matches upstream. Local ignore rules intentionally exclude generated Python
bytecode, build outputs, machine settings and saved emulator data from publishing.

The 65 differences include local core fixes, capture improvements, GUI/settings,
tests, documentation, generated resources and the pre-existing LICENSE text
variant. They are not newly discovered GitHub updates. Additional local files
contain features and regression coverage absent from GitHub.

## Latest upstream fixes checked

Inspected the ten files changed upstream since parent checkout commit
`0b15c6cd6f705a47ca35c0c4cd4b30f655b3adce`. The local tree retains the SPI timing,
collision, receive-buffer and status-clearing changes, the SPI state field,
probe/test/build target, stable scrollbar IDs, scrolling panel/range behavior
and named-dialog hit testing.

Fresh local checks passed:

- `python3 tools/test_spi_registers.py`
- `make -j2 TSYS=linux OBD=_obj_/review/linux OUT=_obj_/review/linux/cuzebox -f tests/preferences/Makefile filedialog-test`

The comparison clone is isolated under `_obj_/github-current-20261015/`.
Per-file hashes/results are in `_obj_/github-comparison-20261015/comparison.json`;
the file-dialog test log is beside it. The parent Git checkout and active source
code were not modified by this comparison. No commit, push or upload occurred.
