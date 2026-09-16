# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for gen_official_genesis.py. Pure logic uses synthetic fixtures;
the real-zip acceptance test skips when the op-geth zip/zstd are unavailable,
unless OP_REQUIRE_REGISTRY_ZIP=1, in which case it fails instead."""
import importlib.util
import json
import os
import re
import zipfile
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "gen_official_genesis", str(Path(__file__).parent / "gen_official_genesis.py"))
gen = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(gen)

TOML = """
name = "Base"
chain_id = 8453
batch_inbox_addr = "0xFF00000000000000000000000000000000000010"
block_time = 2
seq_window_size = 3600
max_sequencer_drift = 600
[hardforks]
canyon_time = 1704992401
delta_time = 1708560000
ecotone_time = 1710374401
[optimism]
eip1559_elasticity = 6
eip1559_denominator = 50
eip1559_denominator_canyon = 250
[genesis]
l2_time = 1686789347
[genesis.l1]
hash = "0x1111111111111111111111111111111111111111111111111111111111111111"
number = 17481768
[genesis.l2]
hash = "0xd043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"
number = 0
[genesis.system_config]
batcherAddress = "0x5050F69a9786F081509234F1a7F4684b5E5b76C9"
overhead = "0x00000000000000000000000000000000000000000000000000000000000000bc"
scalar = "0x00000000000000000000000000000000000000000000000000000000000a6fe0"
gasLimit = 30000000
[addresses]
OptimismPortalProxy = "0xbEb5Fc579115071764c7423A4f12eDde41f106Ed"
SystemConfigProxy = "0x229047fed2591dbec1eF1118d64F7aF3dB9EB290"
"""

# Field-for-field the Task 1 London 16-field vector (ts 0x648a5ce3, non-empty
# extraData); its keccak256(rlp) equals the l2 hash in TOML above, so the synthetic
# end-to-end run necessarily satisfies self-verification.
GENESIS = {
    "number": "0x0", "timestamp": "0x648a5ce3", "gasLimit": "0x1c9c380",
    "gasUsed": "0x0", "difficulty": "0x0", "baseFeePerGas": "0x3b9aca00",
    "coinbase": "0x4200000000000000000000000000000000000011",
    "parentHash": "0x" + "00" * 32, "mixHash": "0x" + "00" * 32,
    "nonce": "0x0000000000000000",
    "extraData": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
    "alloc": {},
}


# L2ToL1MessagePasser predeploy and two non-zero storage slots (the ones op-reth's
# crates/chainspec/src/lib.rs test_storage_root_consistency hashes); op-geth's
# params/protocol_params.go:31 pins the same address.
_L2_TO_L1_MESSAGE_PASSER = "0x4200000000000000000000000000000000000016"
_MP_STORAGE = {
    "0x360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbc":
        "0x000000000000000000000000c0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d30016",
    "0xb53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103":
        "0x0000000000000000000000004200000000000000000000000000000000000018",
}
_MP_STORAGE_ROOT = "8ed4baae3a927be3dea54996b4d5899f8c01e7594bf50b17dc1e741388ce3d12"
# Golden keccak256(rlp(header)) for the synthetic Isthmus-at-genesis below, computed
# offline with the independent trie/RLP reference (withdrawals_root = _MP_STORAGE_ROOT).
_ISTHMUS_GOLDEN_HASH = "7382feec4a5be4d78c3dff2cd9768fdeb6f2acd4876f598e5d5eeb24d2c425bc"

# A synthetic registry chain with Isthmus (and every prior EL fork) at genesis: the
# case where the withdrawals root must be the MessagePasser storage root.
TOML_ISTHMUS = (TOML
                .replace("canyon_time = 1704992401", "canyon_time = 0")
                .replace("delta_time = 1708560000", "delta_time = 0")
                .replace("ecotone_time = 1710374401",
                         "ecotone_time = 0\nfjord_time = 0\ngranite_time = 0\n"
                         "holocene_time = 0\nisthmus_time = 0")
                .replace("d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9",
                         _ISTHMUS_GOLDEN_HASH))
GENESIS_ISTHMUS = {**GENESIS,
                   "alloc": {_L2_TO_L1_MESSAGE_PASSER:
                             {"balance": "0x0", "nonce": "0x0", "storage": _MP_STORAGE}}}


def _make_zip(tmp_path, toml=TOML, genesis=GENESIS):
    path = tmp_path / "superchain-configs.zip"
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("COMMIT", "deadbeef")
        zf.writestr("dictionary", b"")  # unused by the identity decompressor, still read
        zf.writestr("configs/mainnet/base.toml", toml)
        zf.writestr("genesis/mainnet/base.json.zst", json.dumps(genesis))
    return str(path)


def test_load_registry_chain_reads_toml_and_json(tmp_path):
    data = gen.load_registry_chain(_make_zip(tmp_path), "mainnet/base",
                                   decompress=lambda raw, dictionary: raw)
    assert data["commit"] == "deadbeef"
    assert data["toml"]["chain_id"] == 8453
    assert data["toml"]["hardforks"]["canyon_time"] == 1704992401
    assert data["genesis"]["timestamp"] == "0x648a5ce3"


def test_header_field_set_by_genesis_time():
    forks = {"canyon": 100, "ecotone": 200, "isthmus": 400}
    assert gen.header_field_set(0, forks) == gen.LONDON_FIELDS
    assert "withdrawals_root" in gen.header_field_set(100, forks)
    assert "blob_gas_used" in gen.header_field_set(200, forks)
    assert "requests_hash" in gen.header_field_set(400, forks)
    assert "requests_hash" not in gen.header_field_set(399, forks)
    forks7 = {"canyon": 100, "ecotone": 200, "isthmus": 400, "jovian": 500, "karst": 600}
    assert gen.header_field_set(500, forks7) == gen.header_field_set(400, forks7)
    assert gen.header_field_set(600, forks7) == gen.header_field_set(400, forks7)
    for ts in (0, 100, 200, 400, 500, 600):
        assert "slot_number" not in gen.header_field_set(ts, forks7)


def test_eip1559_section_comes_from_the_registry_optimism_table():
    """The FISCO ini must declare the same triple rollup.json calls chain_op_config, from ONE
    source (the registry toml's [optimism]) — if the EL and the CL read different numbers,
    every pre-Canyon block prices differently."""
    toml = gen.tomllib.loads(TOML)
    assert gen.build_eip1559_section(toml) == (
        "[op_eip1559]\nelasticity=6\ndenominator=50\ndenominator_canyon=250\n")


def test_build_and_selfcheck_london_header():
    genesis = {
        "number": "0x0", "timestamp": "0x648a5ce3", "gasLimit": "0x1c9c380",
        "gasUsed": "0x0", "difficulty": "0x0", "baseFeePerGas": "0x3b9aca00",
        "mixHash": "0x" + "00" * 32, "nonce": "0x0000000000000000",
        "coinbase": "0x4200000000000000000000000000000000000011",
        "extraData": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
        "parentHash": "0x" + "00" * 32,
    }
    present = gen.header_field_set(0x648a5ce3, {})
    fields = gen.build_header_fields(genesis, 0x648a5ce3, gen.EMPTY_TRIE_ROOT, present)
    digest = gen.keccak256(gen.encode_header_fields(fields, present)).hex()
    assert digest == "d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"


def test_missing_gas_limit_and_base_fee_defaults_match_op_geth():
    # op-geth Genesis.ToBlock substitutes params.GenesisGasLimit=4712388 when the
    # genesis gasLimit is 0 and params.InitialBaseFee=1000000000 when baseFeePerGas
    # is absent (London-at-block-0 chains): core/genesis.go:658-673, with the
    # constants at params/protocol_params.go:40 and :146. The generator must mirror
    # that substitution, not fall back to 0x0.
    genesis = {"timestamp": "0x0"}  # neither gasLimit nor baseFeePerGas
    fields = gen.build_header_fields(genesis, 0, gen.EMPTY_TRIE_ROOT, gen.LONDON_FIELDS)
    assert fields["gas_limit"] == "0x47e7c4"           # 4712388
    assert fields["base_fee_per_gas"] == "0x3b9aca00"  # 1000000000


def test_compute_state_root_matches_reference_on_adapted_input():
    alloc = {"0x" + "11" * 20: {"balance": "0x1", "nonce": "0x0"}}
    # The adapter must be a pure re-shape: its result equals feeding the reference
    # implementation the equivalent tuple.
    reference = gen._trieroot.state_root(
        [(bytes.fromhex("11" * 20), 0, 1, b"", {})])
    assert gen.compute_state_root(alloc) == reference


def test_compute_state_root_golden_with_code_and_storage():
    # Independent golden (produced offline by gen_trieroot_golden.state_root):
    # pins the tuple shape, the 32-byte storage word padding and the code hash.
    alloc = {"0x" + "00" * 19 + "01": {"balance": "0x1", "nonce": "0x0",
                                       "code": "0x6001", "storage": {"0x00": "0x02"}}}
    assert gen.compute_state_root(alloc).hex() == (
        "00aa0d47b052f7d85b6d74f013475f9fcaa8fef638ba9072ef750bb9f17fbe4e")


def test_message_passer_storage_root_matches_op_reth_vector():
    # Independent oracle: op-reth crates/chainspec/src/lib.rs (test_storage_root_consistency)
    # hashes these same two non-zero MessagePasser slots to _MP_STORAGE_ROOT.
    alloc = {_L2_TO_L1_MESSAGE_PASSER:
             {"balance": "0x0", "nonce": "0x0", "storage": _MP_STORAGE}}
    assert gen.message_passer_storage_root(alloc).hex() == _MP_STORAGE_ROOT


def test_isthmus_without_message_passer_is_an_error():
    # A spec-conformant Isthmus-at-genesis chain always has the predeploy; failing
    # loud beats emitting a header the registry hash can never match.
    with pytest.raises(gen.RegistryError):
        gen.message_passer_storage_root({})


def test_isthmus_genesis_uses_message_passer_storage_root(tmp_path):
    # Isthmus at genesis: withdrawals_root is the L2ToL1MessagePasser storage root
    # (op-geth core/genesis.go:711-719; specs/protocol/isthmus/exec-engine.md
    # §Genesis Block), not the empty withdrawals trie.
    result = gen.generate(_make_zip(tmp_path, TOML_ISTHMUS, GENESIS_ISTHMUS),
                          "mainnet/base", l1_chain_id=1,
                          decompress=lambda raw, dictionary: raw)
    assert result["manifest"]["header_hash"] == _ISTHMUS_GOLDEN_HASH
    assert "withdrawals_root=0x" + _MP_STORAGE_ROOT in result["genesis_ini"]
    assert "requests_hash=" in result["genesis_ini"]
    # all EL forks are active at genesis, so the baseline is the highest one
    assert result["manifest"]["schedule"] == "0:isthmus"


def test_to_ini_allocs_preserves_code_and_storage():
    alloc = {"0x" + "42" * 20: {"balance": "0xa", "nonce": "0x1",
                                "code": "0x6001", "storage": {"0x00": "0x02"}}}
    out = gen.to_ini_allocs(alloc)
    assert out[0]["address"] == "42" * 20
    assert out[0]["balance"] == 10 and out[0]["nonce"] == 1
    assert gen._build_allocs.emit_ini(out) == (
        "[alloc.0]\naddress=0x" + "42" * 20 + "\nbalance=10\nnonce=1\ncode=0x6001\n"
        "[alloc.0.storage]\n0x" + "00" * 32 + "=0x" + "00" * 31 + "02\n")


# Cross-language pin (S1-F2). The generator's EL_FORKS and the C++ loader's
# c_opForkNames are the same consensus input written twice, in two languages; the C++
# side only static_asserts its own count against the OpFork enum, so nothing in CI
# compares the two lists. This test is that comparison.
_CXX_OP_FORK_CODEC = (Path(__file__).resolve().parents[2] /
                      "bcos-framework/bcos-framework/ledger/OpForkScheduleCodec.h")


def test_el_forks_pinned_to_cpp_loader_order_and_membership():
    """EL_FORKS must equal bcos::ledger::detail::c_opForkNames in order and membership.

    Both sides are pinned alone elsewhere; only this case proves the seam, so a
    reorder or an added/removed name in either list must fail here. The C++ home is
    bcos-framework/bcos-framework/ledger/OpForkScheduleCodec.h (c_opForkNames).
    """
    text = _CXX_OP_FORK_CODEC.read_text()
    match = re.search(
        r"c_opForkNames\s*=\s*std::to_array<std::string_view>\(\{(.*?)\}\)",
        text, re.S)
    assert match, f"c_opForkNames array not found in {_CXX_OP_FORK_CODEC}"
    cpp_names = re.findall(r'"([^"]+)"', match.group(1))
    assert cpp_names == gen.EL_FORKS, (
        "EL_FORKS drifted from c_opForkNames\n"
        f"  C++ ({_CXX_OP_FORK_CODEC}): {cpp_names}\n"
        f"  Python (EL_FORKS):          {gen.EL_FORKS}")


TOML_FORKS = {"hardforks": {"canyon_time": 100, "delta_time": 150,
                            "ecotone_time": 200, "fjord_time": 300,
                            "granite_time": 400, "holocene_time": 500,
                            "isthmus_time": 600, "jovian_time": 700}}


def test_schedule_skips_delta_and_ends_at_jovian():
    assert gen.build_schedule(TOML_FORKS, ts0=50) == (
        "0:regolith,100:canyon,200:ecotone,300:fjord,400:granite,"
        "500:holocene,600:isthmus,700:jovian")


def test_schedule_overlay_adds_karst():
    out = gen.build_schedule(TOML_FORKS, ts0=50, extra_forks={"karst": 800})
    assert out.endswith(",800:karst")


def test_schedule_gap_raises():
    # skipping ecotone (canyon then fjord) is not contiguous
    broken = {"hardforks": {"canyon_time": 100, "fjord_time": 300}}
    with pytest.raises(gen.RegistryError):
        gen.build_schedule(broken, ts0=50)


def test_schedule_unknown_extra_fork_raises():
    with pytest.raises(gen.RegistryError):
        gen.build_schedule(TOML_FORKS, ts0=50, extra_forks={"delta": 999})


def test_schedule_overlay_conflict_raises():
    # an overlay naming a pinned fork with a different time must not silently win
    with pytest.raises(gen.RegistryError):
        gen.build_schedule(TOML_FORKS, ts0=50, extra_forks={"canyon": 123})


TOML_ALT_DA = TOML + """
[alt_da]
da_challenge_contract_address = "0x97A2dA87d3439b172e6DD027220e01c9Cb565B80"
da_challenge_window = 3600
da_resolve_window = 3600
da_commitment_type = "KeccakCommitment"
"""


def test_build_rollup_emits_alt_da_when_toml_carries_it():
    # op-node superchain.go maps chConfig.AltDA into Config.AltDAConfig, whose JSON
    # tag is `alt_da` (rollup/types.go). Dropping it silently degrades an alt-DA
    # chain to calldata DA, so the key must be emitted in the reader's shape.
    rollup = gen.build_rollup(gen.tomllib.loads(TOML_ALT_DA), l1_chain_id=1)
    assert rollup["alt_da"] == {
        "da_challenge_contract_address": "0x97a2da87d3439b172e6dd027220e01c9cb565b80",
        "da_commitment_type": "KeccakCommitment",
        "da_challenge_window": 3600,
        "da_resolve_window": 3600,
    }
    # No [alt_da] in the toml => key omitted (json:"alt_da,omitempty").
    assert "alt_da" not in gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1)


def test_build_rollup_carries_registry_fields():
    rollup = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1)
    assert rollup["genesis"]["l2"]["number"] == 0
    assert rollup["genesis"]["l2_time"] == 1686789347
    assert rollup["l2_chain_id"] == 8453
    assert rollup["l1_chain_id"] == 1
    assert rollup["block_time"] == 2
    assert rollup["batch_inbox_address"] == "0xff00000000000000000000000000000000000010"
    assert rollup["deposit_contract_address"] == "0xbeb5fc579115071764c7423a4f12edde41f106ed"
    assert rollup["chain_op_config"]["eip1559DenominatorCanyon"] == 250
    assert rollup["regolith_time"] == 0
    assert rollup["canyon_time"] == 1704992401
    # Unscheduled forks are OMITTED, not null: the pinned op-node parses rollup.json
    # with DisallowUnknownFields and has no KarstTime field, so a karst_time key — even
    # null — would make every artifact undecodable. A fork key is emitted only when
    # scheduled (see test_build_rollup_threads_extra_fork_overlay); interop/pectra
    # nulls are tolerated upstream but dropped under the same single rule.
    assert "karst_time" not in rollup
    assert "interop_time" not in rollup


def test_generate_end_to_end_synthetic(tmp_path):
    result = gen.generate(_make_zip(tmp_path), "mainnet/base", l1_chain_id=1,
                          decompress=lambda raw, dictionary: raw)
    assert set(result) == {"genesis_ini", "rollup", "manifest"}
    assert "[eth_genesis_header]" in result["genesis_ini"]
    assert "hash=0x" in result["genesis_ini"]  # the 22nd field NodeConfig requires
    assert "[alloc.0]" not in result["genesis_ini"]  # empty alloc
    assert "canonical=0:regolith,1704992401:canyon" in result["genesis_ini"]
    assert result["manifest"]["chain"] == "mainnet/base"


def test_generate_hash_mismatch_raises(tmp_path):
    # Break the toml's expected l2 hash: generation must fail loud, not emit artifacts.
    import zipfile
    src = _make_zip(tmp_path)
    good = "d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"
    with zipfile.ZipFile(src) as zf:
        toml = zf.read("configs/mainnet/base.toml").decode().replace(good, "99" * 32)
    broken = tmp_path / "broken.zip"
    with zipfile.ZipFile(broken, "w") as zf:
        zf.writestr("COMMIT", "deadbeef")
        zf.writestr("dictionary", b"")
        zf.writestr("configs/mainnet/base.toml", toml)
        zf.writestr("genesis/mainnet/base.json.zst", json.dumps(GENESIS))
    with pytest.raises(gen.RegistryError):
        gen.generate(str(broken), "mainnet/base", l1_chain_id=1,
                     decompress=lambda raw, dictionary: raw)


# Registry zip: a machine default kept for the local ritual, overridable for CI
# (Task 5's nightly will point OP_GETH_ZIP at the op-geth pin tree). When
# OP_REQUIRE_REGISTRY_ZIP=1 a missing zip is a FAILURE, not a skip — a silent
# skip here is how M5 degraded to "green but vacuous" (WI-11).
_DEFAULT_OP_GETH_ZIP = Path("/Users/octopus/octo/code/op-geth/superchain/superchain-configs.zip")
_OP_GETH_ZIP = Path(os.environ.get("OP_GETH_ZIP") or _DEFAULT_OP_GETH_ZIP)

# Chains the generator deliberately does not reproduce, with the reason. Not a
# silent skip: the sweep asserts the excluded set is exactly this set, so an
# unexpected pass or a new failure both surface.
#
# mainnet/op: op-geth special-cases chain 10 (params/superchain.go:78-84 sets
# LondonBlock = 105235063) and overwrites the expected genesis hash with a
# hardcoded value when chConfig.Genesis.L2.Number != genesisBlock.NumberU64()
# (core/superchain.go:66-73). FISCO reproduces neither the toml l2.hash nor that
# override, so the chain is out of scope per design §2/§9/§11 (chains with
# number != 0 are excluded).
_EXPECTED_REGISTRY_EXCLUSIONS = {"mainnet/op"}

# Registry chains carrying [alt_da]: op-node maps these into Config.AltDAConfig.
_ALT_DA_CHAINS = ["mainnet/redstone", "sepolia/celo-sep"]


def _registry_chains(zip_path):
    with zipfile.ZipFile(zip_path) as zf:
        return sorted(name[len("genesis/"):-len(".json.zst")]
                      for name in zf.namelist()
                      if name.startswith("genesis/") and name.endswith(".json.zst"))


def test_real_registry_full_sweep_matches_documented_exclusions():
    """End-to-end acceptance over the whole zip.

    Every genesis entry except the documented exclusions must reconstruct its
    registry genesis hash and, since U7-F1, pass rollup validation. Offline,
    deterministic, ~10s. Final assertion: passed == all - excluded.
    """
    import shutil
    if not _OP_GETH_ZIP.exists() or shutil.which("zstd") is None:
        if os.environ.get("OP_REQUIRE_REGISTRY_ZIP", "").strip().lower() in ("1", "true", "yes"):
            pytest.fail(f"OP_REQUIRE_REGISTRY_ZIP=1 but registry zip/zstd unavailable "
                        f"(zip={_OP_GETH_ZIP})")
        pytest.skip("op-geth superchain zip / zstd CLI not available")
    chains = _registry_chains(str(_OP_GETH_ZIP))
    assert chains  # empty sweep is a failure, not a pass
    passed, failed = [], {}
    for chain in chains:
        try:
            result = gen.generate(str(_OP_GETH_ZIP), chain)
            assert result["manifest"]["header_hash"] == result["manifest"]["expected_l2_hash"]
        except Exception as exc:  # noqa: BLE001 - report every chain, not just the first
            failed[chain] = exc
        else:
            passed.append(chain)
    assert set(failed) == _EXPECTED_REGISTRY_EXCLUSIONS, (
        f"registry sweep has undocumented failures: "
        f"{sorted(set(failed) - _EXPECTED_REGISTRY_EXCLUSIONS)}; "
        f"documented exclusions that unexpectedly passed: "
        f"{sorted(_EXPECTED_REGISTRY_EXCLUSIONS - set(failed))}")
    assert set(passed) == set(chains) - _EXPECTED_REGISTRY_EXCLUSIONS


@pytest.mark.parametrize("chain", _ALT_DA_CHAINS)
def test_real_registry_alt_da_chains_emit_alt_da(chain):
    import shutil
    if not _OP_GETH_ZIP.exists() or shutil.which("zstd") is None:
        if os.environ.get("OP_REQUIRE_REGISTRY_ZIP", "").strip().lower() in ("1", "true", "yes"):
            pytest.fail(f"OP_REQUIRE_REGISTRY_ZIP=1 but registry zip/zstd unavailable "
                        f"(zip={_OP_GETH_ZIP})")
        pytest.skip("op-geth superchain zip / zstd CLI not available")
    result = gen.generate(str(_OP_GETH_ZIP), chain)
    alt_da = result["rollup"]["alt_da"]
    assert alt_da["da_commitment_type"]
    assert alt_da["da_challenge_window"] > 0


def test_build_rollup_threads_extra_fork_overlay():
    # The CL config must agree with the EL schedule about a fork the pin lacks:
    # otherwise op-node would treat karst as unscheduled while the EL activates it.
    rollup = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1,
                              extra_forks={"karst": 1781712001})
    assert rollup["karst_time"] == 1781712001
    # Everything else still comes from the pin: the overlay adds karst without
    # inventing the forks this synthetic toml does not schedule (and unscheduled
    # forks stay omitted — their absence is what keeps the artifact decodable).
    assert rollup["canyon_time"] == 1704992401
    assert "jovian_time" not in rollup
    # an overlay naming an EL fork the pin already schedules must not be applied silently
    with pytest.raises(gen.RegistryError):
        gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1,
                         extra_forks={"canyon": 123})


def _make_undecodable_zip(tmp_path, name="undecodable.zip"):
    """A zip whose dictionary does not match its frame: the real decompressor fails."""
    path = tmp_path / name
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("COMMIT", "deadbeef")
        zf.writestr("dictionary", b"not-a-dictionary")
        zf.writestr("configs/mainnet/base.toml", TOML)
        zf.writestr("genesis/mainnet/base.json.zst", b"\x28\xb5\x2f\xfd not a zstd frame")
    return str(path)


def test_undecodable_registry_frame_is_a_registry_error(tmp_path):
    # The real default_decompress runs here (no injection): design §7 wants a named
    # error, not a RuntimeError escaping to the operator as a traceback.
    import shutil
    # Exempt from OP_REQUIRE_REGISTRY_ZIP: this case builds its own synthetic zip
    # and only needs the zstd CLI (the registry zip is never read here).
    if shutil.which("zstd") is None:
        pytest.skip("zstd CLI not available")
    with pytest.raises(gen.RegistryError):
        gen.generate(_make_undecodable_zip(tmp_path), "mainnet/base")


def test_cli_reports_undecodable_frame_without_traceback(tmp_path, capsys):
    import shutil
    # Exempt from OP_REQUIRE_REGISTRY_ZIP: this case builds its own synthetic zip
    # and only needs the zstd CLI (the registry zip is never read here).
    if shutil.which("zstd") is None:
        pytest.skip("zstd CLI not available")
    rc = gen.main(["--zip", _make_undecodable_zip(tmp_path), "--chain", "mainnet/base",
                   "--out-dir", str(tmp_path / "out")])
    assert rc == 1
    assert "error:" in capsys.readouterr().err
    assert not (tmp_path / "out").exists()  # no half-written artifacts


def test_check_registry_rollup_validates_fields_and_fork_order():
    rollup = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1)
    gen.check_registry_rollup(rollup)  # the registry-derived config must pass

    regressed = dict(rollup)
    regressed["ecotone_time"] = 1  # before canyon/regolith
    with pytest.raises(gen.RegistryError):
        gen.check_registry_rollup(regressed)

    missing = dict(rollup)
    del missing["l1_chain_id"]
    with pytest.raises(gen.RegistryError):
        gen.check_registry_rollup(missing)

    # A fork the registry does not schedule (null) is skipped, not compared.
    with_overlay = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1,
                                    extra_forks={"karst": 1781712001})
    gen.check_registry_rollup(with_overlay)


def test_check_registry_rollup_ignores_external_fork_order():
    """Only the canonical regolith->...->isthmus chain is ordered by op-node.

    op-node Config.Check() (rollup/types.go) calls checkFork on the seven adjacent
    canonical pairs only; superchain.go applyHardforks assigns
    pectra_blob_schedule/interop/jovian/karst but never compares them. A registry
    chain may therefore put pectra before holocene (sepolia/race) or interop before
    jovian without op-node rejecting it, and the generator must accept that too.
    """
    rollup = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1)
    # sepolia/race shape: pectra_blob_schedule (CL-only) before holocene.
    rollup["holocene_time"] = 1749772800
    rollup["pectra_blob_schedule_time"] = 1742486400
    gen.check_registry_rollup(rollup)  # must not raise
    # interop/jovian are not ordered by op-node either.
    rollup["jovian_time"] = 1000
    rollup["interop_time"] = 500
    gen.check_registry_rollup(rollup)  # must not raise


def test_check_registry_rollup_still_rejects_canonical_regression():
    # Guard against "fix U7-F1 by checking nothing": a real regression on the
    # canonical chain must still be rejected.
    rollup = gen.build_rollup(gen.tomllib.loads(TOML), l1_chain_id=1)
    rollup["delta_time"] = 1  # delta before canyon
    with pytest.raises(gen.RegistryError, match="fork time regresses"):
        gen.check_registry_rollup(rollup)


def _assert_registry_zip_is_pre_karst(zip_path):
    """M5's registry basis is the pre-Karst pin (zip COMMIT 9cf0456a).

    If this guard goes red the corpus has moved past Karst: the M5 basis, the
    minimal read set and the documented 60/59/1 split must be re-decided in the
    SAME commit that swaps the zip — never silently.
    """
    scanned = 0
    with zipfile.ZipFile(zip_path) as zf:
        for name in zf.namelist():
            if name.startswith("configs/") and name.endswith(".toml"):
                assert b"karst_time" not in zf.read(name), (
                    f"{name} carries karst_time — the registry is post-Karst; "
                    "re-baseline M5 (basis + minimal read set) in the same commit")
                scanned += 1
    assert scanned > 0, "scanned no configs/*.toml — the guard is green but empty"


def test_real_registry_zip_is_pre_karst_basis():
    if not _OP_GETH_ZIP.exists():
        if os.environ.get("OP_REQUIRE_REGISTRY_ZIP", "").strip().lower() in ("1", "true", "yes"):
            pytest.fail(
                f"OP_REQUIRE_REGISTRY_ZIP=1 but registry zip unavailable (zip={_OP_GETH_ZIP})"
            )
        pytest.skip("op-geth superchain zip not available")
    _assert_registry_zip_is_pre_karst(_OP_GETH_ZIP)


def test_pre_karst_guard_goes_red_on_a_synthetic_karst_entry(tmp_path):
    # Permanent mutation evidence: the guard above must be able to fail.
    zip_path = tmp_path / "post-karst.zip"
    with zipfile.ZipFile(zip_path, "w") as zf:
        zf.writestr("configs/mainnet/base.toml", "karst_time = 1\n")
    with pytest.raises(AssertionError, match="re-baseline M5"):
        _assert_registry_zip_is_pre_karst(zip_path)
