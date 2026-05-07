file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

find_package(TBB CONFIG REQUIRED)

add_library(${MEMPOOL_TARGET} ${SRC_LIST} ${HEADERS})
target_include_directories(${MEMPOOL_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include/bcos-mempool>)
target_link_libraries(${MEMPOOL_TARGET} PUBLIC bcos-framework ${LEDGER_TARGET} ${UTILITIES_TARGET} TBB::tbb)
set_target_properties(${MEMPOOL_TARGET} PROPERTIES UNITY_BUILD "ON")

include(GNUInstallDirs)
install(TARGETS ${MEMPOOL_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")