#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import json
import unittest
from unittest.mock import patch

import generate


class SchemaGenerationTest(unittest.TestCase):
    def test_current_schema_preserves_optional_block_and_bare_hash(self):
        files = generate.generate()
        header = files[generate.HEADER]
        schemas = json.loads(header.split('R"schema(', 1)[1].split(')schema"', 1)[0])
        balance = schemas["eth_getBalance"]
        storage = schemas["eth_getStorageAt"]
        block = schemas["eth_getBlockByNumber"]

        self.assertEqual((balance["minItems"], balance["maxItems"]), (1, 2))
        self.assertEqual((storage["minItems"], storage["maxItems"]), (2, 3))
        self.assertEqual((block["minItems"], block["maxItems"]), (2, 2))
        self.assertTrue(all(not schema["additionalItems"] for schema in schemas.values()))
        self.assertEqual(block["items"][1], {"type": "boolean"})
        self.assertEqual(balance["items"][0]["maxLength"], 42)
        self.assertEqual(storage["items"][1]["maxLength"], 66)
        self.assertEqual(block["items"][0]["maxLength"], 66)

        quantity, tags, bare_hash = storage["items"][2]["anyOf"]
        self.assertIn("safe", tags["enum"])
        self.assertRegex("0x7fFFFFFFFFFFFFFF", quantity["pattern"])
        self.assertRegex("0x000ff", quantity["pattern"])
        self.assertNotRegex("0x8000000000000000", quantity["pattern"])
        self.assertRegex("0x" + "Af" * 32, bare_hash["pattern"])
        self.assertEqual(block["items"][0]["anyOf"], [{"oneOf": [quantity, tags]}, bare_hash])

    def test_reference_resolution_keeps_combinators(self):
        schema = {"allOf": [
            {"$ref": "#/components/schemas/example", "title": "annotation"},
            {"not": {"enum": ["x"]}},
        ]}
        components = {"example": {"type": "string", "pattern": "^x+$"}}
        self.assertEqual(generate.resolve(schema, components), {"allOf": [
            {"type": "string", "pattern": "^x+$"},
            {"not": {"enum": ["x"]}},
        ]})

    def test_resolver_rejects_unknown_keyword_and_remote_reference(self):
        with self.assertRaisesRegex(ValueError, "Unsupported upstream schema keyword"):
            generate.resolve({"anyOf": [{"unhandled": 1}]}, {})
        with self.assertRaisesRegex(ValueError, "Unsupported schema reference"):
            generate.resolve({"$ref": "https://example.invalid/schema"}, {})
        with self.assertRaisesRegex(ValueError, "Recursive or unresolved"):
            generate.resolve({"$ref": "#/components/schemas/missing"}, {})

    def test_non_trailing_optional_argument_requires_review(self):
        method = {"name": "test", "params": [{"required": False}, {"required": True}]}
        with self.assertRaisesRegex(ValueError, "Non-trailing optional"):
            generate.tuple_schema(method, {}, False)

    def test_upstream_selector_change_requires_overlay_review(self):
        _, methods, components = generate.read_upstream()
        components["BlockNumberOrTagOrHash"]["anyOf"].append({"type": "integer"})
        with self.assertRaisesRegex(ValueError, "Unexpected upstream block selector branches"):
            generate.tuple_schema(methods["eth_getBalance"], components, True)

    def test_upstream_added_parameter_requires_overlay_review(self):
        _, methods, components = generate.read_upstream()
        method = methods["eth_getBalance"]
        method["params"].append({"name": "Extra", "required": False, "schema": {"type": "boolean"}})
        with self.assertRaisesRegex(ValueError, "Unexpected upstream positional parameters"):
            generate.tuple_schema(method, components, True)

    def test_missing_classification_fails(self):
        methods = generate.registered_methods() | {"eth_unclassified"}
        with patch("generate.registered_methods", return_value=methods):
            with self.assertRaisesRegex(ValueError, "eth_unclassified"):
                generate.coverage()


if __name__ == "__main__":
    unittest.main()
