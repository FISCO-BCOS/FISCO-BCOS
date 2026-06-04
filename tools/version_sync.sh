#!/bin/bash
# "Copyright [2026] <fisco-bcos>"
# @ function: sync release version from CMakeLists.txt (single source) to all derived locations
# @ usage   : bash tools/version_sync.sh [--check|--dry-run]
#             --check    verify all locations match, exit 1 on mismatch (used by CI)
#             --dry-run  show pending changes as unified diff, write nothing
#             (no args)  rewrite all out-of-date locations in place
# @ author  : kyonRay
# @ file    : version_sync.sh
# @ date    : 2026-06

set -u

SHELL_FOLDER=$(
    cd "$(dirname "$0")"
    pwd
)
# VERSION_SYNC_ROOT overrides the repo root, used by tests against a /tmp fixture
REPO_ROOT=${VERSION_SYNC_ROOT:-$(
    cd "${SHELL_FOLDER}/.."
    pwd
)}

LOG_INFO() {
    echo -e "\033[32m$1\033[0m"
}
LOG_ERROR() {
    echo -e "\033[31m$1\033[0m"
}

MODE="sync"
case "${1:-}" in
"--check") MODE="check" ;;
"--dry-run") MODE="dry-run" ;;
"") ;;
*)
    echo "usage: bash tools/version_sync.sh [--check|--dry-run]"
    exit 2
    ;;
esac

# ---- authoritative source: set(VERSION "x.y.z") in CMakeLists.txt ----
CMAKELISTS="${REPO_ROOT}/CMakeLists.txt"
if [ ! -f "${CMAKELISTS}" ]; then
    LOG_ERROR "not found: ${CMAKELISTS}"
    exit 2
fi
VERSION=$(sed -n 's/^set(VERSION "\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)").*/\1/p' "${CMAKELISTS}" | head -1)
if [ -z "${VERSION}" ]; then
    LOG_ERROR "cannot extract version from ${CMAKELISTS}, expected: set(VERSION \"x.y.z\")"
    exit 2
fi
MAJOR=${VERSION%%.*}
MINOR_PATCH=${VERSION#*.}
MINOR=${MINOR_PATCH%%.*}
PATCH=${MINOR_PATCH#*.}
for part in "${MAJOR}" "${MINOR}" "${PATCH}"; do
    if [ "${part}" -gt 255 ]; then
        LOG_ERROR "version component ${part} exceeds 255, cannot encode as a BlockVersion byte"
        exit 2
    fi
done
# BlockVersion encoding (Protocol.h:109-112): one byte each for major/minor/patch + one reserved byte
VERSION_HEX=$(printf '0x%02x%02x%02x00' "$((10#${MAJOR}))" "$((10#${MINOR}))" "$((10#${PATCH}))")
ENUM_NAME="V${MAJOR}_${MINOR}_${PATCH}_VERSION"
LOG_INFO "authoritative version: ${VERSION} (${VERSION_HEX}, ${ENUM_NAME})"

MISMATCH=0
PATTERN_BROKEN=0
CHANGED=0

# portable in-place sed (BSD sed on macOS requires a suffix after -i)
sed_inplace() {
    local file=$1 expr=$2
    sed -i.vsync_bak "${expr}" "${file}" && rm -f "${file}.vsync_bak"
}

# print the unified diff a sed expression would produce, without writing
show_diff() {
    local file=$1 expr=$2
    local tmp
    tmp=$(mktemp)
    sed "${expr}" "${file}" >"${tmp}"
    diff -u "${file}" "${tmp}" || true
    rm -f "${tmp}"
}

# act on one located version occurrence according to MODE
# args: FILE LINENO FOUND_VERSION SED_EXPR
handle_target() {
    local file=$1 lineno=$2 found=$3 sed_expr=$4
    local rel=${file#"${REPO_ROOT}"/}
    if [ "${found}" = "${VERSION}" ]; then
        return 0
    fi
    case "${MODE}" in
    check)
        LOG_ERROR "${rel}:${lineno}: expected ${VERSION}, found ${found}"
        MISMATCH=1
        ;;
    dry-run)
        LOG_INFO "would update ${rel}:${lineno}: ${found} -> ${VERSION}"
        show_diff "${file}" "${sed_expr}"
        CHANGED=1
        ;;
    sync)
        sed_inplace "${file}" "${sed_expr}"
        LOG_INFO "updated ${rel}:${lineno}: ${found} -> ${VERSION}"
        CHANGED=1
        ;;
    esac
}

