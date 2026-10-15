#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# build-local.sh — collect the locally built binaries into the hive client
# build context and (optionally) install the client definition into a local
# hive checkout.
#
# Usage:
#   bash tools/hive/build-local.sh                 # just collect binaries
#   bash tools/hive/build-local.sh /path/to/hive   # also install into hive's clients/
#   BUILD_DIR=build-release bash tools/hive/build-local.sh /path/to/hive
#                                                  # collect from another build tree
#
# After installing, run from the hive checkout, e.g.:
#   ./hive --sim smoke --client bcos
#   ./hive --sim ethereum/rpc-compat --client bcos
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLIENT_DIR="${REPO_ROOT}/tools/hive/clients/bcos"

NODE_BIN="${REPO_ROOT}/${BUILD_DIR:-build}/fisco-bcos-air/fisco-bcos"
TOOL_BIN="${REPO_ROOT}/${BUILD_DIR:-build}/tools/eth-sync-check/eth-sync-check"

for bin in "${NODE_BIN}" "${TOOL_BIN}"; do
    [ -f "${bin}" ] || { echo "missing binary: ${bin} (build it first)" >&2; exit 1; }
done

mkdir -p "${CLIENT_DIR}/bin"
cp "${NODE_BIN}" "${CLIENT_DIR}/bin/fisco-bcos"
cp "${TOOL_BIN}" "${CLIENT_DIR}/bin/eth-sync-check"
# Debug/ASan builds carry gigabytes of debug info; strip the copies that go
# into the docker image (the originals in build/ are untouched).
strip "${CLIENT_DIR}/bin/fisco-bcos" "${CLIENT_DIR}/bin/eth-sync-check" || true
echo "binaries collected into ${CLIENT_DIR}/bin/"

# Record the version the image will report.
TAG=$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo "local")
echo "${TAG}" > "${CLIENT_DIR}/bin/.tag"

if [ $# -ge 1 ]; then
    HIVE_DIR="$1"
    [ -d "${HIVE_DIR}/clients" ] || { echo "not a hive checkout: ${HIVE_DIR}" >&2; exit 1; }
    rm -rf "${HIVE_DIR}/clients/bcos"
    cp -r "${CLIENT_DIR}" "${HIVE_DIR}/clients/bcos"
    echo "client installed to ${HIVE_DIR}/clients/bcos"
    echo "run: (cd ${HIVE_DIR} && ./hive --sim smoke --client bcos)"
fi
