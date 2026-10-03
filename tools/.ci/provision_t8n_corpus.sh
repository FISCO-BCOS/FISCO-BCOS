#!/usr/bin/env bash
# Pin and symlink FISCO-BCOS/op-stack-e2e-tests so S6 reads
# opstack-executor/tests/t8n/{vectors,golden/engine}.
set -euo pipefail

PIN="${T8N_CORPUS_PIN:-b82691ac133a94637da9e6969a63f86279c6b0a2}"
ROOT="$(git rev-parse --show-toplevel)"
DEST="${ROOT}/opstack-executor/tests/t8n"
# The pin marker lives NEXT TO the symlink, never inside it: written through the
# symlink it would land in the external corpus git checkout and dirty it.
PIN_FILE="$(dirname "${DEST}")/.t8n-pin"

stored_pin() {
    if [[ -f "${PIN_FILE}" ]]; then
        tr -d '[:space:]' < "${PIN_FILE}"
    fi
}

if [[ -d "${DEST}/vectors" && -d "${DEST}/golden/engine" && "$(stored_pin)" == "${PIN}" ]]; then
    echo "t8n corpus already present at ${DEST} (pin ${PIN})"
    exit 0
fi

CACHE="${T8N_CORPUS_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/fisco-t8n-corpus}"
if [[ ! -d "${CACHE}/.git" ]]; then
    git clone --filter=blob:none https://github.com/FISCO-BCOS/op-stack-e2e-tests "${CACHE}"
fi
git -C "${CACHE}" fetch --depth 1 origin "${PIN}"
git -C "${CACHE}" checkout --detach FETCH_HEAD

mkdir -p "$(dirname "${DEST}")"
# `ln -sfn` only replaces a symlink; a pre-existing REAL directory at DEST would get a
# nested link created inside it instead. Refuse that loudly — removing a real directory
# is the operator's call, not this script's.
if [[ -e "${DEST}" && ! -L "${DEST}" ]]; then
    echo "error: ${DEST} exists and is not a symlink; remove it yourself if it is stale" >&2
    exit 1
fi
ln -sfn "${CACHE}/opstack-executor/tests/t8n" "${DEST}"
printf '%s\n' "${PIN}" > "${PIN_FILE}"
echo "linked ${DEST} -> ${CACHE}/opstack-executor/tests/t8n @ ${PIN}"
