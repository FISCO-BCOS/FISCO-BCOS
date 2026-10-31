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
config_test_cases("" "${SOURCES}" "test-transaction-scheduler" "" "")
