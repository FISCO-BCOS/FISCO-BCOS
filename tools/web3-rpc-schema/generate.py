#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate the Web3 RPC pilot schema from the pinned local OpenRPC source files."""

import argparse
import hashlib
import json
import re
from pathlib import Path

import yaml


DIRECTORY = Path(__file__).resolve().parent
ROOT = DIRECTORY.parents[1]
RPC = ROOT / "bcos-rpc/bcos-rpc"
HEADER = RPC / "validator/ParameterSchemas.generated.h"
MANIFEST = DIRECTORY / "generated-manifest.json"
REF_PREFIX = "#/components/schemas/"
ANNOTATIONS = {"title", "description"}
COMBINATORS = {"allOf", "anyOf", "oneOf"}
SCALAR_KEYWORDS = {"type", "enum", "pattern", "required", "maxLength"}
SCHEMA_KEYWORDS = {"additionalProperties", "not"}
GENERATED_KEYWORDS = SCALAR_KEYWORDS | COMBINATORS | {"items", "minItems", "maxItems", "additionalItems"}
OVERLAY_FIELDS = {
    "eth_getBalance": [
        ("Address", True, "address"), ("Block", False, "BlockNumberOrTagOrHash")
    ],
    "eth_getStorageAt": [
        ("Address", True, "address"), ("Storage slot", True, "bytesMax32"),
        ("Block", False, "BlockNumberOrTagOrHash"),
    ],
    "eth_getBlockByNumber": [
        ("Block", True, "BlockNumberOrTag"), ("Hydrated transactions", True, None)
    ],
}


def json_text(value):
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_upstream():
    upstream = DIRECTORY / "upstream"
    pin = json.loads((DIRECTORY / "upstream.json").read_text())
    sources = {path.relative_to(upstream).as_posix() for path in upstream.rglob("*") if path.is_file()}
    if sources != set(pin["sha256"]):
        raise ValueError("Upstream snapshot files differ from upstream.json")

    methods = {}
    components = {}
    for name in sorted(sources):
        data = (upstream / name).read_bytes()
        if sha256(data) != pin["sha256"][name]:
            raise ValueError(f"Upstream digest mismatch: {name}")
        if name.startswith("src/eth/"):
            for method in yaml.safe_load(data):
                method_name = method["name"]
                if method_name in methods:
                    raise ValueError(f"Duplicate OpenRPC method: {method_name}")
                methods[method_name] = method
        elif name.startswith("src/schemas/"):
            definitions = yaml.safe_load(data)
            if duplicates := components.keys() & definitions.keys():
                raise ValueError(f"Duplicate schema components: {sorted(duplicates)}")
            components.update(definitions)

    return pin, methods, components


def resolve(schema, components, stack=()):
    """Resolve the Request Schema Object without rewriting any combinator semantics."""
    if isinstance(schema, bool):
        return schema
    schema = {key: value for key, value in schema.items() if key not in ANNOTATIONS}
    if "$ref" in schema:
        ref = schema["$ref"]
        if set(schema) != {"$ref"} or not ref.startswith(REF_PREFIX):
            raise ValueError(f"Unsupported schema reference: {schema}")
        name = ref.removeprefix(REF_PREFIX)
        if name in stack or name not in components:
            raise ValueError(f"Recursive or unresolved schema reference: {ref}")
        return resolve(components[name], components, (*stack, name))

    resolved = {}
    for key, value in schema.items():
        if key in SCALAR_KEYWORDS:
            resolved[key] = value
        elif key in COMBINATORS:
            resolved[key] = [resolve(child, components, stack) for child in value]
        elif key in SCHEMA_KEYWORDS:
            resolved[key] = resolve(value, components, stack)
        elif key == "properties":
            resolved[key] = {name: resolve(child, components, stack) for name, child in value.items()}
        else:
            raise ValueError(f"Unsupported upstream schema keyword: {key}")
    return resolved


