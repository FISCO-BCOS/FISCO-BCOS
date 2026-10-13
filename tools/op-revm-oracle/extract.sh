#!/usr/bin/env bash
# Copyright (c) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Regenerate bcos-evm/test/opstack/op_revm_oracle.json from the pinned op-revm
# revision. The oracle exists so the Karst precompile table is judged by an EXTERNAL
# implementation, not by its own literals (OpPrecompilesTest used to compare
# constants to themselves).
#
# Every value is read from the PINNED GIT OBJECT (`git show "$OP_REVM_PIN:<path>"`),
# never from the working tree (review F25): a modified checkout at the right HEAD
# used to mint an oracle whose source.revision claimed the pin while its numbers
# came from edited files.
set -euo pipefail

OP_REVM_REPO="${OP_REVM_REPO:-}"
[ -n "$OP_REVM_REPO" ] || {
  echo "OP_REVM_REPO is required (path to the pinned optimism checkout; no machine default)" >&2
  exit 1
}
OP_REVM_SUBDIR="${OP_REVM_SUBDIR:-rust/op-revm}"
# The revision the CI pin gate (tools/.ci/check-ci-pins.sh Axis 2) enforces for this
# oracle; pass a different OP_REVM_PIN only when deliberately moving the pin.
OP_REVM_PIN="${OP_REVM_PIN:-5f90f749caea14398554afb75062f7111b1fc554}"
REVISION="$(git -C "$OP_REVM_REPO" rev-parse HEAD)"
[ "$REVISION" = "$OP_REVM_PIN" ] || {
  echo "error: $OP_REVM_REPO is at $REVISION, not the pinned $OP_REVM_PIN —" >&2
  echo "       checkout the pin, or set OP_REVM_PIN to move it deliberately" >&2
  exit 1
}
OUT="${1:-$(cd "$(dirname "$0")/../.." && pwd -P)/bcos-evm/test/opstack/op_revm_oracle.json}"

PRE_REL="$OP_REVM_SUBDIR/src/precompiles.rs"
SPEC_REL="$OP_REVM_SUBDIR/src/spec.rs"
CONST_REL="$OP_REVM_SUBDIR/src/constants.rs"
LOCK_REL="rust/Cargo.lock"
for rel in "$PRE_REL" "$SPEC_REL" "$CONST_REL" "$LOCK_REL"; do
  git -C "$OP_REVM_REPO" cat-file -e "$OP_REVM_PIN:$rel" 2>/dev/null || {
    echo "error: $rel not found at $OP_REVM_PIN in $OP_REVM_REPO" >&2
    exit 1
  }
done

show() { # show <repo-relative path>: the pinned blob, never the worktree (review F25)
  git -C "$OP_REVM_REPO" show "$OP_REVM_PIN:$1"
}

grab() { # grab <const-name> <repo-relative path>; a missing anchor is a hard error, never a default
  local value
  value="$(show "$2" | grep -oE "$1: usize = [0-9_]+" | head -1 | grep -oE '[0-9_]+$' | tr -d '_' || true)"
  [ -n "$value" ] || {
    echo "error: $1 not found in $2 at $OP_REVM_PIN; cannot derive its value (no default)" >&2
    exit 1
  }
  printf '%s\n' "$value"
}

grab_u64() { # same shape, for the u64 constants in constants.rs
  local value
  value="$(show "$2" | grep -oE "$1: u64 = [0-9_]+" | head -1 | grep -oE '[0-9_]+$' | tr -d '_' || true)"
  [ -n "$value" ] || {
    echo "error: $1 not found in $2 at $OP_REVM_PIN; cannot derive its value (no default)" >&2
    exit 1
  }
  printf '%s\n' "$value"
}

