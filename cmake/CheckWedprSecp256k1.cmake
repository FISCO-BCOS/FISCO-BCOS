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
# Both invariants fail closed: an empty inspection is an error, never a pass. Generators
# without greppable link rules (Xcode, Visual Studio) cannot verify invariant 1 and emit
# a loud WARNING instead of a silent pass.
#
# Expects -DBINARY_DIR=<build dir> -DWEDPR_LIB_DIR=<dir of libffi_c_*.a>
# and optionally -DGENERATOR=<CMAKE_GENERATOR>.

if(NOT DEFINED BINARY_DIR OR NOT DEFINED WEDPR_LIB_DIR)
    message(FATAL_ERROR "usage: cmake -DBINARY_DIR=<build> -DWEDPR_LIB_DIR=<dir> [-DGENERATOR=<name>] -P CheckWedprSecp256k1.cmake")
endif()

# Invariant 1: the contaminated archive must not appear in any link rule.
# Single-config Ninja puts every rule in build.ninja; Ninja Multi-Config puts them in
# build-<config>.ninja (its top-level build.ninja is only a dispatcher); Makefiles puts
# them in per-target link.txt. Xcode / Visual Studio expose no greppable rules at all.
file(GLOB ninja_rules "${BINARY_DIR}/build*.ninja")
file(GLOB_RECURSE make_rules "${BINARY_DIR}/link.txt")
set(link_rules ${ninja_rules} ${make_rules})
if(NOT link_rules)
    if(GENERATOR MATCHES "Ninja|Makefiles")
        message(FATAL_ERROR
            "found no link rules under ${BINARY_DIR} (generator: '${GENERATOR}'); "
            "refusing to report the link surface as clean on an empty inspection")
    else()
        message(WARNING
            "generator '${GENERATOR}' exposes no greppable link rules: invariant 1 "
            "(libffi_c_fisco_bcos is not linked) could NOT be verified in this build")
    endif()
endif()
set(rule_count 0)
foreach(rule_file IN LISTS link_rules)
    file(READ "${rule_file}" rule_content)
    if(rule_content MATCHES "libffi_c_fisco_bcos")
        message(FATAL_ERROR
            "${rule_file} links libffi_c_fisco_bcos.a, whose bundled stale libsecp256k1 "
            "hijacks the node binary's secp256k1_* symbols; see bcos-executor/CMakeLists.txt")
    endif()
    math(EXPR rule_count "${rule_count} + 1")
endforeach()

# Invariant 2: the linked wedprcrypto FFI archives must not export secp256k1_* symbols.
file(GLOB archives "${WEDPR_LIB_DIR}/libffi_c_*.a")
if(NOT archives)
    message(FATAL_ERROR "no wedprcrypto FFI archives found in ${WEDPR_LIB_DIR}")
endif()
list(LENGTH archives archive_count)
foreach(archive IN LISTS archives)
    if(archive MATCHES "libffi_c_fisco_bcos")
        continue()  # not linked anywhere; covered by invariant 1
    endif()
    execute_process(COMMAND nm -g --defined-only "${archive}"
        OUTPUT_VARIABLE symbols RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "nm failed on ${archive}")
    endif()
    # Any global symbol type (T/D/B/...), with an optional Mach-O leading underscore.
    if(symbols MATCHES "[ ][A-Za-z] _?secp256k1_")
        message(FATAL_ERROR
            "${archive} exports secp256k1_* symbols; linking it lets its bundled "
            "libsecp256k1 hijack the node binary's crypto — rebuild wedprcrypto without "
            "a vendored secp256k1 or stop linking this archive")
    endif()
endforeach()
message(STATUS "wedprcrypto link surface is clean of bundled secp256k1 (${rule_count} link-rule files, ${archive_count} archives checked)")