# generic single-occurrence target: exactly one line must match GREP_PATTERN
# args: FILE GREP_PATTERN EXTRACT_SED SED_EXPR
process_simple_target() {
    local file=$1 grep_pattern=$2 extract_sed=$3 sed_expr=$4
    local rel=${file#"${REPO_ROOT}"/}
    if [ ! -f "${file}" ]; then
        LOG_ERROR "not found: ${rel}"
        PATTERN_BROKEN=1
        return
    fi
    local count
    count=$(grep -cE "${grep_pattern}" "${file}")
    if [ "${count}" -ne 1 ]; then
        LOG_ERROR "${rel}: expected exactly 1 line matching '${grep_pattern}', found ${count} (update tools/version_sync.sh if the file format changed)"
        PATTERN_BROKEN=1
        return
    fi
    local lineno found
    lineno=$(grep -nE "${grep_pattern}" "${file}" | cut -d: -f1)
    found=$(sed -n "${extract_sed}" "${file}" | head -1)
    if [ -z "${found}" ]; then
        LOG_ERROR "${rel}:${lineno}: cannot parse current version value (unexpected format), refusing to rewrite"
        PATTERN_BROKEN=1
        return
    fi
    handle_target "${file}" "${lineno}" "${found}" "${sed_expr}"
}

# ---- BcosBuilder example tomls: compatibility_version = "x.y.z" ----
for toml in \
    "tools/BcosBuilder/max/conf/config-build-example.toml" \
    "tools/BcosBuilder/max/conf/config-deploy-example.toml" \
    "tools/BcosBuilder/pro/conf/config-build-example.toml" \
    "tools/BcosBuilder/pro/conf/config-deploy-example.toml"; do
    process_simple_target "${REPO_ROOT}/${toml}" \
        '^compatibility_version = "' \
        's/^compatibility_version = "\([0-9.]*\)".*/\1/p' \
        "s/^compatibility_version = \"[0-9.]*\"/compatibility_version = \"${VERSION}\"/"
done

# ---- genesis template: indented compatibility_version=x.y.z (no quotes) ----
process_simple_target "${REPO_ROOT}/tools/BcosBuilder/src/tpl/config.genesis" \
    '^[[:space:]]*compatibility_version=' \
    's/^[[:space:]]*compatibility_version=\([0-9.]*\)[[:space:]]*$/\1/p' \
    "s/^\([[:space:]]*\)compatibility_version=[0-9.]*/\1compatibility_version=${VERSION}/"

# ---- README.md: only the 最新版本 line (the 稳定版本 line above it is NOT managed) ----
process_readme() {
    local file="${REPO_ROOT}/README.md"
    local rel="README.md"
    if [ ! -f "${file}" ]; then
        LOG_ERROR "not found: ${rel}"
        PATTERN_BROKEN=1
        return
    fi
    local count
    count=$(grep -c "最新版本" "${file}")
    if [ "${count}" -ne 1 ]; then
        LOG_ERROR "${rel}: expected exactly 1 line containing '最新版本', found ${count} (update tools/version_sync.sh if README changed)"
        PATTERN_BROKEN=1
        return
    fi
    local lineno
    lineno=$(grep -n "最新版本" "${file}" | head -1 | cut -d: -f1)
    local line
    line=$(sed -n "${lineno}p" "${file}")
    if ! echo "${line}" | grep -q "releases/tag/v"; then
        LOG_ERROR "${rel}:${lineno}: expected a releases/tag/v link on the 最新版本 line"
        PATTERN_BROKEN=1
        return
    fi
    local found
    found=$(echo "${line}" | grep -o 'v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*' | sort -u)
    if [ "$(echo "${found}" | wc -l | tr -d ' ')" -ne 1 ]; then
        LOG_ERROR "${rel}:${lineno}: mixed versions on the 最新版本 line: $(echo "${found}" | tr '\n' ' ')"
        PATTERN_BROKEN=1
        return
    fi
    handle_target "${file}" "${lineno}" "${found#v}" \
        "${lineno}s/v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*/v${VERSION}/g"
}
process_readme

# ---- Protocol.h: BlockVersion enum entry + MAX_VERSION ----
PROTOCOL_H="${REPO_ROOT}/bcos-framework/bcos-framework/protocol/Protocol.h"
PROTOCOL_REL="bcos-framework/bcos-framework/protocol/Protocol.h"

# insert the new entry right after the opening brace of `enum class BlockVersion`
# (entries are ordered newest-first, see Protocol.h:116)
insert_enum_entry() {
    local tmp
    tmp=$(mktemp)
    awk -v entry="    ${ENUM_NAME} = ${VERSION_HEX},  // ${VERSION}" '
        { print }
        prev ~ /^enum class BlockVersion : uint32_t/ && $0 ~ /^\{[[:space:]]*$/ { print entry }
        { prev = $0 }
    ' "${PROTOCOL_H}" >"${tmp}"
    if ! grep -q "^[[:space:]]*${ENUM_NAME} = " "${tmp}"; then
        rm -f "${tmp}"
        LOG_ERROR "${PROTOCOL_REL}: insertion anchor not found ('{' line after 'enum class BlockVersion'), file left untouched"
        PATTERN_BROKEN=1
        return 1
    fi
    # cat instead of mv to preserve the original file permissions
    cat "${tmp}" >"${PROTOCOL_H}"
    rm -f "${tmp}"
}

process_protocol_header() {
    if [ ! -f "${PROTOCOL_H}" ]; then
        LOG_ERROR "not found: ${PROTOCOL_REL}"
        PATTERN_BROKEN=1
        return
    fi
    # 1. the enum entry for the current version
    local entry
    entry=$(grep -n "^[[:space:]]*${ENUM_NAME} = " "${PROTOCOL_H}" | head -1)
    if [ -z "${entry}" ]; then
        case "${MODE}" in
        check)
            LOG_ERROR "${PROTOCOL_REL}: missing BlockVersion entry: ${ENUM_NAME} = ${VERSION_HEX},  // ${VERSION}"
            MISMATCH=1
            ;;
        dry-run)
            LOG_INFO "would insert BlockVersion entry: ${ENUM_NAME} = ${VERSION_HEX},  // ${VERSION}"
            CHANGED=1
            ;;
        sync)
            if ! insert_enum_entry; then
                return
            fi
            LOG_INFO "inserted into ${PROTOCOL_REL}: ${ENUM_NAME} = ${VERSION_HEX}"
            CHANGED=1
            ;;
        esac
    else
        local entry_lineno=${entry%%:*}
        if ! echo "${entry}" | grep -q "= ${VERSION_HEX},"; then
            LOG_ERROR "${PROTOCOL_REL}:${entry_lineno}: ${ENUM_NAME} exists but its value is not ${VERSION_HEX}, refusing to auto-fix protocol semantics"
            PATTERN_BROKEN=1
            return
        fi
    fi
    # 2. MAX_VERSION must be >= the build version; it legitimately points to a NEWER
    #    entry mid-cycle (feature PRs add the next version's entry before the release bump,
    #    e.g. V3_17_0 came from PR #5193 while CMakeLists was still 3.16.4)
    local max_name
    max_name=$(sed -n 's/^[[:space:]]*MAX_VERSION = \(V[0-9_]*VERSION\),.*/\1/p' "${PROTOCOL_H}" | head -1)
    if [ -z "${max_name}" ]; then
        LOG_ERROR "${PROTOCOL_REL}: cannot locate the 'MAX_VERSION = Vx_y_z_VERSION,' line"
        PATTERN_BROKEN=1
        return
    fi
    if [ "${max_name}" = "${ENUM_NAME}" ]; then
        return 0
    fi
    local max_hex
    max_hex=$(sed -n "s/^[[:space:]]*${max_name} = \(0x[0-9a-fA-F]*\),.*/\1/p" "${PROTOCOL_H}" | head -1)
    if [ -z "${max_hex}" ]; then
        LOG_ERROR "${PROTOCOL_REL}: cannot resolve the value of ${max_name}"
        PATTERN_BROKEN=1
        return
    fi
    if [ $((max_hex)) -lt $((VERSION_HEX)) ]; then
        local max_lineno
        max_lineno=$(grep -n "^[[:space:]]*MAX_VERSION = ${max_name}," "${PROTOCOL_H}" | head -1 | cut -d: -f1)
        case "${MODE}" in
        check)
            LOG_ERROR "${PROTOCOL_REL}:${max_lineno}: MAX_VERSION = ${max_name} is older than ${VERSION}"
            MISMATCH=1
            ;;
        dry-run)
            LOG_INFO "would update MAX_VERSION: ${max_name} -> ${ENUM_NAME}"
            CHANGED=1
            ;;
        sync)
            sed_inplace "${PROTOCOL_H}" "s/^\([[:space:]]*\)MAX_VERSION = ${max_name},/\1MAX_VERSION = ${ENUM_NAME},/"
            LOG_INFO "updated MAX_VERSION: ${max_name} -> ${ENUM_NAME}"
            CHANGED=1
            ;;
        esac
    fi
    # max_hex > VERSION_HEX: pre-release state, nothing to do
}
process_protocol_header

# ---- summary ----
if [ "${PATTERN_BROKEN}" -ne 0 ]; then
    LOG_ERROR "some files no longer match the patterns this script expects, fix tools/version_sync.sh first"
    exit 2
fi
case "${MODE}" in
check)
    if [ "${MISMATCH}" -ne 0 ]; then
        LOG_ERROR "version check failed, run: bash tools/version_sync.sh  (then commit the changes)"
        exit 1
    fi
    LOG_INFO "all version locations are consistent with ${VERSION}"
    ;;
sync | dry-run)
    if [ "${CHANGED}" -eq 0 ]; then
        LOG_INFO "all version locations already at ${VERSION}, nothing to do"
    elif [ "${MODE}" = "sync" ]; then
        # re-run in check mode to verify every replacement actually took effect
        # (catches partial replacements, e.g. values carrying unexpected suffixes)
        if ! bash "$0" --check >/dev/null 2>&1; then
            LOG_ERROR "post-sync verification failed, run: bash tools/version_sync.sh --check"
            exit 2
        fi
        LOG_INFO "post-sync verification passed"
    fi
    LOG_INFO "note: Protocol.h DEFAULT_VERSION and feature flags are NOT managed by this script, review them manually"
    ;;
esac
exit 0
