# Fetch pinned ethereum/tests GST and EEST fixtures via FetchContent (configure-time).
# Pin metadata: assets/upstream-pins.json

include(FetchContent)

set(_evm_ref_pins_file "${CMAKE_CURRENT_LIST_DIR}/../assets/upstream-pins.json")
if(NOT EXISTS "${_evm_ref_pins_file}")
    message(FATAL_ERROR "Missing upstream pins: ${_evm_ref_pins_file}")
endif()

file(READ "${_evm_ref_pins_file}" _evm_ref_pins_json)

string(JSON _eth_tests_url GET "${_evm_ref_pins_json}" ethereum_tests url)
string(JSON _eth_tests_sha256 GET "${_evm_ref_pins_json}" ethereum_tests sha256)
string(JSON _eest_url GET "${_evm_ref_pins_json}" eest url)
string(JSON _eest_sha256 GET "${_evm_ref_pins_json}" eest sha256)

set(_evm_ref_source_eth_root "${CMAKE_CURRENT_LIST_DIR}/../assets/ethereum-tests")
set(_evm_ref_source_eest_root "${CMAKE_CURRENT_LIST_DIR}/../assets/eest")

function(_evm_ref_populate_tarball dep_name url sha256 out_root_var)
    FetchContent_Declare(
        ${dep_name}
        URL "${url}"
        URL_HASH SHA256=${sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(${dep_name} POPULATED _populated SOURCE_DIR _source_dir)
    if(NOT _populated)
        message(STATUS "FetchContent: downloading ${dep_name}")
        FetchContent_Populate(${dep_name})
        FetchContent_GetProperties(${dep_name} SOURCE_DIR _source_dir)
    endif()
    set(${out_root_var} "${_source_dir}" PARENT_SCOPE)
endfunction()

function(_evm_ref_wrap_ethereum_tests_root extracted_dir out_root_var)
    if(EXISTS "${extracted_dir}/GeneralStateTests")
        set(${out_root_var} "${extracted_dir}" PARENT_SCOPE)
        return()
    endif()

    # FetchContent unpacks fixtures_general_state_tests.tgz without the top-level directory.
    if(EXISTS "${extracted_dir}/stExample" OR EXISTS "${extracted_dir}/stSelfBalance")
        set(_wrapper "${CMAKE_BINARY_DIR}/_deps/evm_ref_ethereum_tests_root")
        file(MAKE_DIRECTORY "${_wrapper}")
        if(NOT EXISTS "${_wrapper}/GeneralStateTests")
            file(CREATE_LINK "${extracted_dir}" "${_wrapper}/GeneralStateTests" SYMBOLIC)
        endif()
        set(${out_root_var} "${_wrapper}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "Fetched ethereum/tests archive has unexpected layout under ${extracted_dir}")
endfunction()

function(_evm_ref_wrap_eest_root extracted_dir out_root_var)
    if(EXISTS "${extracted_dir}/fixtures/state_tests")
        set(${out_root_var} "${extracted_dir}" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${extracted_dir}/state_tests")
        set(_wrapper "${CMAKE_BINARY_DIR}/_deps/evm_ref_eest_root")
        file(MAKE_DIRECTORY "${_wrapper}/fixtures")
        if(NOT EXISTS "${_wrapper}/fixtures/state_tests")
            file(CREATE_LINK "${extracted_dir}/state_tests" "${_wrapper}/fixtures/state_tests" SYMBOLIC)
        endif()
        if(EXISTS "${extracted_dir}/transaction_tests" AND NOT EXISTS "${_wrapper}/fixtures/transaction_tests")
            file(CREATE_LINK "${extracted_dir}/transaction_tests" "${_wrapper}/fixtures/transaction_tests" SYMBOLIC)
        endif()
        if(EXISTS "${extracted_dir}/blockchain_tests" AND NOT EXISTS "${_wrapper}/fixtures/blockchain_tests")
            file(CREATE_LINK "${extracted_dir}/blockchain_tests" "${_wrapper}/fixtures/blockchain_tests" SYMBOLIC)
        endif()
        set(${out_root_var} "${_wrapper}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR "Fetched EEST archive has unexpected layout under ${extracted_dir}")
endfunction()

if(BCOS_EVM_SPECS_TESTS_FETCH_ASSETS)
    _evm_ref_populate_tarball(
        evm_ref_ethereum_tests_gst
        "${_eth_tests_url}"
        "${_eth_tests_sha256}"
        _eth_extracted_dir
    )
    _evm_ref_wrap_ethereum_tests_root("${_eth_extracted_dir}" EVM_REF_ETHEREUM_TESTS_ROOT)

    _evm_ref_populate_tarball(
        evm_ref_eest_fixtures
        "${_eest_url}"
        "${_eest_sha256}"
        _eest_extracted_dir
    )
    _evm_ref_wrap_eest_root("${_eest_extracted_dir}" EVM_REF_EEST_ROOT)
else()
    if(EXISTS "${_evm_ref_source_eth_root}/GeneralStateTests")
        set(EVM_REF_ETHEREUM_TESTS_ROOT "${_evm_ref_source_eth_root}")
    elseif(DEFINED ENV{ETHEREUM_TESTS_ROOT})
        set(EVM_REF_ETHEREUM_TESTS_ROOT "$ENV{ETHEREUM_TESTS_ROOT}")
    else()
        message(FATAL_ERROR
            "BCOS_EVM_SPECS_TESTS_FETCH_ASSETS=OFF but GeneralStateTests not found under "
            "${_evm_ref_source_eth_root}; set ETHEREUM_TESTS_ROOT or run fetch manually")
    endif()

    if(EXISTS "${_evm_ref_source_eest_root}/fixtures/state_tests")
        set(EVM_REF_EEST_ROOT "${_evm_ref_source_eest_root}")
    elseif(DEFINED ENV{EEST_ROOT})
        set(EVM_REF_EEST_ROOT "$ENV{EEST_ROOT}")
    else()
        message(FATAL_ERROR
            "BCOS_EVM_SPECS_TESTS_FETCH_ASSETS=OFF but EEST fixtures not found under "
            "${_evm_ref_source_eest_root}; set EEST_ROOT or run fetch manually")
    endif()
endif()

set(EVM_REF_ETHEREUM_TESTS_ROOT "${EVM_REF_ETHEREUM_TESTS_ROOT}" CACHE INTERNAL
    "Pinned ethereum/tests GeneralStateTests parent directory")
set(EVM_REF_EEST_ROOT "${EVM_REF_EEST_ROOT}" CACHE INTERNAL
    "Pinned EEST fixtures root directory")

message(STATUS "specs-tests ethereum/tests root: ${EVM_REF_ETHEREUM_TESTS_ROOT}")
message(STATUS "specs-tests EEST root: ${EVM_REF_EEST_ROOT}")
