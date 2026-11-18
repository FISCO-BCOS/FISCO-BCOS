import os
import subprocess
import tempfile
import unittest
from pathlib import Path


TABLE_META_HEX = (
    "000000000000000000000100000000000000000000000000000000"
    "04000000000000006e616d6501000b00"
)
PAGE_HEX = "00000000000100000004000000000000006e616d65070000007061796c6f6164"
DEFAULT_BINARY = (
    Path(__file__).resolve().parents[3] / "build/tools/storage-tool/storageTool"
)


class StorageToolKeyPageTest(unittest.TestCase):
    def setUp(self):
        self.binary = Path(os.environ.get("STORAGE_TOOL_BIN", DEFAULT_BINARY))
        if not self.binary.exists():
            self.skipTest(f"storageTool not found: {self.binary}")
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.data_path = self.root / "data"
        self.genesis = self.root / "missing.genesis"
        self.raw_config = self.write_config("raw.ini", 0)
        self.omitted_config = self.write_config("omitted.ini", None)
        self.write_hex_row("s_tables", b"t_page", b"value".hex())
        self.write_hex_row("t_page", b"", TABLE_META_HEX)
        self.write_hex_row("t_page", b"name", PAGE_HEX)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_config(self, name, key_page_size):
        path = self.root / name
        configured = "" if key_page_size is None else f"key_page_size={key_page_size}\n"
        path.write_text(
            "[chain]\n"
            "chain_id=chain0\n"
            "group_id=group0\n"
            "sm_crypto=false\n"
            "[storage]\n"
            "type=RocksDB\n"
            f"data_path={self.data_path}\n"
            f"{configured}"
            "[log]\n"
            "level=INFO\n"
        )
        return path

    def run_tool(self, config, *args):
        env = os.environ.copy()
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        result = subprocess.run(
            [str(self.binary), "--config", str(config), "--genesis", str(self.genesis), *args],
            cwd=self.root,
            env=env,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            self.fail(
                f"storageTool exited with {result.returncode}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        return result

    def write_hex_row(self, table, key, value_hex):
        self.run_tool(
            self.raw_config, "--hex", "true", "--write", table, key.hex(), value_hex
        )

    def iterate(self, config):
        result = self.run_tool(config, "--hex", "true", "--iterate", "t_page")
        return result.stdout, (self.root / "t_page.txt").read_text()

    def test_missing_config_detects_page_and_preserves_writes(self):
        stdout, output = self.iterate(self.omitted_config)
        self.assertIn("iterate use key page", stdout)
        self.assertIn("[key=6e616d65] [value=7061796c6f6164]", output)

        self.run_tool(self.omitted_config, "--write", "t_page", "name", "updated")

        stdout, output = self.iterate(self.omitted_config)
        self.assertIn("iterate use key page", stdout)
        self.assertIn("[key=6e616d65] [value=75706461746564]", output)

    def test_explicit_zero_keeps_config_skipped_semantics(self):
        stdout, output = self.iterate(self.raw_config)
        self.assertNotIn("iterate use key page", stdout)
        self.assertIn(f"[key=745f706167653a6e616d65] [value={PAGE_HEX}]", output)


if __name__ == "__main__":
    unittest.main()
