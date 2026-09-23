# Regression guard for the libsecp256k1 link-order hijack.
#
# wedprcrypto's libffi_c_fisco_bcos.a bundles a stale vendored libsecp256k1. When the
# archive sat on the node link line ahead of libsecp256k1.a, every secp256k1_* symbol in
# the node binary (ECDH, ECDSA sign/verify/recover) silently resolved to the old copy —
# its secp256k1_ecdh predates the hashfp argument and broke the RLPx handshake against
# real geth/op-geth. Unit-test binaries never saw it because they link only the real
# library. See the NOTE in bcos-executor/CMakeLists.txt.
#
# Two invariants are checked:
#   1. no link rule in the build references libffi_c_fisco_bcos (the archive has no live
#      callers since the alt_bn128 precompiles moved to evmmax::bn254 in #5194);
#   2. the wedprcrypto archives that ARE linked (ffi_c_crypto_binary / ffi_c_zkp) export
#      no secp256k1_* symbols, so a wedprcrypto update cannot reintroduce the collision.
#
# Expects -DBINARY_DIR=<build dir> and -DWEDPR_LIB_DIR=<dir of libffi_c_*.a>.

if(NOT DEFINED BINARY_DIR OR NOT DEFINED WEDPR_LIB_DIR)
    message(FATAL_ERROR "usage: cmake -DBINARY_DIR=<build> -DWEDPR_LIB_DIR=<dir> -P CheckWedprSecp256k1.cmake")
endif()

# Invariant 1: the contaminated archive must not appear in any link rule.
set(link_rules "")
if(EXISTS "${BINARY_DIR}/build.ninja")
    list(APPEND link_rules "${BINARY_DIR}/build.ninja")
else()
    file(GLOB_RECURSE link_rules "${BINARY_DIR}/link.txt")
endif()
foreach(rule_file IN LISTS link_rules)
    file(READ "${rule_file}" rule_content)
    if(rule_content MATCHES "libffi_c_fisco_bcos")
        message(FATAL_ERROR
            "${rule_file} links libffi_c_fisco_bcos.a, whose bundled stale libsecp256k1 "
            "hijacks the node binary's secp256k1_* symbols; see bcos-executor/CMakeLists.txt")
    endif()
endforeach()

# Invariant 2: the linked wedprcrypto FFI archives must not export secp256k1_* symbols.
file(GLOB archives "${WEDPR_LIB_DIR}/libffi_c_*.a")
if(NOT archives)
    message(FATAL_ERROR "no wedprcrypto FFI archives found in ${WEDPR_LIB_DIR}")
endif()
foreach(archive IN LISTS archives)
    if(archive MATCHES "libffi_c_fisco_bcos")
        continue()  # not linked anywhere; covered by invariant 1
    endif()
    execute_process(COMMAND nm -g --defined-only "${archive}"
        OUTPUT_VARIABLE symbols RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "nm failed on ${archive}")
    endif()
    if(symbols MATCHES " T secp256k1_")
        message(FATAL_ERROR
            "${archive} exports secp256k1_* symbols; linking it lets its bundled "
            "libsecp256k1 hijack the node binary's crypto — rebuild wedprcrypto without "
            "a vendored secp256k1 or stop linking this archive")
    endif()
endforeach()
message(STATUS "wedprcrypto link surface is clean of bundled secp256k1")
