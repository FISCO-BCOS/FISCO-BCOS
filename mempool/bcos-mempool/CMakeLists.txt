file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

find_package(TBB CONFIG REQUIRED)

add_library(mempool ${SRC_LIST} ${HEADERS})
target_include_directories(mempool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include/bcos-mempool>)
target_link_libraries(mempool PUBLIC bcos-framework ledger bcos-utilities TBB::tbb)
set_target_properties(mempool PROPERTIES UNITY_BUILD "ON")

include(GNUInstallDirs)
install(TARGETS mempool EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")