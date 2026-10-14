#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: (re)compile the vendored CI contracts under tools/.ci/contracts/<Name>/ into
#             <Name>.bin (keccak256 solc), <Name>.sm.bin (sm3 solc) and <Name>.abi with the
#             pinned solc version; `--check` recompiles into a temp dir and fails when any
#             artefact drifts from the committed one (reviewers run it on a PR that touches
#             a .sol or a .bin). solc comes from SOLC_DIR (default ~/.fisco/solc, the layout
#             console's download_solc.sh produces: <version>/keccak256/solc and
#             <version>/sm3/solc); the binary's reported version must equal SOLC_VERSION.
#             Usage: bash tools/.ci/contracts/regenerate.sh [--check] [Name...]
# @ file    : regenerate.sh
set -euo pipefail

contracts_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SOLC_VERSION="${SOLC_VERSION:-0.8.11}"
SOLC_DIR="${SOLC_DIR:-${HOME}/.fisco/solc}"
solc_keccak="${SOLC_DIR}/${SOLC_VERSION}/keccak256/solc"
solc_sm3="${SOLC_DIR}/${SOLC_VERSION}/sm3/solc"

# ContractTestAll.sol swaps ECRecoverTest for ECRecoverSMTest on an SM chain (what the console
# CI did with sed before deploying); the .sm.bin is built from that variant
sm_source_sed()
{
    case "${1}" in
        ContractTestAll) echo 's/ECRecoverTest/ECRecoverSMTest/g' ;;
        *) echo '' ;;
    esac
}

check_mode=false
names=()
for arg in "$@"; do
    case "${arg}" in
        --check) check_mode=true ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) names+=("${arg}") ;;
    esac
done
if (( ${#names[@]} == 0 )); then
    for dir in "${contracts_dir}"/*/; do
        names+=("$(basename "${dir}")")
    done
fi

for solc in "${solc_keccak}" "${solc_sm3}"; do
    if [[ ! -x "${solc}" ]]; then
        echo "solc not found: ${solc} (set SOLC_DIR; console's tools/download_solc.sh -v ${SOLC_VERSION} fetches it)" >&2
        exit 1
    fi
    version=$("${solc}" --version | tail -n 1)
    if [[ "${version}" != *"${SOLC_VERSION}+"* ]]; then
        echo "${solc} reports '${version}', expected ${SOLC_VERSION}" >&2
        exit 1
    fi
done

# run_solc <src_dir> <solc> <out_dir> <file>: compile, show solc's output only when it fails
run_solc()
{
    local log
    log=$(mktemp)
    if ! (cd "${1}" && "${2}" --bin --abi --overwrite -o "${3}" "${4}" >"${log}" 2>&1); then
        cat "${log}" >&2
        rm -f "${log}"
        return 1
    fi
    rm -f "${log}"
}

# compile_one <Name> <out_dir>: writes <out_dir>/<Name>.{bin,sm.bin,abi}
compile_one()
{
    local name="${1}"
    local out="${2}"
    local src="${contracts_dir}/${name}"
    [[ -f "${src}/${name}.sol" ]] || { echo "no ${src}/${name}.sol" >&2; return 1; }
    local work
    work=$(mktemp -d)
    local keccak_out="${work}/keccak" sm_out="${work}/sm" sm_src="${work}/sm-src"
    mkdir -p "${keccak_out}" "${sm_out}" "${sm_src}"
    # solc prints SPDX/unused-return warnings for the upstream sources; only a failure matters
    run_solc "${src}" "${solc_keccak}" "${keccak_out}" "${name}.sol"
    cp "${src}"/*.sol "${sm_src}/"
    local expr
    expr=$(sm_source_sed "${name}")
    if [[ -n "${expr}" ]]; then
        sed -i.bak -e "${expr}" "${sm_src}/${name}.sol" && rm -f "${sm_src}/${name}.sol.bak"
    fi
    run_solc "${sm_src}" "${solc_sm3}" "${sm_out}" "${name}.sol"
    mkdir -p "${out}"
    cp "${keccak_out}/${name}.bin" "${out}/${name}.bin"
    cp "${sm_out}/${name}.bin" "${out}/${name}.sm.bin"
    cp "${keccak_out}/${name}.abi" "${out}/${name}.abi"
    rm -rf "${work}"
}

status=0
for name in "${names[@]}"; do
    if ${check_mode}; then
        tmp=$(mktemp -d)
        compile_one "${name}" "${tmp}"
        name_status=0
        for artefact in "${name}.bin" "${name}.sm.bin" "${name}.abi"; do
            if ! cmp -s "${tmp}/${artefact}" "${contracts_dir}/${name}/${artefact}"; then
                echo "DRIFT: ${name}/${artefact} differs from a fresh solc ${SOLC_VERSION} build" >&2
                name_status=1
                status=1
            fi
        done
        rm -rf "${tmp}"
        (( name_status == 0 )) && echo "ok: ${name} artefacts match solc ${SOLC_VERSION}"
    else
        compile_one "${name}" "${contracts_dir}/${name}"
        echo "wrote ${contracts_dir}/${name}/${name}.{bin,sm.bin,abi}"
    fi
done
exit "${status}"
