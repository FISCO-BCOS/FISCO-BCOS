#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Fetch forge-std + pinned OpenZeppelin trees for bcos-l2-contracts.
# Mirrors .github/workflows/l2-contracts.yml so C2 e2e and local runs share
# the same dep-install contract as the storage-layout gate.
set -euo pipefail

REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
L2_DIR="${REPO_ROOT}/bcos-l2-contracts"
PIN="${L2_DIR}/op-fork-pin.toml"

[ -f "$PIN" ] || {
  echo "[fetch-l2-contract-deps] missing $PIN" >&2
  exit 1
}

cd "$L2_DIR"
mkdir -p lib

if [[ ! -d lib/forge-std/.git ]]; then
  git clone --depth 1 https://github.com/foundry-rs/forge-std lib/forge-std
fi

oz=$(grep -E '^"openzeppelin-contracts"' op-fork-pin.toml | awk -F'"' '{print $4}' | tr 'A-F' 'a-f')
ozu=$(grep -E '^"openzeppelin-contracts-upgradeable"' op-fork-pin.toml | awk -F'"' '{print $4}' | tr 'A-F' 'a-f')
for sha in "$oz" "$ozu"; do
  if ! [[ "$sha" =~ ^[0-9a-f]{40}$ ]]; then
    echo "::error::op-fork-pin.toml OZ dep SHA is not a 40-char hex SHA: '${sha}'" >&2
    exit 1
  fi
done

if [[ ! -d lib/openzeppelin-contracts/.git ]]; then
  git clone https://github.com/OpenZeppelin/openzeppelin-contracts lib/openzeppelin-contracts
fi
git -C lib/openzeppelin-contracts fetch --depth 1 origin "$oz" 2>/dev/null || true
git -C lib/openzeppelin-contracts checkout -q "$oz"

if [[ ! -d lib/openzeppelin-contracts-upgradeable/.git ]]; then
  git clone https://github.com/OpenZeppelin/openzeppelin-contracts-upgradeable \
    lib/openzeppelin-contracts-upgradeable
fi
git -C lib/openzeppelin-contracts-upgradeable fetch --depth 1 origin "$ozu" 2>/dev/null || true
git -C lib/openzeppelin-contracts-upgradeable checkout -q "$ozu"