def overlay_parameter(component, source, resolved, components):
    """Apply decoder compatibility only to parameters of the selected pilot methods."""
    if source.get("$ref") != REF_PREFIX + component:
        raise ValueError(f"Expected upstream parameter schema reference to {component}")
    if component in {"address", "bytesMax32"}:
        if set(resolved) != {"type", "pattern"} or resolved["type"] != "string":
            raise ValueError(f"Unexpected upstream hex string schema: {component}")
        if component == "address":
            # The decoder strips optional 0x / 0X prefixes and accepts mixed case.
            return resolved | {"pattern": "^(0[xX])?[0-9a-fA-F]{40}$", "maxLength": 42}
        # Retain short, leading-zero, uppercase and optionally prefixed storage slots.
        return resolved | {"pattern": "^(0[xX])?[0-9a-fA-F]{0,64}$", "maxLength": 66}

    composition = "oneOf" if component == "BlockNumberOrTag" else "anyOf"
    branches = components[component][composition]
    expected_refs = ["uint", "BlockTag"]
    expected_objects = []
    if composition == "anyOf":
        # Review changes to the upstream EIP-1898 object branches before omitting them.
        expected_refs.append("hash32")
        expected_objects = [
            {
                "type": "object",
                "additionalProperties": False,
                "required": ["blockNumber"],
                "properties": {"blockNumber": resolve(components["uint"], components)},
            },
            {
                "type": "object",
                "additionalProperties": False,
                "required": ["blockHash"],
                "properties": {
                    "blockHash": resolve(components["hash32"], components),
                    "requireCanonical": {"type": "boolean"},
                },
            },
        ]
    reference_count = len(expected_refs)
    if (
        set(resolved) != {composition}
        or [branch.get("$ref") for branch in branches[:reference_count]]
        != [REF_PREFIX + name for name in expected_refs]
        or resolved[composition][reference_count:] != expected_objects
    ):
        raise ValueError(f"Unexpected upstream block selector branches: {component}")

    original_quantity, tag, *_ = resolved[composition]
    original_hash = resolve({"$ref": REF_PREFIX + "hash32"}, components)
    for original in (original_quantity, original_hash):
        if set(original) != {"pattern", "type"} or original["type"] != "string":
            raise ValueError(f"Unexpected upstream quantity or hash schema: {component}")
    if tag != {"type": "string", "enum": ["earliest", "finalized", "safe", "latest", "pending"]}:
        raise ValueError(f"Unexpected upstream block tags: {component}")
    quantity = original_quantity | {
        "pattern": "^0x0{0,63}(0|[1-9a-fA-F][0-9a-fA-F]{0,14}|[1-7][0-9a-fA-F]{15})$"
    }
    bare_hash = original_hash | {"pattern": "^0x[0-9a-fA-F]{64}$"}
    if composition == "oneOf":
        return {"anyOf": [{"oneOf": [quantity, tag]}, bare_hash], "maxLength": 66}
    return {"anyOf": [quantity, tag, bare_hash], "maxLength": 66}


def tuple_schema(method, components, use_overlay):
    if method.get("paramStructure", "by-position") not in {"by-position", "either"}:
        raise ValueError(f"OpenRPC method has no positional form: {method['name']}")
    params = method["params"]
    required = [parameter["required"] for parameter in params]
    if any(type(value) is not bool for value in required):
        raise ValueError(f"Non-boolean OpenRPC required flag in {method['name']}")
    minimum = sum(required)
    if required != [True] * minimum + [False] * (len(params) - minimum):
        raise ValueError(f"Non-trailing optional parameters in {method['name']}")
    overlay = {}
    if use_overlay:
        fields = OVERLAY_FIELDS[method["name"]]
        if [(param["name"], param["required"]) for param in params] != [
            (name, is_required) for name, is_required, _ in fields
        ]:
            raise ValueError(f"Unexpected upstream positional parameters in {method['name']}")
        overlay = {name: ref for name, _, ref in fields}
    result = {"type": "array", "minItems": minimum, "maxItems": len(params)}
    if params:
        items = []
        for parameter in params:
            source = parameter["schema"]
            resolved = resolve(source, components)
            if use_overlay:
                component = overlay[parameter["name"]]
                if component is not None:
                    resolved = overlay_parameter(component, source, resolved, components)
                elif resolved != {"type": "boolean"}:
                    raise ValueError(f"Expected boolean parameter in {method['name']}")
            items.append(resolved)
        result.update(items=items, additionalItems=False)
    return result


