file(GLOB_RECURSE SOURCES "*.cpp")

add_executable(test-transaction-scheduler ${SOURCES})
find_package(Boost REQUIRED serialization unit_test_framework)
find_package(FakeIt CONFIG REQUIRED)

target_link_libraries(test-transaction-scheduler
	transaction-executor
	transaction-scheduler
	executor
	ledger
	protocol-tars
	table
	bcos-framework
	ethereum-executor
	engine
	mempool
	bcos-devp2p
	rpc
	Boost::unit_test_framework
	FakeIt::FakeIt-boost
)
# TestEthereumExecutorScheduler shares the production BLOCKHASH provider
# (EthereumBlockHashLookup.h) from libinitializer; header-only, no link.
target_include_directories(test-transaction-scheduler PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../../libinitializer"
)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# FullChainFixture.h pulls RocksDB and testutils headers whose names collide with the
# using-directives other unity-merged TUs carry; compile its users standalone.
set_source_files_properties(
    "FullChainFixtureSmokeTest.cpp" "BaselineSchedulerMPTL1UpgradeTest.cpp"
    "BaselineSchedulerMPTGenesisTest.cpp" "SharedBaselineSchedulerInst.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TestEthereumExecutorScheduler pulls EthereumExecutor + bcos-ledger LedgerMethods.h
# (executor / tars headers) whose namespace-scope entities would collide with the
# other unity-merged TUs; compile standalone.
set_source_files_properties(
    "TestEthereumExecutorScheduler.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TestEthereumBlockVerifier shares the same standalone build model.
set_source_files_properties(
    "TestEthereumBlockVerifier.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TestEthereumBlockSync (download -> verify -> commit wiring) does the same and adds
# the devp2p fake-peer headers.
set_source_files_properties(
    "TestEthereumBlockSync.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TestSepoliaGenesisSync (real Sepolia genesis state load + empty-block verify).
set_source_files_properties(
    "TestSepoliaGenesisSync.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# TestWeb3RawTransaction (raw EIP-2718 -> Transaction decoder round-trip).
set_source_files_properties(
    "TestWeb3RawTransaction.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-transaction-scheduler PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" "test-transaction-scheduler" "" "")
