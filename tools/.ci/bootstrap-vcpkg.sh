#!/usr/bin/env bash
set -euo pipefail

vcpkg_root="${1:-vcpkg}"
source "${vcpkg_root}/scripts/vcpkg-tool-metadata.txt"

system="$(uname -s)"
arch="$(uname -m)"
expected_sha=""
binary="vcpkg"

case "${system}" in
    Darwin*)
        asset="vcpkg-macos"
        expected_sha="${VCPKG_MACOS_SHA}"
        ;;
    Linux*)
        case "${arch}" in
            x86_64)
                asset="vcpkg-glibc"
                expected_sha="${VCPKG_GLIBC_SHA}"
                ;;
            aarch64 | arm64)
                asset="vcpkg-glibc-arm64"
                expected_sha="${VCPKG_GLIBC_ARM64_SHA}"
                ;;
            *)
                echo "Unsupported Linux architecture: ${arch}" >&2
                exit 1
                ;;
        esac
        ;;
    MINGW* | MSYS* | CYGWIN*)
        binary="vcpkg.exe"
        if [[ "${PROCESSOR_ARCHITECTURE:-}" == "ARM64" || "${PROCESSOR_IDENTIFIER:-}" == ARMv8* || "${PROCESSOR_IDENTIFIER:-}" == ARMv9* ]]; then
            asset="vcpkg-arm64.exe"
        else
            asset="vcpkg.exe"
        fi
        ;;
    *)
        echo "Unsupported operating system: ${system}" >&2
        exit 1
        ;;
esac

# gh uses the release-asset API instead of the github.com download redirect.
gh release download "${VCPKG_TOOL_RELEASE_TAG}" \
    --repo microsoft/vcpkg-tool \
    --pattern "${asset}" \
    --output "${vcpkg_root}/${binary}" \
    --clobber
chmod +x "${vcpkg_root}/${binary}"

if [[ -n "${expected_sha}" ]]; then
    actual_sha="$(shasum -a 512 "${vcpkg_root}/${binary}")"
    actual_sha="${actual_sha%% *}"
    if [[ "${actual_sha}" != "${expected_sha}" ]]; then
        echo "Unexpected ${asset} SHA-512: ${actual_sha}; expected ${expected_sha}" >&2
        exit 1
    fi
fi

"${vcpkg_root}/${binary}" version --disable-metrics
touch "${vcpkg_root}/vcpkg.disable-metrics"
