
# Header-only: the library was already usable without linking (every consumer's coroutine frames are
# instantiated in its own TUs), and its one source file only implemented the pmr coroutine-allocator
# support, which nothing used and which has been removed. Targets that want the allocator seam back
# should add it together with a consumer, not as a compiled stub.
add_library(bcos-task INTERFACE)
target_include_directories(bcos-task INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-task>)
# Boost::atomic was only needed by the removed pmr allocation path; the remaining headers
# (including TBBWait.h) use std::atomic only.


include(GNUInstallDirs)
# bcos-task must stay in the export set: every installed target links it, and an INTERFACE library
# that is not exported breaks install(EXPORT fiscobcosTargets).
install(TARGETS bcos-task EXPORT fiscobcosTargets)
install(DIRECTORY "bcos-task" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()