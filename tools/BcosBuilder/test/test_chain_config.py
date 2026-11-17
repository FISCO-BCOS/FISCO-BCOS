import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from config.chain_config import NodeConfig


class NodeConfigKeyPageSizeTest(unittest.TestCase):
    def make_node(self, value):
        config = {
            "node_name": "node0",
            "key_page_size": value,
            "deploy_ip": ["127.0.0.1"],
        }
        agency = SimpleNamespace(name="agency", chain_id="chain0")
        return NodeConfig(
            config, "chain0", "group0", agency, "BcosNodeService", [], False, "max"
        )

    def test_accepts_integer_and_string_zero(self):
        for value in (0, "0"):
            with self.subTest(value=value):
                self.assertEqual(self.make_node(value).key_page_size, value)

    def test_rejects_non_integer_zero_and_positive_values(self):
        for value in (False, 0.0, 1, "10240"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(Exception, "key_page_size must be 0"):
                    self.make_node(value)


if __name__ == "__main__":
    unittest.main()
