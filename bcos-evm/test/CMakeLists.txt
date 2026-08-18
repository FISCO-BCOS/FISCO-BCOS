find_package(Boost REQUIRED unit_test_framework)

add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)


# OpEnvelopeToTarsTest is the sole guard-block test kept in bcos-evm: it verifies
# opEnvelopeToTars' sender derivation against known-signature envelopes. It is in-tree-only —
# it needs bcos-framework targets (codec / protocol-tars / ledger / engine / rpc) that exist only
# when the top-level CMakeLists.txt add_subdirectory(bcos-framework) runs first. It includes
# <engine/bcos-engine/EngineServiceImpl.h> (needs the repo root on the include path) and needs
# `engine` + `rpc` for the non-template helpers / EngineHelper symbols; the jsoncpp link covers
# EngineServiceImpl.h's include chain (bcos-rpc/jsonrpc/Common.h). `ledger`/`engine`/`rpc` are
# added by the root CMakeLists.txt AFTER bcos-evm, so the target references resolve at generate
# time (same pattern as the source branch's test CMake).
if(TARGET bcos-framework)
    find_package(jsoncpp CONFIG REQUIRED)
endif()
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
