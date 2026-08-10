find_package(GTest CONFIG REQUIRED)
find_package(evmone REQUIRED)

add_executable(opstack-executor-tests OpstackExecutorTest.cpp)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor           # INTERFACE: pulls ethereum-executor + bcos-evm-opstack + bcos-evm-eth + bcos-framework
    protocol-tars              # TransactionImpl / BlockHeaderImpl / TransactionReceiptFactoryImpl
    bcos-crypto                # CryptoSuite / Keccak256
    codec                      # rlp::decode for the web3 tx fixture
    rpc                        # bcos::rpc::Web3Transaction (model .cpp)
    evmone::evmone
    GTest::gtest_main)
# Web3Transaction + tars aggregate init are not unity-build friendly here.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD OFF)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)
