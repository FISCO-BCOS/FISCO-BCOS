project(bcos-rlp-protocol VERSION ${VERSION})

file(GLOB_RECURSE ALL_SRCS bcos-rlp-protocol/*.cpp)
file(GLOB_RECURSE HEADERS bcos-rlp-protocol/*.h)

add_library(rlp-protocol STATIC ${ALL_SRCS} ${HEADERS})
target_include_directories(rlp-protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-rlp-protocol>
)
target_link_libraries(rlp-protocol PUBLIC bcos-crypto codec bcos-framework PRIVATE bcos-protocol)
set_target_properties(rlp-protocol PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS rlp-protocol EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-rlp-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
