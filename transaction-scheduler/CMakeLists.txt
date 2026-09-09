
find_package(fmt REQUIRED)

add_library(transaction-scheduler bcos-transaction-scheduler/BaselineScheduler.cpp)
target_include_directories(transaction-scheduler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-transaction-scheduler>)
target_link_libraries(transaction-scheduler PUBLIC ledger storage bcos-framework fmt::fmt)
set_target_properties(transaction-scheduler PROPERTIES UNITY_BUILD "ON")

include(GNUInstallDirs)
# Headers only: the installed bcos-engine headers include
# <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h> (EngineMPTStateRoot.h, pulled in
# by the installed EthEngineService.h). The target is deliberately NOT added to the export
# set: it links bcos-storage, and exporting that would drag RocksDB/zstd into fiscobcosTargets.
# DESTINATION is the include root like every other module, so the headers land at
# include/bcos-transaction-scheduler/*.h and resolve through the same -I<prefix>/include the
# rest of the package uses.
install(DIRECTORY "bcos-transaction-scheduler"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmark)
endif()