KARST_BN254="$(grab KARST_MAX_INPUT_SIZE "$PRE_REL")"
JOV_G1="$(grab JOVIAN_G1_MSM_MAX_INPUT_SIZE "$PRE_REL")"
JOV_G2="$(grab JOVIAN_G2_MSM_MAX_INPUT_SIZE "$PRE_REL")"
JOV_PAIR="$(grab JOVIAN_PAIRING_MAX_INPUT_SIZE "$PRE_REL")"
KARST_ETH_SPEC="$(show "$SPEC_REL" | grep -oE 'Self::KARST \| Self::INTEROP => SpecId::[A-Z]+' | grep -oE '[A-Z]+$' || true)"
[ -n "$KARST_ETH_SPEC" ] || {
  echo "error: KARST/INTEROP spec mapping not found in $SPEC_REL at $OP_REVM_PIN;" >&2
  echo "       cannot derive karst_eth_spec (no default)" >&2
  exit 1
}
# Holocene / Jovian per-value anchors (WI-E5/E6): the ETH spec each fork runs with, plus
# the two numbers the operator-fee formula is built from (Isthmus'/1e6 decimal, Jovian x100).
HOLOCENE_ETH_SPEC="$(show "$SPEC_REL" | grep -oE 'Self::ECOTONE \| Self::FJORD \| Self::GRANITE \| Self::HOLOCENE => SpecId::[A-Z]+' | grep -oE '[A-Z]+$' || true)"
[ -n "$HOLOCENE_ETH_SPEC" ] || {
  echo "error: ECOTONE..HOLOCENE spec mapping not found in $SPEC_REL at $OP_REVM_PIN;" >&2
  echo "       cannot derive holocene_eth_spec (no default)" >&2
  exit 1
}
JOVIAN_ETH_SPEC="$(show "$SPEC_REL" | grep -oE 'Self::ISTHMUS \| Self::JOVIAN => SpecId::[A-Z]+' | grep -oE '[A-Z]+$' || true)"
[ -n "$JOVIAN_ETH_SPEC" ] || {
  echo "error: ISTHMUS/JOVIAN spec mapping not found in $SPEC_REL at $OP_REVM_PIN;" >&2
  echo "       cannot derive jovian_eth_spec (no default)" >&2
  exit 1
}
OPERATOR_FEE_DECIMAL="$(grab_u64 OPERATOR_FEE_SCALAR_DECIMAL "$CONST_REL")"
OPERATOR_FEE_JOVIAN_MULT="$(grab_u64 OPERATOR_FEE_JOVIAN_MULTIPLIER "$CONST_REL")"
# P256Verify gas for Karst: Karst activates Osaka, so the OSAKA constant is the one the
# spec runs with (revm-precompile/src/secp256r1.rs defines P256VERIFY_BASE_GAS_FEE=3450
# and P256VERIFY_BASE_GAS_FEE_OSAKA=6900; grabbing "the number next to P256VERIFY" instead
# matched P256VERIFY_ADDRESS=256, which is the precompile ADDRESS, not a gas).
REVM_PRECOMPILE_VER="$(show "$LOCK_REL" | grep -A1 '^name = "revm-precompile"' | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
[ -n "$REVM_PRECOMPILE_VER" ] || {
  echo "error: revm-precompile version not found in $LOCK_REL at $OP_REVM_PIN;" >&2
  echo "       cannot locate the p256 anchor (no default)" >&2
  exit 1
}
P256_SRC="$(ls -d "${CARGO_HOME:-$HOME/.cargo}"/registry/src/*/revm-precompile-"$REVM_PRECOMPILE_VER"/src 2>/dev/null | head -1 || true)"
P256_GAS=""
P256_SHA256=""
if [ -n "$P256_SRC" ]; then
  # `|| true` + the named errors below (review F26): under set -euo pipefail a grep
  # miss used to abort the script silently before its own diagnostics could run.
  P256_GAS="$(grep -hoE 'P256VERIFY_BASE_GAS_FEE_OSAKA: u64 = [0-9_]+' \
    "$P256_SRC/secp256r1.rs" | head -1 | grep -oE '[0-9_]+$' | tr -d '_' || true)"
  # Content hash of the exact file the constant was read from. The registry copy is
  # keyed by SEMVER only (crates.io versions are immutable in the normal case, but
  # admin-deleted republishes, mirror substitutions and local tampering are not
  # detectable without one) — with the hash recorded, a drift is at least visible.
  P256_SHA256="$(shasum -a 256 "$P256_SRC/secp256r1.rs" 2>/dev/null | awk '{print $1}' || true)"
  # Provenance fields never carry sentinels: an unreadable hash is a hard error, the
  # same rule as the anchors below — a consumer comparing this field must never
  # mistake a placeholder for a value.
  [ -n "$P256_SHA256" ] || {
    echo "error: cannot sha256 $P256_SRC/secp256r1.rs (shasum unavailable?);" >&2
    echo "       cannot record p256verify_source_sha256" >&2
    exit 1
  }
fi
# The tracked oracle is the external judge for the Karst precompile table, so a missing
# source must die, not silently pin p256verify_gas:0 (same rule as c2-e2e.sh's forge
# pin: an unfetchable anchor is a hard error, never a default).
[ -n "$P256_SRC" ] || {
  echo "error: revm-precompile-$REVM_PRECOMPILE_VER sources not found under" >&2
  echo "       \${CARGO_HOME:-$HOME/.cargo}/registry/src; cannot derive p256verify_gas" >&2
  exit 1
}
[ -n "$P256_GAS" ] || {
  echo "error: P256VERIFY_BASE_GAS_FEE_OSAKA not found in $P256_SRC/secp256r1.rs;" >&2
  echo "       cannot derive p256verify_gas" >&2
  exit 1
}

cat > "$OUT" <<EOF
{
  "_comment": "Generated by tools/op-revm-oracle/extract.sh at the pinned op-revm revision. This file is a REVISION PIN consumed by the CI pin gate (tools/.ci/check-ci-pins.sh reads source.revision) — no C++ test reads the values below today: they are an explicitly NON-JUDGED regeneration reference (review F16), and every cap carries the OP fork whose constant it is — the KARST_MAX_INPUT_SIZE vs JOVIAN_* distinction an earlier flat name blurred.",
  "source": {
    "crate": "op-revm",
    "repo_hint": "override with the OP_REVM_REPO env var when regenerating; the revision above is the authoritative pin",
    "subdir": "$OP_REVM_SUBDIR",
    "revision": "$REVISION",
    "precompiles_rs": "$OP_REVM_SUBDIR/src/precompiles.rs",
    "spec_rs": "$OP_REVM_SUBDIR/src/spec.rs",
    "constants_rs": "$OP_REVM_SUBDIR/src/constants.rs"
  },
  "karst_eth_spec": "$KARST_ETH_SPEC",
  "holocene_eth_spec": "$HOLOCENE_ETH_SPEC",
  "jovian_eth_spec": "$JOVIAN_ETH_SPEC",
  "operator_fee_scalar_decimal": ${OPERATOR_FEE_DECIMAL},
  "operator_fee_jovian_multiplier": ${OPERATOR_FEE_JOVIAN_MULT},
  "karst_bn254_pairing_max_input_size": $KARST_BN254,
  "jovian_bls_g1_msm_max_input_size": $JOV_G1,
  "jovian_bls_g2_msm_max_input_size": $JOV_G2,
  "jovian_bls_pairing_max_input_size": $JOV_PAIR,
  "p256verify_gas": ${P256_GAS},
  "p256verify_source_sha256": "$P256_SHA256",
  "revm_precompile_version": "$REVM_PRECOMPILE_VER"
}
EOF

echo "wrote $OUT"
cat "$OUT"
