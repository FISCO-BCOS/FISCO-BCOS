#!/bin/bash

BUILD_DIR=./build

function has_dir_change() {
    dir=${1}
    checksum=${2}

    checksum_file=${BUILD_DIR}/${dir}.checksum

    echo "Verify ${dir} checksum. current:${checksum}"

    if [ ! -f "${checksum_file}" ]; then
        echo "Verify false for checksum file not exists."
        return 1
    fi

    origin_checksum=$(cat ${checksum_file})

    if [ "${origin_checksum}" == "${checksum}" ]; then
        echo "Verify ok! No need to clear cache for ${dir}"
        return 0
    else
        echo "Verify failed for checksum not the same. origin checksum:${origin_checksum}"
        return 1
    fi
}

function write_checksum_file() {
    dir=${1}
    checksum=${2}
    checksum_file=${BUILD_DIR}/${dir}.checksum
    checksum_file_dir=${checksum_file%/*}
    echo "Generate checksum file dir:" ${checksum_file_dir}
    mkdir -p ${checksum_file_dir}
    echo -n ${checksum} > ${checksum_file}
    echo "Generate checksum file:" ${checksum_file}
}

function check_and_clear_cache() {
    dir=${1}
    cache_dir=${2}
    checksum=$(git rev-parse HEAD:${dir})

    has_dir_change ${dir} ${checksum}

    has_change=$?
    if [ ${has_change} == 1 ]; then
        echo "Dir ${dir} has changed. Clear build cache: \"${cache_dir}\". Current checksum: ${checksum}"
        rm -rf ${cache_dir}

        write_checksum_file ${dir} ${checksum}
    fi
}

function check_file_and_clear_cache() {
    file=${1}
    cache_dir=${2}
    checksum_file=${file}_check.md5sum
    tmp_checksum_file=${file}_tmp.md5sum

    md5sum $(find . -type f |grep -ia ${file} |grep -vE 'build|vcpkg|deps|md5sum') > ${tmp_checksum_file}
    if [ -f "${checksum_file}" ]; then
        if diff ${checksum_file} ${tmp_checksum_file}; then
            echo "Verify ok! No need to clear cache for ${file}"
            return;
        fi
    fi

    # checksum not ok

    echo "File ${file} has changed. Clear build cache: \"${cache_dir}\" "
    rm -rf ${cache_dir}
    mv ${tmp_checksum_file} ${checksum_file}
}

# First: check file change
check_file_and_clear_cache cmake ${BUILD_DIR}

# Second: check dir change
check_and_clear_cache .github/workflows ${BUILD_DIR}
check_and_clear_cache bcos-tars-protocol/bcos-tars-protocol ${BUILD_DIR}/generated
check_and_clear_cache bcos-sync/bcos-sync/protocol/proto ${BUILD_DIR}/bcos-sync
check_and_clear_cache bcos-protocol/bcos-protocol ${BUILD_DIR}/bcos-protocol
check_and_clear_cache bcos-pbft/bcos-pbft/core/proto ${BUILD_DIR}/bcos-pbft/bcos-pbft/core
check_and_clear_cache bcos-pbft/bcos-pbft/pbft/protocol/proto ${BUILD_DIR}/bcos-pbft/bcos-pbft/pbft/protocol
check_and_clear_cache bcos-txpool/bcos-txpool/sync/protocol/proto ${BUILD_DIR}/bcos-txpool

