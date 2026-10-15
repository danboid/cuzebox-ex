"""Exercise the standalone launcher on Linux/WSL without running SDL."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "packaging/rocknix/start_cuzebox.sh"


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cuzebox-launcher-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / "user config"
        self.log = self.root / "launch.log"
        self.binary = self.root / "fake emulator"
        self.binary.write_text(
            '#!/bin/sh\n{ printf "%s\\n" "$PWD" "$#"; '
            'printf "arg:%s\\n" "$@"; '
            'printf "db:%s\\n" "$SDL_GAMECONTROLLERCONFIG_FILE"; } > "$TEST_LOG"\n'
        )
        self.binary.chmod(0o755)
        self.defaults = self.root / "default.cfg"
        self.defaults.write_text("DisplayFullscreen=1\n")
        self.db = self.root / "controller database.txt"
        self.db.write_text("test\n")
        self.profile = self.root / "profile"
        self.profile.write_text('set_kill() { printf "%s\\n" "$*" > "$TEST_KILL_LOG"; }\n')
        self.env = dict(os.environ, CUZEBOX_BINARY=str(self.binary),
                        CUZEBOX_CONFIG_HOME=str(self.config),
                        CUZEBOX_DEFAULT_CONFIG=str(self.defaults),
                        CUZEBOX_CONTROLLER_DB=str(self.db),
                        CUZEBOX_SYSTEM_PROFILE=str(self.profile),
                        TEST_LOG=str(self.log), TEST_KILL_LOG=str(self.root / "kill.log"))

    def launch(self, *args):
        return subprocess.run(["bash", str(LAUNCHER), *args], cwd=self.root,
                              env=self.env, capture_output=True, text=True)

    def test_relative_rom_spaces_and_first_launch(self):
        rom = self.root / "game with spaces.hex"
        rom.write_text("test")
        result = self.launch(rom.name)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = self.log.read_text().splitlines()
        self.assertEqual(lines[:3], [str(self.config), "1", "arg:" + str(rom)])
        self.assertEqual(lines[3], "db:" + str(self.db))
        self.assertEqual((self.config / "config.cfg").read_text(), self.defaults.read_text())
        self.assertEqual((self.root / "kill.log").read_text().strip(), "set -TERM cuzebox-ex")

    def test_existing_preferences_and_bootloader_launch(self):
        self.config.mkdir()
        cfg = self.config / "config.cfg"
        cfg.write_text("user preferences\n")
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines()[1], "0")
        self.assertEqual(cfg.read_text(), "user preferences\n")

    def test_missing_rom_does_not_launch_or_create_config(self):
        result = self.launch("missing.uze")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ROM not found", result.stderr)
        self.assertFalse(self.log.exists())
        self.assertFalse(self.config.exists())

    def test_missing_binary(self):
        self.binary.unlink()
        result = self.launch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("executable missing", result.stderr)
        self.assertFalse(self.config.exists())


if __name__ == "__main__":
    unittest.main()