def generated_keywords(schema):
    keywords = set(schema)
    if unsupported := keywords - GENERATED_KEYWORDS:
        raise ValueError(f"Unsupported runtime schema keywords: {sorted(unsupported)}")
    for key in keywords & (COMBINATORS | {"items"}):
        for child in schema[key]:
            keywords |= generated_keywords(child)
    return keywords


def registered_methods():
    mapping = (RPC / "web3jsonrpc/endpoints/EndpointsMapping.cpp").read_text()
    # Reconcile assignment-style registrations against the explicit coverage list.
    # Fail on an unfamiliar write rather than silently dropping it from coverage.
    assignments = re.findall(r"m_handlers\s*\[(.*?)\]\s*=", mapping, re.DOTALL)
    if re.search(r"m_handlers\s*\.\s*(insert|emplace|try_emplace|insert_or_assign)\s*\(", mapping):
        raise ValueError("Unrecognized endpoint registration in EndpointsMapping.cpp")
    registered = []
    for assignment in assignments:
        match = re.fullmatch(r"\s*methodString\s*\(\s*EthMethod::(\w+)\s*\)\s*", assignment)
        if not match:
            raise ValueError(f"Unrecognized endpoint registration: {assignment}")
        registered.append(match[1])

    dispatcher = (RPC / "web3jsonrpc/Web3JsonRpcImpl.cpp").read_text()
    subscribe = (RPC / "web3jsonrpc/Web3Subscribe.h").read_text()
    subscriptions = dict(re.findall(r'\b(\w+)\s*=\s*"(\w+)"\s*;', subscribe))
    for name in re.findall(r"_method\s*==\s*Web3Subscribe::(\w+)", dispatcher):
        if name not in subscriptions:
            raise ValueError(f"Unrecognized subscription registration: {name}")
        registered.append(subscriptions[name])
    if len(registered) != len(set(registered)):
        raise ValueError("Duplicate Web3 RPC registration")
    return set(registered)


def coverage():
    groups = json.loads((DIRECTORY / "coverage.json").read_text())
    methods = {}
    for status, reasons in groups.items():
        if status not in {"upstream", "local overlay", "deferred", "out of scope"}:
            raise ValueError(f"Unrecognized coverage status: {status}")
        for reason, names in reasons.items():
            for name in names:
                if name in methods:
                    raise ValueError(f"Duplicate coverage entry: {name}")
                methods[name] = {"status": status, "reason": reason}
    registered = registered_methods()
    if registered != methods.keys():
        raise ValueError(
            f"Web3 RPC coverage mismatch, missing: {sorted(registered - methods.keys())}, "
            f"unregistered: {sorted(methods.keys() - registered)}"
        )
    return methods


def generate():
    pin, methods, components = read_upstream()
    entries = coverage()
    schemas = {}
    for name, entry in entries.items():
        status = entry["status"]
        if status in {"upstream", "local overlay"}:
            if name not in methods:
                raise ValueError(f"Missing Web3 RPC schema in pinned OpenRPC source: {name}")
            schema = tuple_schema(methods[name], components, status == "local overlay")
            entry["keywords"] = sorted(generated_keywords(schema))
            schemas[name] = schema

    schema_json = json_text(schemas)
    header = (
        "// SPDX-License-Identifier: Apache-2.0\n"
        "// Generated by tools/web3-rpc-schema/generate.py.\n"
        "// Derived from Ethereum execution-apis (CC0-1.0).\n"
        "#pragma once\n\n#include <string_view>\n\n"
        "namespace bcos::rpc::schema\n{\n"
        'inline constexpr std::string_view parameterSchemas = R"schema('
        + schema_json
        + ')schema";\n}  // namespace bcos::rpc::schema\n'
    )
    manifest = {
        "upstream": pin,
        "schema_sha256": sha256(schema_json.encode()),
        "generator_sha256": sha256(Path(__file__).read_bytes()),
        "methods": entries,
    }
    return {HEADER: header, MANIFEST: json_text(manifest)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated files without changing them")
    args = parser.parse_args()
    try:
        for path, content in generate().items():
            if args.check:
                if not path.exists() or path.read_bytes() != content.encode():
                    raise ValueError(f"Regenerate {path.relative_to(ROOT)} with {Path(__file__).relative_to(ROOT)}")
            else:
                path.write_bytes(content.encode())
    except (ValueError, KeyError, TypeError, yaml.YAMLError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
