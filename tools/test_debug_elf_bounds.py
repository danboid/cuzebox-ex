"""Malformed ELF section bounds for both source-line and variable readers."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile

from test_debug_source import make_elf as make_source
from test_debug_dwarf import make_elf as make_variables

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        raise SystemExit("No host compiler")
    with tempfile.TemporaryDirectory(prefix="cuzebox-elf-bounds-") as folder:
        root = Path(folder)
        harness = root / "test.c"
        harness.write_text('''#include <string.h>
#include "debug_source.h"
#include "debug_dwarf.h"
int main(int argc, char **argv){
 int i;
 if (argc < 4) return 1;
 int (*load)(char const*) = !strcmp(argv[1], "source") ? cu_debug_source_load_elf : cu_debug_dwarf_load_elf;
 if (!load(argv[2])) return 1;
 for (i = 3; i < argc; ++i){ if (load(argv[i])) return 2; }
 cu_debug_source_reset(); cu_debug_dwarf_reset();
 return 0;
}
''', encoding="utf-8")
        executable = root / ("test.exe" if os.name == "nt" else "test")
        subprocess.run([compiler, "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra",
                        *shlex.split(os.environ.get("EXTRA_CFLAGS", "")), "-I", str(ROOT),
                        str(ROOT / "debug_source.c"), str(ROOT / "debug_dwarf.c"),
                        str(harness), "-o", str(executable)], check=True)
        for name, factory in (("source", make_source), ("variables", make_variables)):
            valid = root / (name + ".elf")
            factory(valid)
            data = valid.read_bytes()
            table = struct.unpack_from("<I", data, 32)[0]
            entry = struct.unpack_from("<H", data, 46)[0]
            names_size_offset = table + entry + 20
            names_size = struct.unpack_from("<I", data, names_size_offset)[0]
            mutations = [(32, 0xFFFFFFFF), (names_size_offset, names_size - 1),
                         (table + entry + 16, 0xFFFFFFF0),
                         (table + 2 * entry + 16, 0xFFFFFFF0)]
            fixtures = []
            for index, (offset, value) in enumerate(mutations):
                bad = bytearray(data)
                struct.pack_into("<I", bad, offset, value)
                path = root / f"{name}-bad-{index}.elf"
                path.write_bytes(bad)
                fixtures.append(str(path))
            subprocess.run([str(executable), name, str(valid), *fixtures], check=True)
    print("ELF bounds regression: PASS (both readers, names and offset overflow)")


if __name__ == "__main__":
    main()
