file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(utilities ${SRC_LIST} ${HEADERS})

target_include_directories(utilities SYSTEM BEFORE PUBLIC ${BOOST_INCLUDE_DIR})
# set_source_files_properties(${SRC_LIST} ${HEADERS} PROPERTIES COMPILE_FLAGS -Wno-unused-function)
target_compile_options(utilities PRIVATE -Wno-error -Wno-unused-variable)

target_link_libraries(utilities PUBLIC LevelDB Boost::Log Boost::Filesystem Snappy TBB)
add_dependencies(utilities BuildInfo.h LevelDB)

