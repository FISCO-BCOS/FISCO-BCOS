find_package(jsoncpp CONFIG REQUIRED)

add_executable(op-payload-builder main.cpp)
target_link_libraries(op-payload-builder
    opstack-executor        # computeOpTxRoot
    protocol-tars           # BlockHeaderImpl / BlockHeaderFactoryImpl
    bcos-crypto             # keccak256Hash
    codec                   # rlp
    ledger                  # bcos::ledger::mpt (computeTrieRootVarKey)
    bcos-utilities          # fromHex / FixedBytes
    jsoncpp_static)
