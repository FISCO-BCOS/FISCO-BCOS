#!/usr/bin/env bash
# Pin and symlink FISCO-BCOS/op-stack-e2e-tests so S6 reads
# opstack-executor/tests/t8n/{vectors,golden/engine}.
set -euo pipefail

PIN="${T8N_CORPUS_PIN:-1f1fd4d9a2e8d76a7f98231ad0a9606d01f84a8f}"
ROOT="$(git rev-parse --show-toplevel)"
DEST="${ROOT}/opstack-executor/tests/t8n"
PIN_FILE="${DEST}/.t8n-pin"

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
ln -sfn "${CACHE}/opstack-executor/tests/t8n" "${DEST}"
printf '%s\n' "${PIN}" > "${PIN_FILE}"
echo "linked ${DEST} -> ${CACHE}/opstack-executor/tests/t8n @ ${PIN}"
